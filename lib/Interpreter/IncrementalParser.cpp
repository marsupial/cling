//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Axel Naumann <axel@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "IncrementalParser.h"

#include "AutoSynthesizer.h"
#include "BackendPasses.h"
#include "CheckEmptyTransactionTransformer.h"
#include "ClingPragmas.h"
#include "DeclCollector.h"
#include "DeclExtractor.h"
#include "DynamicLookup.h"
#include "IncrementalExecutor.h"
#include "NullDerefProtectionTransformer.h"
#include "ValueExtractionSynthesizer.h"
#include "TransactionPool.h"
#include "ASTTransformer.h"
#include "ValuePrinterSynthesizer.h"
#include "ObjCSupport.h"
#include "cling/Interpreter/CIFactory.h"
#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/InterpreterCallbacks.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Utils/AST.h"

#include "clang/AST/Attr.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclGroup.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/FileManager.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/Parse/Parser.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "clang/Serialization/ASTWriter.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CrashRecoveryContext.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_os_ostream.h"

#include <iostream>
#include <stdio.h>
#include <sstream>

// Include the necessary headers to interface with the Windows registry and
// environment.
#ifdef _MSC_VER
  #define WIN32_LEAN_AND_MEAN
  #define NOGDI
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <Windows.h>
  #include <sstream>
  #define popen _popen
  #define pclose _pclose
  #pragma comment(lib, "Advapi32.lib")
#endif

using namespace clang;

namespace {

  static const Token* getMacroToken(const Preprocessor& PP, const char* Macro) {
    if (const IdentifierInfo* II = PP.getIdentifierInfo(Macro)) {
      if (const DefMacroDirective* MD = llvm::dyn_cast_or_null
          <DefMacroDirective>(PP.getLocalMacroDirective(II))) {
        if (const clang::MacroInfo* MI = MD->getMacroInfo()) {
          if (MI->getNumTokens() == 1)
            return MI->tokens_begin();
        }
      }
    }
    return nullptr;
  }

  ///\brief Check the compile-time C++ ABI version vs the run-time ABI version,
  /// a mismatch could cause havoc. Reports if ABI versions differ.
  static bool CheckABICompatibility(clang::CompilerInstance* CI) {

    const clang::Preprocessor& PP = CI->getPreprocessor();

#ifdef CLING_CLANG_RUNTIME_PATCH
    // If CLING_CLANG_RUNTIME_PATCH is defined here, we'll loose out on some
    // compile-time optimizations that can be performed.
    if (getMacroToken(PP, "CLING_CLANG_RUNTIME_PATCH")) {
      llvm::errs() <<
        "Warning in cling::IncrementalParser::CheckABICompatibility():\n"
        "  CLING_CLANG_RUNTIME_PATCH should not be defined\n";
    }
#endif

#ifdef _MSC_VER
    // For MSVC we do not use CLING_CXXABI*
    HKEY regVS;
  #if (_MSC_VER >= 1900)
    int VSVersion = (_MSC_VER / 100) - 5;
  #else
    int VSVersion = (_MSC_VER / 100) - 6;
  #endif
    std::stringstream subKey;
    subKey << "VisualStudio.DTE." << VSVersion << ".0";
    if (RegOpenKeyEx(HKEY_CLASSES_ROOT, subKey.str().c_str(), 0, KEY_READ,
                     &regVS) == ERROR_SUCCESS) {
      RegCloseKey(regVS);
      return true;
    }

#endif // !_MSC_VER

#if defined(__GLIBCXX__) || defined(_LIBCPP_VERSION)
  #ifdef __GLIBCXX__
    auto CLING_CXXABIV = __GLIBCXX__;
    const char* CLING_CXXABIS = "__GLIBCXX__";
  #elif _LIBCPP_VERSION
    auto CLING_CXXABIV = _LIBCPP_VERSION;
    const char* CLING_CXXABIS = "_LIBCPP_VERSION";
  #endif

    llvm::StringRef tokStr;
    if (const clang::Token* Tok = getMacroToken(PP, CLING_CXXABIS)) {
      if (Tok->isLiteral() && Tok->getLength() && Tok->getLiteralData()) {
        std::string cxxabivStr;
        llvm::raw_string_ostream cxxabivStrStrm(cxxabivStr);
        cxxabivStrStrm << CLING_CXXABIV;
        
        tokStr = llvm::StringRef(Tok->getLiteralData(), Tok->getLength());
        if (tokStr.equals(cxxabivStrStrm.str()))
          return true;
      }
    }

    llvm::errs() <<
      "Warning in cling::IncrementalParser::CheckABICompatibility():\n"
      "  Possible C++ standard library mismatch, compiled with "
      << CLING_CXXABIS << " '" << CLING_CXXABIV
      << "' but extraction of runtime standard library version ";
    if (!tokStr.empty())
      llvm::errs() << "was: '" << tokStr << "'.\n";
    else
      llvm::errs() << "failed.\n";

#elif defined(_MSC_VER)

    llvm::errs() <<
      "Warning in cling::IncrementalParser::CheckABICompatibility():\n"
      "  Possible C++ standard library mismatch, compiled with Visual "
      "Studio v" << VSVersion << ".0,\nbut this version of Visual Studio "
      "was not found in your system's registry.\n";

#else // Unknown platform

    llvm::errs() <<
      "Warning in cling::IncrementalParser::CheckABICompatibility():\n"
      "  C++ ABI check not implemented for this standard library\n";

#endif

    return false;
  }
} // unnamed namespace

