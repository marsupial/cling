//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vvasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/Interpreter/Transaction.h"

#include "IncrementalExecutor.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Sema.h"

#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

namespace cling {

  Transaction::Transaction() {
    Initialize();
  }

  Transaction::Transaction(const CompilationOptions& Opts) {
    m_Opts = Opts; // intentional copy.
  }

  void Transaction::Initialize() {
    m_NestedTransactions.reset(0);
    m_Parent = 0;
    m_State = kCollecting;
    m_IssuedDiags = kNone;
    m_Opts = CompilationOptions();
    m_Module = 0;
    m_ExeUnload = {(void*)(size_t)-1};
    m_Next = 0;
    m_BufferFID = FileID(); // sets it to invalid.
    m_Exe = 0;
  }

  Transaction::~Transaction() {
    if (hasNestedTransactions())
      for (size_t i = 0; i < m_NestedTransactions->size(); ++i) {
        assert(((*m_NestedTransactions)[i]->getState() == kCommitted
                || (*m_NestedTransactions)[i]->getState() == kRolledBack)
               && "All nested transactions must be committed!");
        delete (*m_NestedTransactions)[i];
      }
  }

  NamedDecl* Transaction::containsNamedDecl(llvm::StringRef name) const {
    for (auto I = decls_begin(), E = decls_end(); I < E; ++I) {
      for (auto DI : I->m_DGR) {
        if (NamedDecl* ND = dyn_cast<NamedDecl>(DI)) {
          if (name.equals(ND->getNameAsString()))
            return ND;
        }
        else if (LinkageSpecDecl* LSD = dyn_cast<LinkageSpecDecl>(DI)) {
          for (Decl* DI : LSD->decls()) {
            if (NamedDecl* ND = dyn_cast<NamedDecl>(DI)) {
              if (name.equals(ND->getNameAsString()))
                return ND;
            }
          }
        }
      }
    }
    return 0;
  }

  void Transaction::addNestedTransaction(Transaction* nested) {
    // Create lazily the list
    if (!m_NestedTransactions)
      m_NestedTransactions.reset(new NestedTransactions());

    nested->setParent(this);
    // Leave a marker in the parent transaction, where the nested transaction
    // started.
    DelayCallInfo marker(clang::DeclGroupRef(), Transaction::kCCINone);
    m_DeclQueue.push_back(marker);
    m_NestedTransactions->push_back(nested);
  }

  void Transaction::removeNestedTransaction(Transaction* nested) {
    assert(hasNestedTransactions() && "Does not contain nested transactions");
    int nestedPos = -1;
    for (size_t i = 0; i < m_NestedTransactions->size(); ++i)
      if ((*m_NestedTransactions)[i] == nested) {
        nestedPos = i;
        break;
      }
    assert(nestedPos > -1 && "Not found!?");
    m_NestedTransactions->erase(m_NestedTransactions->begin() + nestedPos);
    // We need to remove the marker too.
    int markerPos = -1;
    for (iterator I = decls_begin(), E = decls_end(); I != E; ++I) {
      if (I->m_DGR.isNull() && I->m_Call == kCCINone) {
        ++markerPos;
        if (nestedPos == markerPos) {
          erase(I); // Safe because of the break stmt.
          break;
        }
      }
    }
    if (!m_NestedTransactions->size())
      m_NestedTransactions.reset(0);
  }

  void Transaction::append(DelayCallInfo DCI) {
    assert(!DCI.m_DGR.isNull() && "Appending null DGR?!");
    assert(getState() == kCollecting
           && "Cannot append declarations in current state.");
    forceAppend(DCI);
  }