namespace cling {
  IncrementalParser::IncrementalParser(Interpreter* interp, const char* llvmdir):
    m_Interpreter(interp),
    m_CI(CIFactory::createCI("", interp->getOptions(), llvmdir)),
    m_Consumer(nullptr), m_ModuleNo(0), m_PragmaHandler(nullptr) {

    if (!m_CI) {
      llvm::errs() << "Compiler instance could not be created.\n";
      return;
    } else if (m_Interpreter->getOptions().CompilerOpts.HasOutput)
      return;

    m_Consumer = dyn_cast<DeclCollector>(&m_CI->getSema().getASTConsumer());
    if (!m_Consumer) {
      llvm::errs() << "No AST consumer available.\n";
      return;
    }

    if (m_CI->getFrontendOpts().ProgramAction != frontend::ParseSyntaxOnly){
      #ifdef CLING_OBJC_SUPPORT
        cling::objectivec::ObjCSupport::create(m_CI->getInvocation(),
                                     m_Interpreter->getDynamicLibraryManager());
      #endif

      m_CodeGen.reset(CreateLLVMCodeGen(m_CI->getDiagnostics(), "cling-module-0",
                                        m_CI->getHeaderSearchOpts(),
                                        m_CI->getPreprocessorOpts(),
                                        m_CI->getCodeGenOpts(),
                                        *(m_Interpreter->getLLVMContext())));

      m_Consumer->setContext(this, m_CodeGen.get());
    } else {
      m_Consumer->setContext(this, 0);
    }

    initializeVirtualFile();
  }

  bool
  IncrementalParser::Initialize(llvm::SmallVectorImpl<ParseResultTransaction>&
                                result, bool isChildInterpreter) {
    m_TransactionPool.reset(new TransactionPool);
    if (!m_TransactionPool) {
      llvm::errs() << "TransactionPool could not be created.\n";
      return false;
    }

    if (hasCodeGenerator()) {
      getCodeGenerator()->Initialize(getCI()->getASTContext());
      m_BackendPasses.reset(new BackendPasses(getCI()->getCodeGenOpts(),
                                              getCI()->getTargetOpts(),
                                              getCI()->getLangOpts()));
      if (!m_BackendPasses) {
        llvm::errs() << "BackendPasses could not be created.\n";
        return false;
      }
    }

    CompilationOptions CO;
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = CompilationOptions::VPDisabled;
    CO.CodeGeneration = hasCodeGenerator();

    bool Success = true;
    Transaction* CurT = beginTransaction(CO);
    Preprocessor& PP = m_CI->getPreprocessor();

    // Pull in PCH.
    const std::string& PCHFileName
      = m_CI->getInvocation().getPreprocessorOpts().ImplicitPCHInclude;
    if (!PCHFileName.empty()) {
      Transaction* CurT = beginTransaction(CO);
      DiagnosticErrorTrap Trap(m_CI->getSema().getDiagnostics());
      m_CI->createPCHExternalASTSource(PCHFileName,
                                       true /*DisablePCHValidation*/,
                                       true /*AllowPCHWithCompilerErrors*/,
                                       0 /*DeserializationListener*/,
                                       true /*OwnsDeserializationListener*/);

      // FIXME: Better diagnosis of whether the error is safe to ignore.
      Success = !Trap.hasUnrecoverableErrorOccurred();
      result.push_back(endTransaction(CurT));
    }

    if (Success) {
      m_PragmaHandler = ClingPragmaHandler::install(*m_Interpreter);

      // Must happen after attaching the PCH, else PCH elements will end up
      // being lexed.
      PP.EnterMainSourceFile();

      Sema& Sema = m_CI->getSema();
      m_Parser.reset(new Parser(PP, Sema, false /*skipFuncBodies*/));
      if (m_Parser) {
        // Initialize the parser after PP has entered the main source file.
        m_Parser->Initialize();

        ExternalASTSource *External = Sema.getASTContext().getExternalSource();
        if (External)
          External->StartTranslationUnit(m_Consumer);

        // If I belong to the parent Interpreter, am using C++, and -noruntime
        // wasn't given on command line, then #include <new> and check ABI
        if (!isChildInterpreter && m_CI->getLangOpts().CPlusPlus &&
            !m_Interpreter->getOptions().NoRuntime) {
          if (ParseInternal("#include <new>") == kFailed) {
            // This isn't good, but still not fatal. ValuePrinter only needs
            // operator new which is a builtin, so it's still there.
            // Demote the errors to warnings, just becuase <new> failed doesn't
            // mean we shouldn't be able to do anything (and the errors will have
            // been reported).
            Sema.getDiagnostics().Reset(true);
            CurT->setIssuedDiags(Transaction::kWarnings);
          }
          // That's really C++ ABI compatibility. C has other problems ;-)
          // Still check this if <new> failed as the macros can still be defined.
          CheckABICompatibility(m_CI.get());
        }
      } else {
        ::perror("Parser could not be created");
        Success= false;
      }
    }

    // DO NOT commit the transactions here: static initialization in these
    // transactions requires gCling through local_cxa_atexit(), but that has not
    // been defined yet!
    ParseResultTransaction PRT = endTransaction(CurT);
    result.push_back(PRT);

    return Success;
  }

  void IncrementalParser::setCommands(meta::CommandTable* Cmds) {
    m_PragmaHandler->setCommands(Cmds);
  }

  bool IncrementalParser::isValid(bool initialized) const {
    return m_CI && m_CI->hasFileManager() && m_Consumer &&
           !m_VirtualFileID.isInvalid() && (!initialized ||
               (m_TransactionPool && m_Parser &&
                 (!hasCodeGenerator() || m_BackendPasses)));
  }

  const Transaction* IncrementalParser::getCurrentTransaction() const {
    return m_Consumer->getTransaction();
  }

  const Transaction* IncrementalParser::mergeTransactionsAfter(const Transaction *T, bool prev) {
    llvm::SmallVector<Transaction*, 8> merge;
    for (std::deque<Transaction*>::reverse_iterator itr = m_Transactions.rbegin(),
            end  = m_Transactions.rend(); itr != end; ++itr) {
      Transaction *cur = *itr;
      merge.push_back(cur);
      if (cur==T) {
        if (!prev)
          merge.pop_back();
        break;
      }
    }
    if (merge.size()>1) {
      llvm::SmallVector<Transaction*, 8>::reverse_iterator itr = merge.rbegin();
      Transaction *parent = *itr;
      ++itr;
      for (llvm::SmallVector<Transaction*, 8>::reverse_iterator end = merge.rend();
           itr != end; ++itr) {
        Transaction *cur = *itr;
        // Try to keep everything current
        if (m_Consumer->getTransaction()==cur)
          m_Consumer->setTransaction(parent);
        if (cur==parent->getNext()) // Make sure parent->next isn't a child
          parent->setNext(const_cast<Transaction*>(cur->getNext()));
        while (cur->getParent())   // Keep parents in place
          cur = cur->getParent();

        // Lets just make them match until there's a decent test case
        assert((cur->getIssuedDiags() != Transaction::kErrors ||
               parent->getIssuedDiags() == Transaction::kErrors)
              && "Cannot merge transactions with different error states");
        parent->addNestedTransaction(cur);

        // TODO: Propogate child's error state into parent.
        //
        // Once addNestedTransaction finishes we can't get the child's errors
        // const Transaction::IssuedDiags childErr = cur->getIssuedDiags();
        // parent->addNestedTransaction(cur);
        // if (issued < parent->getIssuedDiags())
        //   parent->setIssuedDiags(childErr);

        m_Transactions.pop_back();
      }
      return parent;
    }
    return getCurrentTransaction();
  }
  
  SourceLocation IncrementalParser::getLastMemoryBufferEndLoc() const {
    const SourceManager& SM = getCI()->getSourceManager();
    SourceLocation Result = SM.getLocForStartOfFile(m_VirtualFileID);
    return Result.getLocWithOffset(m_MemoryBuffers.size() + 1);
  }

  IncrementalParser::~IncrementalParser() {
    Transaction* T = const_cast<Transaction*>(getFirstTransaction());
    while (T) {
      assert((T->getState() == Transaction::kCommitted
              || T->getState() == Transaction::kRolledBackWithErrors
              || T->getState() == Transaction::kNumStates // reset from the pool
              || T->getState() == Transaction::kRolledBack)
             && "Not committed?");
      const Transaction* nextT = T->getNext();
      m_TransactionPool->releaseTransaction(T, false);
      T = const_cast<Transaction*>(nextT);
    }
  }

  void IncrementalParser::addTransaction(Transaction* T) {
    if (!T->isNestedTransaction() && T != getLastTransaction()) {
      if (getLastTransaction())
        m_Transactions.back()->setNext(T);
      m_Transactions.push_back(T);
    }
  }