  void Transaction::forceAppend(DelayCallInfo DCI) {
    assert(!DCI.m_DGR.isNull() && "Appending null DGR?!");
    assert((getState() == kCollecting || getState() == kCompleted)
           && "Must not be");

#ifndef NDEBUG
    // Check for duplicates
    for (size_t i = 0, e = m_DeclQueue.size(); i < e; ++i) {
      DelayCallInfo &oldDCI (m_DeclQueue[i]);
      // FIXME: This is possible bug in clang, which will instantiate one and
      // the same CXXStaticMemberVar several times. This happens when there are
      // two dependent expressions and the first uses another declaration from
      // the redeclaration chain. This will force Sema in to instantiate the
      // definition (usually the most recent decl in the chain) and then the
      // second expression might referece the definition (which was already)
      // instantiated, but Sema seems not to keep track of these kinds of
      // instantiations, even though the points of instantiation are the same!
      //
      // This should be investigated further when we merge with newest clang.
      // This is triggered by running the roottest: ./root/io/newstl
      if (oldDCI.m_Call == kCCIHandleCXXStaticMemberVarInstantiation)
        continue;
      // It is possible to have duplicate calls to HandleVTable with the same
      // declaration, because each time Sema believes a vtable is used it emits
      // that callback.
      // For reference (clang::CodeGen::CodeGenModule::EmitVTable).
      if (oldDCI.m_Call != kCCIHandleVTable
          && oldDCI.m_Call != kCCIHandleCXXImplicitFunctionInstantiation)
        assert(oldDCI != DCI && "Duplicates?!");
    }
#endif

    if (comesFromASTReader(DCI.m_DGR))
      m_DeserializedDeclQueue.push_back(DCI);
    else
      m_DeclQueue.push_back(DCI);
  }

  void Transaction::append(clang::DeclGroupRef DGR) {
    append(DelayCallInfo(DGR, kCCIHandleTopLevelDecl));
  }

  void Transaction::append(Decl* D) {
    append(DeclGroupRef(D));
  }

  void Transaction::forceAppend(Decl* D) {
    forceAppend(DelayCallInfo(DeclGroupRef(D), kCCIHandleTopLevelDecl));
  }

  void Transaction::append(MacroDirectiveInfo MDE) {
    assert(MDE.m_II && "Appending null IdentifierInfo?!");
    assert(MDE.m_MD && "Appending null MacroDirective?!");
    assert(getState() == kCollecting
           && "Cannot append declarations in current state.");
#ifndef NDEBUG
    // Check for duplicates
    for (size_t i = 0, e = m_MacroDirectiveInfoQueue.size(); i < e; ++i) {
      MacroDirectiveInfo &oldMacroDirectiveInfo (m_MacroDirectiveInfoQueue[i]);
      assert(oldMacroDirectiveInfo != MDE && "Duplicates?!");
    }
#endif

    m_MacroDirectiveInfoQueue.push_back(MDE);
  }

  unsigned Transaction::getUniqueID() const {
    return m_BufferFID.getHashValue();
  }

  void Transaction::erase(iterator pos) {
    assert(!empty() && "Erasing from an empty transaction.");
    m_DeclQueue.erase(pos);
  }

  void Transaction::DelayCallInfo::dump() const {
    PrintingPolicy Policy((LangOptions()));
    print(llvm::errs(), Policy, /*Indent*/0, /*PrintInstantiation*/true);
  }

  void Transaction::DelayCallInfo::print(llvm::raw_ostream& Out,
                                         const PrintingPolicy& Policy,
                                         unsigned Indent,
                                         bool PrintInstantiation,
                                    llvm::StringRef prependInfo /*=""*/) const {
    static const char* const stateNames[Transaction::kCCINumStates] = {
      "kCCINone",
      "kCCIHandleTopLevelDecl",
      "kCCIHandleInterestingDecl",
      "kCCIHandleTagDeclDefinition",
      "kCCIHandleVTable",
      "kCCIHandleCXXImplicitFunctionInstantiation",
      "kCCIHandleCXXStaticMemberVarInstantiation",
      "kCCICompleteTentativeDefinition",
    };
    assert((sizeof(stateNames) /sizeof(void*)) == Transaction::kCCINumStates
           && "Missing states?");
    if (!prependInfo.empty()) {
      Out.changeColor(llvm::raw_ostream::RED);
      Out << prependInfo;
      Out.resetColor();
      Out << ", ";
    }
    Out.changeColor(llvm::raw_ostream::BLUE);
    Out << stateNames[m_Call];
    Out.changeColor(llvm::raw_ostream::GREEN);
    Out << " <- ";
    Out.resetColor();
    for (DeclGroupRef::const_iterator I = m_DGR.begin(), E = m_DGR.end();
         I != E; ++I) {
        if (*I)
          (*I)->print(Out, Policy, Indent, PrintInstantiation);
        else
          Out << "<<NULL DECL>>";
        Out << '\n';
    }
  }

  void Transaction::MacroDirectiveInfo::dump(const clang::Preprocessor& PP) const {
    print(llvm::errs(), PP);
  }