  Transaction* IncrementalParser::beginTransaction(const CompilationOptions&
                                                   Opts) {
    Transaction* OldCurT = m_Consumer->getTransaction();
    Transaction* NewCurT = m_TransactionPool->takeTransaction();
    NewCurT->setCompilationOpts(Opts);
    // If we are in the middle of transaction and we see another begin
    // transaction - it must be nested transaction.
    if (OldCurT && OldCurT != NewCurT
        && (OldCurT->getState() == Transaction::kCollecting
            || OldCurT->getState() == Transaction::kCompleted)) {
      OldCurT->addNestedTransaction(NewCurT); // takes the ownership
    }

    m_Consumer->setTransaction(NewCurT);
    return NewCurT;
  }

  IncrementalParser::ParseResultTransaction
  IncrementalParser::endTransaction(Transaction* T) {
    assert(T && "Null transaction!?");
    assert(T->getState() == Transaction::kCollecting);

#ifndef NDEBUG
    if (T->hasNestedTransactions()) {
      for(Transaction::const_nested_iterator I = T->nested_begin(),
            E = T->nested_end(); I != E; ++I)
        assert((*I)->isCompleted() && "Nested transaction not completed!?");
    }
#endif

    T->setState(Transaction::kCompleted);

    DiagnosticsEngine& Diags = getCI()->getSema().getDiagnostics();

    //TODO: Make the enum orable.
    EParseResult ParseResult = kSuccess;

    if (Diags.hasErrorOccurred() || Diags.hasFatalErrorOccurred()
        || T->getIssuedDiags() == Transaction::kErrors) {
      T->setIssuedDiags(Transaction::kErrors);
      ParseResult = kFailed;
    } else if (Diags.getNumWarnings() > 0) {
      T->setIssuedDiags(Transaction::kWarnings);
      ParseResult = kSuccessWithWarnings;
    }

    // Empty transaction, send it back to the pool.
    if (T->empty()) {
      assert((!m_Consumer->getTransaction()
              || (m_Consumer->getTransaction() == T))
             && "Cannot release different T");
      // If a nested transaction the active one should be its parent
      // from now on. FIXME: Merge conditional with commitTransaction
      if (T->isNestedTransaction())
        m_Consumer->setTransaction(T->getParent());
      else
        m_Consumer->setTransaction((Transaction*)0);

      m_TransactionPool->releaseTransaction(T);
      return ParseResultTransaction(nullptr, ParseResult);
    }

    addTransaction(T);
    return ParseResultTransaction(T, ParseResult);
  }

  void IncrementalParser::commitTransaction(ParseResultTransaction& PRT) {
    Transaction* T = PRT.getPointer();
    if (!T) {
      if (PRT.getInt() != kSuccess) {
        // Nothing has been emitted to Codegen, reset the Diags.
        DiagnosticsEngine& Diags = getCI()->getSema().getDiagnostics();
        Diags.Reset(/*soft=*/true);
        Diags.getClient()->clear();
      }
      return;
    }

    assert(T->isCompleted() && "Transaction not ended!?");
    assert(T->getState() != Transaction::kCommitted
           && "Committing an already committed transaction.");
    assert((T->getIssuedDiags() == Transaction::kErrors || !T->empty())
           && "Valid Transactions must not be empty;");

    // If committing a nested transaction the active one should be its parent
    // from now on.
    if (T->isNestedTransaction())
      m_Consumer->setTransaction(T->getParent());

    // Check for errors...
    if (T->getIssuedDiags() == Transaction::kErrors) {
      // Make module visible to TransactionUnloader.
      bool MustStartNewModule = false;
      if (!T->isNestedTransaction() && hasCodeGenerator()) {
        MustStartNewModule = true;
        std::unique_ptr<llvm::Module> M(getCodeGenerator()->ReleaseModule());

        if (M) {
          T->setModule(std::move(M));
        }
      }
      // Module has been released from Codegen, reset the Diags now.
      DiagnosticsEngine& Diags = getCI()->getSema().getDiagnostics();
      Diags.Reset(/*soft=*/true);
      Diags.getClient()->clear();

      PRT.setPointer(nullptr);
      PRT.setInt(kFailed);
      m_Interpreter->unload(*T);

      if (MustStartNewModule) {
        // Create a new module.
        std::string ModuleName;
        {
          llvm::raw_string_ostream strm(ModuleName);
          strm << "cling-module-" << ++m_ModuleNo;
        }
        getCodeGenerator()->StartModule(ModuleName,
                                        *m_Interpreter->getLLVMContext(),
                                        getCI()->getCodeGenOpts());
      }
      return;
    }

    if (T->hasNestedTransactions()) {
      Transaction* TopmostParent = T->getTopmostParent();
      EParseResult PR = kSuccess;
      if (TopmostParent->getIssuedDiags() == Transaction::kErrors)
        PR = kFailed;
      else if (TopmostParent->getIssuedDiags() == Transaction::kWarnings)
        PR = kSuccessWithWarnings;

      for (Transaction::const_nested_iterator I = T->nested_begin(),
            E = T->nested_end(); I != E; ++I)
        if ((*I)->getState() != Transaction::kCommitted) {
          ParseResultTransaction PRT(*I, PR);
          commitTransaction(PRT);
        }
    }

    // If there was an error coming from the transformers.
    if (T->getIssuedDiags() == Transaction::kErrors) {
      m_Interpreter->unload(*T);
      return;
    }

    // Here we expect a template instantiation. We need to open the transaction
    // that we are currently work with.
    {
      Transaction* prevConsumerT = m_Consumer->getTransaction();
      m_Consumer->setTransaction(T);
      Transaction* nestedT = beginTransaction(CompilationOptions());
      // Pull all template instantiations in that came from the consumers.
      getCI()->getSema().PerformPendingInstantiations();
      ParseResultTransaction nestedPRT = endTransaction(nestedT);
      commitTransaction(nestedPRT);
      m_Consumer->setTransaction(prevConsumerT);
    }
    m_Consumer->HandleTranslationUnit(getCI()->getASTContext());


    // The static initializers might run anything and can thus cause more
    // decls that need to end up in a transaction. But this one is done
    // with CodeGen...
    if (T->getCompilationOpts().CodeGeneration && hasCodeGenerator()) {
      Transaction* prevConsumerT = m_Consumer->getTransaction();
      m_Consumer->setTransaction(T);
      codeGenTransaction(T);
      transformTransactionIR(T);
      T->setState(Transaction::kCommitted);
      if (!T->getParent()) {
        if (m_Interpreter->executeTransaction(*T)
            >= Interpreter::kExeFirstError) {
          // Roll back on error in initializers
          //assert(0 && "Error on inits.");
          m_Interpreter->unload(*T);
          T->setState(Transaction::kRolledBackWithErrors);
          return;
        }
      }
      m_Consumer->setTransaction(prevConsumerT);
    }
    T->setState(Transaction::kCommitted);

    if (InterpreterCallbacks* callbacks = m_Interpreter->getCallbacks())
      callbacks->TransactionCommitted(*T);

  }

  void IncrementalParser::markWholeTransactionAsUsed(Transaction* T) const {
    ASTContext& C = m_CI->getASTContext();
    for (Transaction::const_iterator I = T->decls_begin(), E = T->decls_end();
         I != E; ++I) {
      // Copy DCI; it might get relocated below.
      Transaction::DelayCallInfo DCI = *I;
      // FIXME: implement for multiple decls in a DGR.
      assert(DCI.m_DGR.isSingleDecl());
      Decl* D = DCI.m_DGR.getSingleDecl();
      if (!D->hasAttr<clang::UsedAttr>())
        D->addAttr(::new (D->getASTContext())
                   clang::UsedAttr(D->getSourceRange(), D->getASTContext(),
                                   0/*AttributeSpellingListIndex*/));
    }
    for (Transaction::iterator I = T->deserialized_decls_begin(),
           E = T->deserialized_decls_end(); I != E; ++I) {
      // FIXME: implement for multiple decls in a DGR.
      assert(I->m_DGR.isSingleDecl());
      Decl* D = I->m_DGR.getSingleDecl();
      if (!D->hasAttr<clang::UsedAttr>())
        D->addAttr(::new (C) clang::UsedAttr(D->getSourceRange(), C,
                                   0/*AttributeSpellingListIndex*/));
    }
  }

  void IncrementalParser::emitTransaction(Transaction* T) {
    for (auto DI = T->decls_begin(), DE = T->decls_end(); DI != DE; ++DI)
      m_Consumer->HandleTopLevelDecl(DI->m_DGR);
  }