  void Transaction::MacroDirectiveInfo::print(llvm::raw_ostream& Out,
                                              const clang::Preprocessor& PP) const {
    PP.printMacro(this->m_II, this->m_MD, Out);
  }

  void Transaction::dump(const clang::Sema& Sema) const {
    const ASTContext& C = Sema.getASTContext();
    PrintingPolicy Policy = C.getPrintingPolicy();
    print(Sema, llvm::errs(), Policy, /*Indent*/0, /*PrintInstantiation*/true);
  }

  void Transaction::dumpPretty(const clang::Sema& Sema) const {
    const ASTContext& C = Sema.getASTContext();
    PrintingPolicy Policy(C.getLangOpts());
    print(Sema, llvm::errs(), Policy, /*Indent*/0, /*PrintInstantiation*/true);
  }

  void Transaction::print(const clang::Sema& Sema, llvm::raw_ostream& Out,
                          const PrintingPolicy& Policy, unsigned Indent,
                          bool PrintInstantiation) const {
    int nestedT = 0;
    for (const_iterator I = decls_begin(), E = decls_end(); I != E; ++I) {
      if (I->m_DGR.isNull()) {
        assert(hasNestedTransactions() && "DGR is null even if no nesting?");
        // print the nested decl
        Out<< "\n";
        Out<<"+====================================================+\n";
        Out<<"        Nested Transaction" << nestedT << "           \n";
        Out<<"+====================================================+\n";
        (*m_NestedTransactions)[nestedT++]->print(Sema, Out, Policy, Indent,
                                                  PrintInstantiation);
        Out<< "\n";
        Out<<"+====================================================+\n";
        Out<<"          End Transaction" << nestedT << "            \n";
        Out<<"+====================================================+\n";
      }
      I->print(Out, Policy, Indent, PrintInstantiation);
    }

    // Print the deserialized decls if any.
    for (const_iterator I = deserialized_decls_begin(),
           E = deserialized_decls_end(); I != E; ++I) {
      assert(!I->m_DGR.isNull() && "Must not contain null DGR.");
      I->print(Out, Policy, Indent, PrintInstantiation, "Deserialized");
    }

    for (Transaction::const_reverse_macros_iterator MI = rmacros_begin(),
           ME = rmacros_end(); MI != ME; ++MI) {
      MI->print(Out, Sema.getPreprocessor());
    }
  }

  void Transaction::printStructure(size_t nindent) const {
    static const char* const stateNames[kNumStates] = {
      "Collecting",
      "kCompleted",
      "RolledBack",
      "RolledBackWithErrors",
      "Committed"
    };
    assert((sizeof(stateNames) / sizeof(void*)) == kNumStates
           && "Missing a state to print.");
    std::string indent(nindent, ' ');
    llvm::errs() << indent << "Transaction @" << this << ": \n";
    for (const_nested_iterator I = nested_begin(), E = nested_end();
         I != E; ++I) {
      (*I)->printStructure(nindent + 3);
    }
    llvm::errs() << indent << " state: " << stateNames[getState()]
                 << " decl groups, ";
    if (hasNestedTransactions())
      llvm::errs() << m_NestedTransactions->size();
    else
      llvm::errs() << "0";

    llvm::errs() << " nested transactions\n"
                 << ", parent: " << m_Parent
                 << ", next: " << m_Next << "\n";
  }

  void Transaction::printStructureBrief(size_t nindent /*=0*/) const {
    std::string indent(nindent, ' ');
    llvm::errs() << indent << "<cling::Transaction* " << this
                 << " isEmpty=" << empty();
    llvm::errs() << " isCommitted=" << (getState() == kCommitted);
    llvm::errs() <<"> \n";

    for (const_nested_iterator I = nested_begin(), E = nested_end();
         I != E; ++I) {
      llvm::errs() << indent << "`";
      (*I)->printStructureBrief(nindent + 3);
    }
  }

  bool Transaction::comesFromASTReader(DeclGroupRef DGR) const {
    assert(!DGR.isNull() && "DeclGroupRef is Null!");
    if (getCompilationOpts().CodeGenerationForModule)
      return true;

    // Take the first/only decl in the group.
    Decl* D = *DGR.begin();
    return D->isFromASTFile();
  }

} // end namespace cling