  void IncrementalParser::codeGenTransaction(Transaction* T) {
    // codegen the transaction
    assert(T->getCompilationOpts().CodeGeneration && "CodeGen turned off");
    assert(T->getState() == Transaction::kCompleted && "Must be completed");
    assert(hasCodeGenerator() && "No CodeGen");

    // Could trigger derserialization of decls.
    Transaction* deserT = beginTransaction(CompilationOptions());


    // Commit this transaction first - T might need symbols from it, so
    // trigger emission of weak symbols by providing use.
    ParseResultTransaction PRT = endTransaction(deserT);
    commitTransaction(PRT);
    deserT = PRT.getPointer();

    // This llvm::Module is done; finalize it and pass it to the execution
    // engine.
    if (!T->isNestedTransaction() && hasCodeGenerator()) {
      // The initializers are emitted to the symbol "_GLOBAL__sub_I_" + filename.
      // Make that unique!
      ASTContext& Context = getCI()->getASTContext();
      SourceManager &SM = Context.getSourceManager();
      const clang::FileEntry *MainFile = SM.getFileEntryForID(SM.getMainFileID());
      clang::FileEntry* NcMainFile = const_cast<clang::FileEntry*>(MainFile);
      // Hack to temporarily set the file entry's name to a unique name.
      assert(MainFile->getName() == *(const char**)NcMainFile
         && "FileEntry does not start with the name");
      const char* &FileName = *(const char**)NcMainFile;
      const char* OldName = FileName;
      std::string ModName = getCodeGenerator()->GetModule()->getName().str();
      FileName = ModName.c_str();

      deserT = beginTransaction(CompilationOptions());
      // Reset the module builder to clean up global initializers, c'tors, d'tors
      getCodeGenerator()->HandleTranslationUnit(Context);
      FileName = OldName;
      auto PRT = endTransaction(deserT);
      commitTransaction(PRT);
      deserT = PRT.getPointer();

      std::unique_ptr<llvm::Module> M(getCodeGenerator()->ReleaseModule());

      if (M) {
        m_Interpreter->addModule(M.get());
        T->setModule(std::move(M));
      }

      if (T->getIssuedDiags() != Transaction::kNone) {
        // Module has been released from Codegen, reset the Diags now.
        DiagnosticsEngine& Diags = getCI()->getSema().getDiagnostics();
        Diags.Reset(/*soft=*/true);
        Diags.getClient()->clear();
      }

      // Create a new module.
      std::string ModuleName;
      {
        llvm::raw_string_ostream strm(ModuleName);
        strm << "cling-module-" << ++m_ModuleNo;
      }
      getCodeGenerator()->StartModule(ModuleName,
                                      *m_Interpreter->getLLVMContext(),
                                      getCI()->getCodeGenOpts());
    }
  }

  bool IncrementalParser::transformTransactionIR(Transaction* T) {
    // Transform IR
    bool success = true;
    //if (!success)
    //  m_Interpreter->unload(*T);
    if (m_BackendPasses && T->getModule())
      m_BackendPasses->runOnModule(*T->getModule());
    return success;
  }

  void IncrementalParser::deregisterTransaction(Transaction& T) {
    if (&T == m_Consumer->getTransaction())
      m_Consumer->setTransaction(T.getParent());

    if (Transaction* Parent = T.getParent()) {
      Parent->removeNestedTransaction(&T);
      T.setParent(0);
    } else {
      // Remove from the queue
      assert(&T == m_Transactions.back() && "Out of order transaction removal");
      m_Transactions.pop_back();
      if (!m_Transactions.empty())
        m_Transactions.back()->setNext(0);
    }

    m_TransactionPool->releaseTransaction(&T);
  }

  std::vector<const Transaction*> IncrementalParser::getAllTransactions() {
    std::vector<const Transaction*> result(m_Transactions.size());
    const cling::Transaction* T = getFirstTransaction();
    while (T) {
      result.push_back(T);
      T = T->getNext();
    }
    return result;
  }

  // Each input line is contained in separate memory buffer. The SourceManager
  // assigns sort-of invalid FileID for each buffer, i.e there is no FileEntry
  // for the MemoryBuffer's FileID. That in turn is problem because invalid
  // SourceLocations are given to the diagnostics. Thus the diagnostics cannot
  // order the overloads, for example
  //
  // Our work-around is creating a virtual file, which doesn't exist on the disk
  // with enormous size (no allocation is done). That file has valid FileEntry
  // and so on... We use it for generating valid SourceLocations with valid
  // offsets so that it doesn't cause any troubles to the diagnostics.
  //
  // +---------------------+
  // | Main memory buffer  |
  // +---------------------+
  // |  Virtual file SLoc  |
  // |    address space    |<-----------------+
  // |         ...         |<------------+    |
  // |         ...         |             |    |
  // |         ...         |<----+       |    |
  // |         ...         |     |       |    |
  // +~~~~~~~~~~~~~~~~~~~~~+     |       |    |
  // |     input_line_1    | ....+.......+..--+
  // +---------------------+     |       |
  // |     input_line_2    | ....+.....--+
  // +---------------------+     |
  // |          ...        |     |
  // +---------------------+     |
  // |     input_line_N    | ..--+
  // +---------------------+
  //
  void IncrementalParser::initializeVirtualFile() {
    SourceManager& SM = getCI()->getSourceManager();
    m_VirtualFileID = SM.getMainFileID();
    if (m_VirtualFileID.isInvalid())
      llvm::errs() << "VirtualFileID could not be created.\n";
  }

  IncrementalParser::ParseResultTransaction
  IncrementalParser::Compile(llvm::StringRef input,
                             const CompilationOptions& Opts,
                             clang::FunctionDecl** FD) {
    Transaction* CurT = beginTransaction(Opts);
    EParseResult ParseRes = ParseInternal(input, FD);

    if (ParseRes == kSuccessWithWarnings)
      CurT->setIssuedDiags(Transaction::kWarnings);
    else if (ParseRes == kFailed)
      CurT->setIssuedDiags(Transaction::kErrors);

    ParseResultTransaction PRT = endTransaction(CurT);
    commitTransaction(PRT);

    return PRT;
  }
    
  // Add the input to the memory buffer, parse it, and add it to the AST.
  IncrementalParser::EParseResult
  IncrementalParser::ParseInternal(llvm::StringRef input,
                                   clang::FunctionDecl** FD) {
    if (input.empty()) return IncrementalParser::kSuccess;

    Sema& S = getCI()->getSema();

    const CompilationOptions& CO
       = m_Consumer->getTransaction()->getCompilationOpts();

    assert(!(S.getLangOpts().Modules
             && CO.CodeGenerationForModule)
           && "CodeGenerationForModule to be removed once PCMs are available!");

    // Recover resources if we crash before exiting this method.
    llvm::CrashRecoveryContextCleanupRegistrar<Sema> CleanupSema(&S);

    Preprocessor& PP = m_CI->getPreprocessor();
    if (!PP.getCurrentLexer()) {
       PP.EnterSourceFile(m_CI->getSourceManager().getMainFileID(),
                          0, SourceLocation());
    }
    assert(PP.isIncrementalProcessingEnabled() && "Not in incremental mode!?");
    PP.enableIncrementalProcessing();

    std::ostringstream source_name;
    source_name << "input_line_" << (m_MemoryBuffers.size() + 1);

    // Create an uninitialized memory buffer, copy code in and append "\n"
    size_t InputSize = input.size(); // don't include trailing 0
    // MemBuffer size should *not* include terminating zero
    std::unique_ptr<llvm::MemoryBuffer>
      MB(llvm::MemoryBuffer::getNewUninitMemBuffer(InputSize + 1,
                                                   source_name.str()));
    char* MBStart = const_cast<char*>(MB->getBufferStart());
    memcpy(MBStart, input.data(), InputSize);
    memcpy(MBStart + InputSize, "\n", 2);

    SourceManager& SM = getCI()->getSourceManager();

    // Create SourceLocation, which will allow clang to order the overload
    // candidates for example
    SourceLocation NewLoc = getLastMemoryBufferEndLoc().getLocWithOffset(1);

    llvm::MemoryBuffer* MBNonOwn = MB.get();

    // Create FileID for the current buffer.
    FileID FID;
    if (CO.CodeCompletionOffset == -1)
    {
      FID = SM.createFileID(std::move(MB), SrcMgr::C_User,
                                 /*LoadedID*/0,
                                 /*LoadedOffset*/0, NewLoc);
    } else {
      // Create FileEntry and FileID for the current buffer.
      // Enabling the completion point only works on FileEntries.
      const clang::FileEntry* FE
        = SM.getFileManager().getVirtualFile("vfile for " + source_name.str(),
                                             InputSize, 0 /* mod time*/);
      SM.overrideFileContents(FE, std::move(MB));
      FID = SM.createFileID(FE, NewLoc, SrcMgr::C_User);

      // The completion point is set one a 1-based line/column numbering.
      // It relies on the implementation to account for the wrapper extra line.
      PP.SetCodeCompletionPoint(FE, 1/* start point 1-based line*/,
                                CO.CodeCompletionOffset+1/* 1-based column*/);
    }

    m_MemoryBuffers.push_back(std::make_pair(MBNonOwn, FID));

    // NewLoc only used for diags.
    PP.EnterSourceFile(FID, /*DirLookup*/0, NewLoc);
    m_Consumer->getTransaction()->setBufferFID(FID);

    DiagnosticsEngine& Diags = getCI()->getDiagnostics();

    bool IgnorePromptDiags = CO.IgnorePromptDiags;
    if (IgnorePromptDiags) {
      // Disable warnings which doesn't make sense when using the prompt
      // This gets reset with the clang::Diagnostics().Reset(/*soft*/=false)
      // using clang's API we simulate:
      // #pragma warning push
      // #pragma warning ignore ...
      // #pragma warning ignore ...
      // #pragma warning pop
      SourceLocation Loc = SM.getLocForStartOfFile(FID);
      Diags.pushMappings(Loc);
      // The source locations of #pragma warning ignore must be greater than
      // the ones from #pragma push

      auto setIgnore = [&](clang::diag::kind Diag) {
        Diags.setSeverity(Diag, diag::Severity::Ignored, SourceLocation());
      };

      setIgnore(clang::diag::warn_unused_expr);
      setIgnore(clang::diag::warn_unused_call);
      setIgnore(clang::diag::warn_unused_comparison);
      setIgnore(clang::diag::ext_return_has_expr);
    }
    auto setError = [&](clang::diag::kind Diag) {
      Diags.setSeverity(Diag, diag::Severity::Error, SourceLocation());
    };
    setError(clang::diag::warn_falloff_nonvoid_function);

    DiagnosticErrorTrap Trap(Diags);
    Sema::SavePendingInstantiationsRAII SavedPendingInstantiations(S);

    Parser::DeclGroupPtrTy ADecl;
    while (!m_Parser->ParseTopLevelDecl(ADecl)) {
      // If we got a null return and something *was* parsed, ignore it.  This
      // is due to a top-level semicolon, an action override, or a parse error
      // skipping something.
      if (Trap.hasErrorOccurred())
        m_Consumer->getTransaction()->setIssuedDiags(Transaction::kErrors);
      if (ADecl)
        m_Consumer->HandleTopLevelDeclAndWrapper(ADecl.get(), FD);
      
      // Debug mode asserts only one wrapper possible
#ifdef NDEBUG
      // Stop looking for it
      if (FD && *FD)
        FD = nullptr;
#endif
    };
    // We could have never entered the while block, in which case there's a
    // good chance an error occured.
    if (Trap.hasErrorOccurred())
      m_Consumer->getTransaction()->setIssuedDiags(Transaction::kErrors);

    if (CO.CodeCompletionOffset != -1) {
      assert((int)SM.getFileOffset(PP.getCodeCompletionLoc())
             == CO.CodeCompletionOffset
             && "Completion point wrongly set!");
      assert(PP.isCodeCompletionReached()
             && "Code completion set but not reached!");
      return kSuccess;
    }

#ifdef LLVM_ON_WIN32
    // Microsoft-specific:
    // Late parsed templates can leave unswallowed "macro"-like tokens.
    // They will seriously confuse the Parser when entering the next
    // source file. So lex until we are EOF.
    Token Tok;
    do {
      PP.Lex(Tok);
    } while (Tok.isNot(tok::eof));
#endif

#ifndef NDEBUG
    Token AssertTok;
    PP.Lex(AssertTok);
    assert(AssertTok.is(tok::eof) && "Lexer must be EOF when starting incremental parse!");
#endif

    if (IgnorePromptDiags) {
      SourceLocation Loc = SM.getLocForEndOfFile(m_MemoryBuffers.back().second);
      Diags.popMappings(Loc);
    }

    // Process any TopLevelDecls generated by #pragma weak.
    for (llvm::SmallVector<Decl*,2>::iterator I = S.WeakTopLevelDecls().begin(),
         E = S.WeakTopLevelDecls().end(); I != E; ++I) {
      m_Consumer->HandleTopLevelDecl(DeclGroupRef(*I));
    }

    if (m_Consumer->getTransaction()->getIssuedDiags() == Transaction::kErrors)
      return kFailed;
    else if (Diags.getNumWarnings())
      return kSuccessWithWarnings;

    return kSuccess;
  }

  void IncrementalParser::printTransactionStructure() const {
    for(size_t i = 0, e = m_Transactions.size(); i < e; ++i) {
      m_Transactions[i]->printStructureBrief();
    }
  }

  void IncrementalParser::SetTransformers(bool isChildInterpreter) {
    // Add transformers to the IncrementalParser, which owns them
    Sema* TheSema = &m_CI->getSema();
    // Register the AST Transformers
    typedef std::unique_ptr<ASTTransformer> ASTTPtr_t;
    std::vector<ASTTPtr_t> ASTTransformers;
    ASTTransformers.emplace_back(new AutoSynthesizer(TheSema));
    ASTTransformers.emplace_back(new EvaluateTSynthesizer(TheSema));
    if (hasCodeGenerator() && !m_Interpreter->getOptions().NoRuntime) {
       // Don't protect against crashes if we cannot run anything.
       // cling might also be in a PCH-generation mode; don't inject our Sema pointer
       // into the PCH.
       ASTTransformers.emplace_back(new NullDerefProtectionTransformer(m_Interpreter));
    }

    typedef std::unique_ptr<WrapperTransformer> WTPtr_t;
    std::vector<WTPtr_t> WrapperTransformers;
    if (!m_Interpreter->getOptions().NoRuntime)
      WrapperTransformers.emplace_back(new ValuePrinterSynthesizer(TheSema, 0));
    WrapperTransformers.emplace_back(new DeclExtractor(TheSema));
    if (!m_Interpreter->getOptions().NoRuntime)
      WrapperTransformers.emplace_back(new ValueExtractionSynthesizer(TheSema,
                                                           isChildInterpreter));
    WrapperTransformers.emplace_back(new CheckEmptyTransactionTransformer(TheSema));

    m_Consumer->SetTransformers(std::move(ASTTransformers),
                                std::move(WrapperTransformers));
  }


} // namespace cling
