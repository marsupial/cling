//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Lukasz Janyst <ljanyst@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/Interpreter/Interpreter.h"
#include "cling/Utils/Paths.h"
#include "ClingUtils.h"

#include "DynamicLookup.h"
#include "ExternalInterpreterSource.h"
#include "ForwardDeclPrinter.h"
#include "IncrementalExecutor.h"
#include "IncrementalParser.h"
#include "MultiplexInterpreterCallbacks.h"
#include "TransactionUnloader.h"

#include "cling/Interpreter/AutoloadCallback.h"
#include "cling/Interpreter/CIFactory.h"
#include "cling/Interpreter/ClangInternalState.h"
#include "cling/Interpreter/ClingCodeCompleteConsumer.h"
#include "cling/Interpreter/CompilationOptions.h"
#include "cling/Interpreter/DynamicExprInfo.h"
#include "cling/Interpreter/DynamicLibraryManager.h"
#include "cling/Interpreter/InterceptBuilder.h"
#include "cling/Interpreter/LookupHelper.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Interpreter/Value.h"
#include "cling/Utils/AST.h"
#include "cling/Utils/Casting.h"
#include "cling/Utils/Output.h"
#include "cling/Utils/SourceNormalization.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/ExternalPreprocessorSource.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/LexDiagnostic.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Parse/Parser.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Path.h"

#include <string>
#include <vector>

#ifdef LLVM_ON_WIN32
#include "cling/Utils/Platform.h"
#include <unordered_set>
#else
extern "C" void* __dso_handle;

#if (defined(__clang__) ? !__has_feature(cxx_rtti) : !defined(__GXX_RTTI))
#define CLING_NO_RTTI
// -fno-rtti, export the std::exceptions to the JIT
extern "C" std::type_info
  *_ZTISt9exception,
  *_ZTISt11logic_error,
  *_ZTISt11range_error,
  *_ZTISt12length_error,
  *_ZTISt12out_of_range,
  *_ZTISt13runtime_error,
  *_ZTISt9bad_alloc;
#endif // No RTTI

#endif // !LLVM_ON_WIN32

using namespace clang;

extern "C" void* __emutls_get_address(struct __emutls_control*);

namespace cling {
 namespace utils {
     // This is here, to [hopefully] inline the FileEntry contruction below
    FileEntry::FileEntry(const clang::FileEntry* FE, const char* absPath)
      : mEntry(FE), mNameOrPath(absPath),
      mFlags(kResolved | kExists |
        (DynamicLibraryManager::isSharedLib(mNameOrPath) ? kIsLibrary : 0)) {
    }
  }
}

namespace {

  static cling::Interpreter::ExecutionResult
  ConvertExecutionResult(cling::IncrementalExecutor::ExecutionResult ExeRes) {
    switch (ExeRes) {
    case cling::IncrementalExecutor::kExeSuccess:
      return cling::Interpreter::kExeSuccess;
    case cling::IncrementalExecutor::kExeFunctionNotCompiled:
      return cling::Interpreter::kExeFunctionNotCompiled;
    case cling::IncrementalExecutor::kExeUnresolvedSymbols:
      return cling::Interpreter::kExeUnresolvedSymbols;
    default: break;
    }
    return cling::Interpreter::kExeSuccess;
  }

  static bool isPracticallyEmptyModule(const llvm::Module* M) {
    return M->empty() && M->global_empty() && M->alias_empty();
  }
} // unnamed namespace

namespace cling {

  CompilationOptions::CompilationOptions(const Interpreter* I, const char* Name)
      : DeclarationExtraction(0), ValuePrinting(VPDisabled),
        ResultEvaluation(0), DynamicScoping(I->isDynamicLookupEnabled()),
        Debug(I->isPrintingDebug()), CodeGeneration(!I->isInSyntaxOnlyMode()),
        CodeGenerationForModule(0), IgnorePromptDiags(!I->isRawInputEnabled()),
        CheckPointerValidity(!I->isRawInputEnabled()),
        OptLevel(I->getDefaultOptLevel()), LineName(Name) {}

  Interpreter::PushTransactionRAII::PushTransactionRAII(const Interpreter* I)
    : m_Interpreter(I) {
    CompilationOptions CO(I);
    CO.ResultEvaluation = 0;
    CO.DynamicScoping = 0;

    m_Transaction = m_Interpreter->m_IncrParser->beginTransaction(CO);
  }

  Interpreter::PushTransactionRAII::~PushTransactionRAII() {
    pop();
  }

  void Interpreter::PushTransactionRAII::pop() const {
    IncrementalParser::ParseResultTransaction PRT
      = m_Interpreter->m_IncrParser->endTransaction(m_Transaction);
    if (PRT.getPointer()) {
      assert(PRT.getPointer()==m_Transaction && "Ended different transaction?");
      m_Interpreter->m_IncrParser->commitTransaction(PRT);
    }
  }

  Interpreter::StateDebuggerRAII::StateDebuggerRAII(const Interpreter* i)
    : m_Interpreter(i) {
    if (m_Interpreter->isPrintingDebug()) {
      const CompilerInstance& CI = *m_Interpreter->getCI();
      CodeGenerator* CG = i->m_IncrParser->getCodeGenerator();

      // The ClangInternalState constructor can provoke deserialization,
      // we need a transaction.
      PushTransactionRAII pushedT(i);

      m_State.reset(new ClangInternalState(CI.getASTContext(),
                                           CI.getPreprocessor(),
                                           CG ? CG->GetModule() : 0,
                                           CG,
                                           "aName"));
    }
  }

  Interpreter::StateDebuggerRAII::~StateDebuggerRAII() {
    if (m_State) {
      // The ClangInternalState destructor can provoke deserialization,
      // we need a transaction.
      PushTransactionRAII pushedT(m_Interpreter);
      m_State->compare("aName", m_Interpreter->m_Opts.Verbose());
      m_State.reset();
    }
  }

  Interpreter::TransactionMerge::TransactionMerge(Interpreter *interp,
                                                  bool prev) :
    m_IncrParser(*interp->m_IncrParser),
    m_Current(m_IncrParser.getLastTransaction()), m_Prev(prev) {
  }

  Interpreter::TransactionMerge::~TransactionMerge() {
    m_IncrParser.mergeTransactionsAfter(m_Current, m_Prev);
  }

  const Parser& Interpreter::getParser() const {
    return *m_IncrParser->getParser();
  }

  Parser& Interpreter::getParser() {
    return *m_IncrParser->getParser();
  }

  clang::SourceLocation Interpreter::getNextAvailableLoc() const {
    return m_IncrParser->getLastMemoryBufferEndLoc().getLocWithOffset(1);
  }

  bool Interpreter::isInSyntaxOnlyMode() const {
    return getCI()->getFrontendOpts().ProgramAction
      == clang::frontend::ParseSyntaxOnly;
  }

  bool Interpreter::isValid() const {
    // Should we also check m_IncrParser->getFirstTransaction() ?
    // not much can be done without it (its the initializing transaction)
    return m_IncrParser && m_IncrParser->isValid() &&
           m_DyLibManager && m_LookupHelper &&
           (isInSyntaxOnlyMode() || m_Executor);
  }

  const char* Interpreter::getVersion() {
    return ClingStringify(CLING_VERSION);
  }

  static bool handleSimpleOptions(const InvocationOptions& Opts) {
    if (Opts.ShowVersion) {
      cling::log() << Interpreter::getVersion() << '\n';
    }
    if (Opts.Help) {
      Opts.PrintHelp();
    }
    return Opts.ShowVersion || Opts.Help;
  }

  struct Interpreter::RuntimeIntercept {
    enum { kAtExitFunc, kWinFacetRegister = 10 };

#if defined(LLVM_ON_WIN32)
    std::unordered_set<void*> m_RuntimeFacets;

    ~RuntimeIntercept() {
      for (void *Ptr : m_RuntimeFacets)
        delete reinterpret_cast<std::_Facet_base*>(Ptr)->_Decref();
    }
#endif
    
    static int Dispatch(void* A0, void* A1, unsigned Cmd, void* T) {
      switch (Cmd) {
        case kAtExitFunc:
          reinterpret_cast<Interpreter*>(T)->AddAtExitFunc(
              utils::VoidToFunctionPtr<void (*)(void*)>(A0), A1);
          return 0;

#if defined(LLVM_ON_WIN32)
        case kWinFacetRegister:
          reinterpret_cast<RuntimeIntercept*>(T)->m_RuntimeFacets.insert(A0);
          return 0;
#endif
      }
      llvm_unreachable("Unknown action");
    }
  };

  Interpreter::Interpreter(int argc, const char* const *argv,
                           const char* llvmdir /*= 0*/, bool noRuntime,
                           const Interpreter* parentInterp) :
    m_Opts(argc, argv), m_Parenting(nullptr),
    m_UniqueCounter(parentInterp ? parentInterp->m_UniqueCounter + 1 : 0),
    m_PrintDebug(false), m_DynamicLookupDeclared(false),
    m_DynamicLookupEnabled(false), m_RawInputEnabled(false),
    m_OptLevel(parentInterp ? parentInterp->m_OptLevel : -1) {

    ::memset(m_CachedTrns, 0, sizeof(m_CachedTrns));
    if (parentInterp) {
      m_Parenting = new Interpreter*[2];
      m_Parenting[0] = this;
      m_Parenting[1] = const_cast<Interpreter*>(parentInterp);
    } else
      m_Parenting = reinterpret_cast<Interpreter**>(this);

    if (handleSimpleOptions(m_Opts))
      return;

    m_LLVMContext.reset(new llvm::LLVMContext);
    m_DyLibManager.reset(new DynamicLibraryManager(getOptions()));
    m_IncrParser.reset(new IncrementalParser(this, llvmdir));
    if (!m_IncrParser->isValid(false))
      return;

    // Initialize the opt level to what CodeGenOpts says.
    if (m_OptLevel == -1)
      m_OptLevel = getCI()->getCodeGenOpts().OptimizationLevel;

    Sema& SemaRef = getSema();
    Preprocessor& PP = SemaRef.getPreprocessor();
    // Enable incremental processing, which prevents the preprocessor destroying
    // the lexer on EOF token.
    PP.enableIncrementalProcessing();

    m_LookupHelper.reset(new LookupHelper(new Parser(PP, SemaRef,
                                                     /*SkipFunctionBodies*/false,
                                                     /*isTemp*/true), this));
    if (!m_LookupHelper)
      return;

    clang::CompilerInstance* CI = getCI();

    if (!isInSyntaxOnlyMode()) {
      m_Executor.reset(new IncrementalExecutor(SemaRef.Diags, *CI));
      if (!m_Executor)
        return;

      // Build the overloads __cxa_exit, atexit, etc.
      // Do this as early as possible so any static variables or other runtime
      // initialization during subsequent initialization will be registered for
      // destruction properly.
      //
      // FIXME: It would be nicer to emit these lazily, but the current order of
      // lookup has NotifyLazyFunctionCreators following dlsym lookup and these
      // symbols obviously exist in process/libraries.
      //
      const clang::LangOptions& LangOpts = CI->getLangOpts();
      clang::CodeGenerator* CG = m_IncrParser->getCodeGenerator();
      InterceptBuilder Overload(CG->GetModule(), &RuntimeIntercept::Dispatch);

      // C atexit, std::atexit (Windows uses this for registering static dtors)
      Overload("atexit", this);

#if !defined(LLVM_ON_WIN32)

      // Linux/ OS X API for registering static destructors
      // Defined even when not in C++ in case any other language uses it.
      Overload("__cxa_atexit", this, 3);

      // Give the user a __dso_handle in case they need it.
      // Note cling will generate code: __cxa_atexit(Dtor, 0, __dso_handle);
      // but Overload("__cxa_atexit") above replaces __dso_handle with this.
      m_Executor->addSymbol("__dso_handle", &__dso_handle, true);

#ifdef CLING_NO_RTTI
      m_Executor->addSymbol("_ZTISt9exception", &_ZTISt9exception, 1);
      m_Executor->addSymbol("_ZTISt11logic_error", &_ZTISt11logic_error, 1);
      m_Executor->addSymbol("_ZTISt11range_error", &_ZTISt11range_error, 1);
      m_Executor->addSymbol("_ZTISt12length_error", &_ZTISt12length_error, 1);
      m_Executor->addSymbol("_ZTISt12out_of_range", &_ZTISt12out_of_range, 1);
      m_Executor->addSymbol("_ZTISt13runtime_error", &_ZTISt13runtime_error, 1);
      m_Executor->addSymbol("_ZTISt9bad_alloc", &_ZTISt9bad_alloc, 1);
#endif

#else // LLVM_ON_WIN32

      // Windows specific: _onexit, __dllonexit
      Overload("__dllonexit", this, 3);
      Overload("_onexit", this);

      if (LangOpts.CPlusPlus) {
        // Windows C++ SEH handler
        m_Executor->addSymbol("_CxxThrowException",
             utils::FunctionToVoidPtr(&platform::ClingRaiseSEHException), true);

        const char* FacetReg =
            Overload.getDataLayout().hasMicrosoftFastStdCallMangling()
                ? "?_Facet_Register@std@@YAXPAV_Facet_base@1@@Z"
                : "?_Facet_Register@std@@YAXPEAV_Facet_base@1@@Z";

        m_RuntimeIntercept.reset(new RuntimeIntercept);
        if (!Overload(FacetReg, false, 1, m_RuntimeIntercept.get(),
                     RuntimeIntercept::kWinFacetRegister)) {
          m_RuntimeIntercept.reset();
        }
      }

      // FIXME: Using emulated TLS LLVM doesn't respect external TLS data.
      // By passing itself as the argument to __emutls_get_address, it can
      // return a pointer to the current thread's _Init_thread_epoch.
      // This obviously handles only one case, and would need to be rethought
      // to properly support extern __declspec(thread), though hopefully that
      // construct is dubious enough to never be used .
      m_Executor->addSymbol("__emutls_v._Init_thread_epoch",
          utils::FunctionToVoidPtr(&__emutls_get_address), true);

#endif

      if (LangOpts.CPlusPlus && LangOpts.CPlusPlus11) {
        // C++ 11 at_quick_exit, std::at_quick_exit
        Overload("at_quick_exit", this);
#if defined(__GLIBCXX__) && !(defined(__APPLE__) || (__GNUC__ >= 5))
        // libstdc++ mangles at_quick_exit on Linux when headers from g++ < 5
        Overload("_Z13at_quick_exitPFvvE", this);
#endif
      }

      // Add the modules and emit the symbols
      addModule(CG->ReleaseModule(), m_OptLevel, true);
      Overload.Emit(m_Executor.get()
#ifdef LLVM_ON_WIN32
                    , m_Opts.CompilerOpts.JITFormat == 0
#endif
                   );

      // Start a new module for the remaining initialization
      m_IncrParser->StartModule();
    }

    // Tell the diagnostic client that we are entering file parsing mode.
    DiagnosticConsumer& DClient = CI->getDiagnosticClient();
    DClient.BeginSourceFile(CI->getLangOpts(), &PP);

    llvm::SmallVector<IncrementalParser::ParseResultTransaction, 2>
      IncrParserTransactions;
    if (!m_IncrParser->Initialize(IncrParserTransactions, parentInterp)) {
      // Initialization is not going well, but we still have to commit what
      // we've been given. Don't clear the DiagnosticsConsumer so the caller
      // can inspect any errors that have been generated.
      for (auto&& I: IncrParserTransactions)
        m_IncrParser->commitTransaction(I, false);
      return;
    }

    Initialize(noRuntime || m_Opts.NoRuntime, parentInterp);

    // Commit the transactions, now that gCling is set up. It is needed for
    // static initialization in these transactions through __cxa_atexit.
    for (auto&& I: IncrParserTransactions)
      m_IncrParser->commitTransaction(I);

    // Sync offset so input line numbers match.
    m_IncrParser->moveLineOffset(-(m_IncrParser->getLineNumber() - 1));

    // Disable suggestions for ROOT
    bool showSuggestions = !llvm::StringRef(ClingStringify(CLING_VERSION)).startswith("ROOT");

    // We need InterpreterCallbacks only if it is a parent Interpreter.
    if (!parentInterp) {
      std::unique_ptr<InterpreterCallbacks>
         AutoLoadCB(new AutoloadCallback(this, showSuggestions));
      setCallbacks(std::move(AutoLoadCB));
    }

    m_IncrParser->SetTransformers(parentInterp);
  }

  ///\brief Constructor for the child Interpreter.
  /// Passing the parent Interpreter as an argument.
  ///
  Interpreter::Interpreter(const Interpreter &parentInterpreter, int argc,
                           const char* const *argv,
                           const char* llvmdir /*= 0*/, bool noRuntime) :
    Interpreter(argc, argv, llvmdir, noRuntime, &parentInterpreter) {
    // Do the "setup" of the connection between this interpreter and
    // its parent interpreter.
    if (CompilerInstance* CI = getCIOrNull()) {
      // The "bridge" between the interpreters.
      ExternalInterpreterSource *myExternalSource =
        new ExternalInterpreterSource(&parentInterpreter, this);

      llvm::IntrusiveRefCntPtr <ExternalASTSource>
        astContextExternalSource(myExternalSource);

      CI->getASTContext().setExternalSource(astContextExternalSource);

      // Inform the Translation Unit Decl of I2 that it has to search somewhere
      // else to find the declarations.
      CI->getASTContext().getTranslationUnitDecl()->setHasExternalVisibleStorage(true);

      // Give my IncrementalExecutor a pointer to the Incremental executor of the
      // parent Interpreter.
      m_Executor->setExternalIncrementalExecutor(parentInterpreter.m_Executor.get());
    }
  }

  Interpreter::~Interpreter() {
    // Do this first so m_StoredStates will be ignored if Interpreter::unload
    // is called later on.
    for (size_t i = 0, e = m_StoredStates.size(); i != e; ++i)
      delete m_StoredStates[i];
    m_StoredStates.clear();

#if defined(LLVM_ON_WIN32)
    m_RuntimeIntercept.reset();
#endif

    m_DyLibManager.reset();

    if (m_Executor)
      m_Executor->shuttingDown();

    if (CompilerInstance* CI = getCIOrNull())
      CI->getDiagnostics().getClient()->EndSourceFile();

    // LookupHelper's ~Parser needs the PP from IncrParser's CI, so do this
    // first:
    m_LookupHelper.reset();

    // We want to keep the callback alive during the shutdown of Sema, CodeGen
    // and the ASTContext. For that to happen we shut down the IncrementalParser
    // explicitly, before the implicit destruction (through the unique_ptr) of
    // the callbacks.
    m_IncrParser.reset(0);

    if (m_Parenting != reinterpret_cast<Interpreter**>(this))
      delete [] m_Parenting;
  }

  Interpreter* Interpreter::parent() {
    if (m_Parenting == reinterpret_cast<Interpreter**>(this))
      return nullptr;
    return m_Parenting[1];
  }

  Interpreter& Interpreter::ancestor() {
    if (m_Parenting == reinterpret_cast<Interpreter**>(this))
      return *this;
    return m_Parenting[1]->ancestor();
  }

  Transaction* Interpreter::Initialize(bool NoRuntime, const Interpreter* Pnt) {
    largestream Strm;
    const clang::LangOptions& LangOpts = getCI()->getLangOpts();
    const void* ThisP = isInSyntaxOnlyMode() ? nullptr : static_cast<void*>(this);

    // FIXME: gCling should be const so assignment is a compile time error.
    if (!NoRuntime) {
      Strm << "#include \"cling/Interpreter/RuntimeUniverse.h\"\n";
      std::pair<const char*, const char*> Close;
      if (LangOpts.CPlusPlus) {
        Strm << "namespace cling { class Interpreter; namespace runtime { "
                "Interpreter* gCling";
        Close = std::make_pair("Interpreter*", " }}\nusing namespace cling::runtime;\n");
      } else {
        Strm << "void* gCling";
        Close = std::make_pair("void*", "");
      }
      if (ThisP)
        Strm << "=(" << Close.first << ")" << ThisP;
      Strm << ";" << Close.second << "\n";
    }
    // Make all Interpreter accessible via thisCling pointer
    if (!NoRuntime || (Pnt && !m_Opts.NoRuntime)) {
      const char* InrpTy = LangOpts.CPlusPlus ? "cling::Interpreter" : "void";
      // If parent exists, then thisCling is already define to that Interpreter
      if (Pnt) Strm << "#undef thisCling\n";
      Strm << "#define thisCling ((" << InrpTy << "*)" << ThisP << ")\n";
    }

    if (m_Opts.Verbose())
      cling::errs() << Strm.str();

    Transaction *T;
    CompilationOptions CO(this, "cling_Interpreter_initialization");
    CO.CheckPointerValidity = 0;
    DeclareInternal(Strm.str(), CO, &T);
    return T;
  }

  void Interpreter::AddIncludePaths(llvm::StringRef PathStr, const char* Delm) {
    CompilerInstance* CI = getCI();
    HeaderSearchOptions& HOpts = CI->getHeaderSearchOpts();

    // Save the current number of entries
    size_t Idx = HOpts.UserEntries.size();
    utils::AddIncludePaths(PathStr, HOpts, Delm);

    Preprocessor& PP = CI->getPreprocessor();
    SourceManager& SM = PP.getSourceManager();
    FileManager& FM = SM.getFileManager();
    HeaderSearch& HSearch = PP.getHeaderSearchInfo();
    const bool isFramework = false;

    // Add all the new entries into Preprocessor
    for (const size_t N = HOpts.UserEntries.size(); Idx < N; ++Idx) {
      const HeaderSearchOptions::Entry& E = HOpts.UserEntries[Idx];
      if (const clang::DirectoryEntry *DE = FM.getDirectory(E.Path)) {
        HSearch.AddSearchPath(DirectoryLookup(DE, SrcMgr::C_User, isFramework),
                              E.Group == frontend::Angled);
      }
    }
  }

  void Interpreter::AddIncludePath(llvm::StringRef PathsStr) {
    return AddIncludePaths(PathsStr, nullptr);
  }

  void Interpreter::DumpIncludePath(llvm::raw_ostream* S) {
    utils::DumpIncludePaths(getCI()->getHeaderSearchOpts(), S ? *S : cling::outs(),
                            true /*withSystem*/, true /*withFlags*/);
  }

  // FIXME: Add stream argument and move DumpIncludePath path here.
  void Interpreter::dump(llvm::StringRef what, llvm::StringRef filter) {
    llvm::raw_ostream &where = cling::log();
    if (what.equals("asttree")) {
      std::unique_ptr<clang::ASTConsumer> printer =
          clang::CreateASTDumper(filter, true  /*DumpDecls*/,
                                         false /*Deserialize*/,
                                         false /*DumpLookups*/);
      printer->HandleTranslationUnit(getSema().getASTContext());
    } else if (what.equals("ast"))
      getSema().getASTContext().PrintStats();
    else if (what.equals("decl"))
      ClangInternalState::printLookupTables(where, getSema().getASTContext());
    else if (what.equals("undo"))
      m_IncrParser->printTransactionStructure();
  }

  void Interpreter::storeInterpreterState(const std::string& name) const {
    // This may induce deserialization
    PushTransactionRAII RAII(this);
    CodeGenerator* CG = m_IncrParser->getCodeGenerator();
    ClangInternalState* state
      = new ClangInternalState(getCI()->getASTContext(),
                               getCI()->getPreprocessor(),
                               getLastTransaction()->getModule(),
                               CG, name);
    m_StoredStates.push_back(state);
  }

  void Interpreter::compareInterpreterState(const std::string &Name) const {
    const auto Itr = std::find_if(
        m_StoredStates.begin(), m_StoredStates.end(),
        [&Name](const ClangInternalState *S) { return S->getName() == Name; });
    if (Itr == m_StoredStates.end()) {
      cling::errs() << "The store point name " << Name
                    << " does not exist."
                       "Unbalanced store / compare\n";
      return;
    }
    // This may induce deserialization
    PushTransactionRAII RAII(this);
    (*Itr)->compare(Name, m_Opts.Verbose());
  }

  void Interpreter::printIncludedFiles(llvm::raw_ostream& Out) const {
    ClangInternalState::printIncludedFiles(Out, getCI()->getSourceManager());
  }


  void Interpreter::GetIncludePaths(llvm::SmallVectorImpl<std::string>& incpaths,
                                   bool withSystem, bool withFlags) {
    utils::CopyIncludePaths(getCI()->getHeaderSearchOpts(), incpaths,
                            withSystem, withFlags);
  }

  CompilerInstance* Interpreter::getCI() const {
    return m_IncrParser->getCI();
  }

  CompilerInstance* Interpreter::getCIOrNull() const {
    return m_IncrParser ? m_IncrParser->getCI() : nullptr;
  }

  Sema& Interpreter::getSema() const {
    return getCI()->getSema();
  }

  DiagnosticsEngine& Interpreter::getDiagnostics() const {
    return getCI()->getDiagnostics();
  }

  const MacroInfo* Interpreter::getMacro(llvm::StringRef Macro) const {
    clang::Preprocessor& PP = getCI()->getPreprocessor();
    if (IdentifierInfo* II = PP.getIdentifierInfo(Macro)) {
      // If the information about this identifier is out of date, update it from
      // the external source.
      // FIXME: getIdentifierInfo will probably do this for us once we update
      // clang. If so, please remove this manual update.
      if (II->isOutOfDate())
        PP.getExternalSource()->updateOutOfDateIdentifier(*II);
      MacroDefinition MDef = PP.getMacroDefinition(II);
      MacroInfo* MI = MDef.getMacroInfo();
      return MI;
    }
    return nullptr;
  }

  std::string Interpreter::getMacroValue(llvm::StringRef Macro,
                                         const char* Trim) const {
    std::string Value;
    if (const MacroInfo* MI = getMacro(Macro)) {
      for (const clang::Token& Tok : MI->tokens()) {
        llvm::SmallString<64> Buffer;
        Macro = getCI()->getPreprocessor().getSpelling(Tok, Buffer);
        if (!Value.empty())
          Value += " ";
        Value += Trim ? Macro.trim(Trim).str() : Macro.str();
      }
    }
    return Value;
  }
  
  ///\brief Maybe transform the input line to implement cint command line
  /// semantics (declarations are global) and compile to produce a module.
  ///
  Interpreter::CompilationResult
  Interpreter::process(const std::string& input, Value* V /* = 0 */,
                       Transaction** T /* = 0 */,
                       bool disableValuePrinting /* = false*/) {
    std::string wrapReadySource = input;
    size_t wrapPoint = std::string::npos;
    if (!isRawInputEnabled())
      wrapPoint = utils::getWrapPoint(wrapReadySource, getCI()->getLangOpts());

    if (isRawInputEnabled() || wrapPoint == std::string::npos) {
      CompilationOptions CO(this);
      CO.DeclarationExtraction = 0;
      CO.ValuePrinting = 0;
      CO.ResultEvaluation = 0;
      return DeclareInternal(input, CO, T);
    }

    CompilationOptions CO(this);
    CO.DeclarationExtraction = 1;
    CO.ValuePrinting = disableValuePrinting ? CompilationOptions::VPDisabled
      : CompilationOptions::VPAuto;
    CO.ResultEvaluation = (bool)V;
    // CO.IgnorePromptDiags = 1; done by EvaluateInternal().
    CO.CheckPointerValidity = 1;
    if (EvaluateInternal(wrapReadySource, CO, V, T, wrapPoint)
                                                     == Interpreter::kFailure) {
      return Interpreter::kFailure;
    }

    return Interpreter::kSuccess;
  }

  Interpreter::CompilationResult
  Interpreter::parse(const std::string& input, Transaction** T /*=0*/) const {
    CompilationOptions CO(this);
    CO.CodeGeneration = 0;
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = 0;
    CO.ResultEvaluation = 0;

    return DeclareInternal(input, CO, T);
  }

  Interpreter::CompilationResult
  Interpreter::loadModuleForHeader(const std::string& headerFile) {
    Preprocessor& PP = getCI()->getPreprocessor();
    //Copied from clang's PPDirectives.cpp
    bool isAngled = false;
    // Clang doc says:
    // "LookupFrom is set when this is a \#include_next directive, it specifies
    // the file to start searching from."
    const DirectoryLookup* FromDir = 0;
    const clang::FileEntry* FromFile = 0;
    const DirectoryLookup* CurDir = 0;

    ModuleMap::KnownHeader suggestedModule;
    // PP::LookupFile uses it to issue 'nice' diagnostic
    SourceLocation fileNameLoc;
    PP.LookupFile(fileNameLoc, headerFile, isAngled, FromDir, FromFile, CurDir,
                  /*SearchPath*/0, /*RelativePath*/ 0, &suggestedModule,
                  0 /*IsMapped*/,
                  /*SkipCache*/false, /*OpenFile*/ false, /*CacheFail*/ false);
    if (!suggestedModule)
      return Interpreter::kFailure;

    // Copied from PPDirectives.cpp
    SmallVector<std::pair<IdentifierInfo *, SourceLocation>, 2> path;
    for (Module *mod = suggestedModule.getModule(); mod; mod = mod->Parent) {
      IdentifierInfo* II
        = &getSema().getPreprocessor().getIdentifierTable().get(mod->Name);
      path.push_back(std::make_pair(II, fileNameLoc));
    }

    std::reverse(path.begin(), path.end());

    // Pretend that the module came from an inclusion directive, so that clang
    // will create an implicit import declaration to capture it in the AST.
    bool isInclude = true;
    SourceLocation includeLoc;
    if (getCI()->loadModule(includeLoc, path, Module::AllVisible, isInclude)) {
      // After module load we need to "force" Sema to generate the code for
      // things like dynamic classes.
      getSema().ActOnEndOfTranslationUnit();
      return Interpreter::kSuccess;
    }

    return Interpreter::kFailure;
  }

  Interpreter::CompilationResult
  Interpreter::parseForModule(const std::string& input) {
    CompilationOptions CO(this);
    CO.CodeGenerationForModule = 1;
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = 0;
    CO.ResultEvaluation = 0;

    // When doing parseForModule avoid warning about the user code
    // being loaded ... we probably might as well extend this to
    // ALL warnings ... but this will suffice for now (working
    // around a real bug in QT :().
    DiagnosticsEngine& Diag = getDiagnostics();
    Diag.setSeverity(clang::diag::warn_field_is_uninit,
                     clang::diag::Severity::Ignored, SourceLocation());
    CompilationResult Result = DeclareInternal(input, CO);
    Diag.setSeverity(clang::diag::warn_field_is_uninit,
                     clang::diag::Severity::Warning, SourceLocation());
    return Result;
  }


  Interpreter::CompilationResult
  Interpreter::CodeCompleteInternal(const std::string& input, unsigned offset) {

    CompilationOptions CO(this);
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = 0;
    CO.ResultEvaluation = 0;
    CO.CheckPointerValidity = 0;

    std::string wrapped = input;
    size_t wrapPos = utils::getWrapPoint(wrapped, getCI()->getLangOpts());
    const std::string& Src = WrapInput(wrapped, wrapped, wrapPos);

    CO.CodeCompletionOffset = offset + wrapPos;

    StateDebuggerRAII stateDebugger(this);

    // This triggers the FileEntry to be created and the completion
    // point to be set in clang.
    m_IncrParser->Compile(Src, CO);

    return kSuccess;
  }

  Interpreter::CompilationResult
  Interpreter::declare(const std::string& input, Transaction** T/*=0 */) {
    CompilationOptions CO(this);
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = 0;
    CO.ResultEvaluation = 0;
    CO.CheckPointerValidity = 0;

    return DeclareInternal(input, CO, T);
  }

  Interpreter::CompilationResult
  Interpreter::evaluate(const std::string& input, Value& V) {
    // Here we might want to enforce further restrictions like: Only one
    // ExprStmt can be evaluated and etc. Such enforcement cannot happen in the
    // worker, because it is used from various places, where there is no such
    // rule
    CompilationOptions CO(this, "cling::Interpreter::evaluate");
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = 0;
    CO.ResultEvaluation = 1;

    moveLineNumber(-1);
    return EvaluateInternal(input, CO, &V);
  }

  Interpreter::CompilationResult
  Interpreter::codeComplete(const std::string& line, size_t& cursor,
                            std::vector<std::string>& completions) const {

    const char * const argV = "cling";
    std::string resourceDir = this->getCI()->getHeaderSearchOpts().ResourceDir;
    // Remove the extra 3 directory names "/lib/clang/3.9.0"
    StringRef parentResourceDir = llvm::sys::path::parent_path(
                                  llvm::sys::path::parent_path(
                                  llvm::sys::path::parent_path(resourceDir)));
    std::string llvmDir = parentResourceDir.str();

    Interpreter childInterpreter(*this, 1, &argV, llvmDir.c_str());
    if (!childInterpreter.isValid())
      return kFailure;

    auto childCI = childInterpreter.getCI();
    clang::Sema &childSemaRef = childCI->getSema();

    // Create the CodeCompleteConsumer with InterpreterCallbacks
    // from the parent interpreter and set the consumer for the child
    // interpreter.
    ClingCodeCompleteConsumer* consumer = new ClingCodeCompleteConsumer(
                getCI()->getFrontendOpts().CodeCompleteOpts, completions);
    // Child interpreter CI will own consumer!
    childCI->setCodeCompletionConsumer(consumer);
    childSemaRef.CodeCompleter = consumer;

    // Ignore diagnostics when we tab complete.
    // This is because we get redefinition errors due to the import of the decls.
    clang::IgnoringDiagConsumer* ignoringDiagConsumer =
                                            new clang::IgnoringDiagConsumer();                      
    childSemaRef.getDiagnostics().setClient(ignoringDiagConsumer, true);
    DiagnosticsEngine& parentDiagnostics = this->getCI()->getSema().getDiagnostics();

    std::unique_ptr<DiagnosticConsumer> ownerDiagConsumer = 
                                                parentDiagnostics.takeClient();
    auto clientDiagConsumer = parentDiagnostics.getClient();
    parentDiagnostics.setClient(ignoringDiagConsumer, /*owns*/ false);

    // The child will desirialize decls from *this. We need a transaction RAII.
    PushTransactionRAII RAII(this);

    // Triger the code completion.
    childInterpreter.CodeCompleteInternal(line, cursor);

    // Restore the original diagnostics client for parent interpreter.
    parentDiagnostics.setClient(clientDiagConsumer,
                                ownerDiagConsumer.release() != nullptr);
    parentDiagnostics.Reset(/*soft=*/true);

    return kSuccess;
  }

  Interpreter::CompilationResult
  Interpreter::echo(const std::string& input, Value* V /* = 0 */) {
    CompilationOptions CO(this);
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = CompilationOptions::VPEnabled;
    CO.ResultEvaluation = (bool)V;

    return EvaluateInternal(input, CO, V);
  }

  Interpreter::CompilationResult
  Interpreter::execute(const std::string& input) {
    CompilationOptions CO(this);
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = 0;
    CO.ResultEvaluation = 0;
    CO.DynamicScoping = 0;
    return EvaluateInternal(input, CO);
  }

  Interpreter::CompilationResult Interpreter::emitAllDecls(Transaction* T) {
    assert(!isInSyntaxOnlyMode() && "No CodeGenerator?");
    m_IncrParser->emitTransaction(T);
    m_IncrParser->addTransaction(T);
    T->setState(Transaction::kCollecting);
    auto PRT = m_IncrParser->endTransaction(T);
    m_IncrParser->commitTransaction(PRT);

    if ((T = PRT.getPointer()))
      if (executeTransaction(*T))
        return Interpreter::kSuccess;

    return Interpreter::kFailure;
  }

  static void makeUniqueName(llvm::raw_ostream &Strm, unsigned long long ID) {
    Strm << utils::Synthesize::UniquePrefix << ID;
  }

  static std::string makeUniqueWrapper(unsigned long long ID) {
    cling::ostrstream Strm;
    Strm << "void ";
    makeUniqueName(Strm, ID);
    Strm << "(void* vpClingValue) {\n ";
    return Strm.str();
  }

  void Interpreter::createUniqueName(std::string &Out) {
    llvm::raw_string_ostream Strm(Out);
    makeUniqueName(Strm, m_UniqueCounter++);
  }

  bool Interpreter::isUniqueName(llvm::StringRef name) {
    return name.startswith(utils::Synthesize::UniquePrefix);
  }

  clang::SourceLocation Interpreter::getSourceLocation(bool skipWrapper) const {
    const Transaction* T = getLatestTransaction();
    if (!T)
      return SourceLocation();

    const SourceManager &SM = getCI()->getSourceManager();
    if (skipWrapper) {
      return T->getSourceStart(SM).getLocWithOffset(
          makeUniqueWrapper(m_UniqueCounter).size());
    }
    return T->getSourceStart(SM);
  }

  const std::string& Interpreter::WrapInput(const std::string& Input,
                                            std::string& Output,
                                            size_t& WrapPoint) const {
    // If wrapPoint is > length of input, nothing is wrapped!
    if (WrapPoint < Input.size()) {
      const std::string Header = makeUniqueWrapper(m_UniqueCounter++);

      // Suppport Input and Output begin the same string
      std::string Wrapper = Input.substr(WrapPoint);
      Wrapper.insert(0, Header);
      Wrapper.append("\n;\n}");
      Wrapper.insert(0, Input.substr(0, WrapPoint));
      Wrapper.swap(Output);
      WrapPoint += Header.size();
      return Output;
    }
    // in-case std::string::npos was passed
    WrapPoint = 0;
    return Input;
  }

  bool Interpreter::isObjectiveC() const {
    // Should this be cached?
    const LangOptions& Opts = getCI()->getLangOpts();
    return Opts.ObjC2 || Opts.ObjC1;
  }

  Interpreter::ExecutionResult
  Interpreter::RunFunction(const FunctionDecl* FD, Value* res /*=0*/) {
    if (getDiagnostics().hasErrorOccurred())
      return kExeCompilationError;

    if (isInSyntaxOnlyMode()) {
      return kExeNoCodeGen;
    }

    if (!FD)
      return kExeUnkownFunction;

    std::string mangledNameIfNeeded;
    utils::Analyze::maybeMangleDeclName(FD, mangledNameIfNeeded);
    IncrementalExecutor::ExecutionResult ExeRes =
       m_Executor->executeWrapper(mangledNameIfNeeded, res);
    return ConvertExecutionResult(ExeRes);
  }

  static const clang::FunctionDecl* IsNamedFunction(StringRef Name, Decl* D) {
    if (const FunctionDecl* FD = dyn_cast<FunctionDecl>(D)) {
      const IdentifierInfo* II = FD->getDeclName().getAsIdentifierInfo();
      if (II && II->getName() == Name)
        return FD;
    }
    return nullptr;
  }

  template <class CtxType>
  static const clang::FunctionDecl* FindFunction(StringRef Name, Decl* D) {
    while (const CtxType* CtxDecl = dyn_cast<CtxType>(D)) {
      DeclContext::decl_iterator DeclBegin = CtxDecl->decls_begin();
      if (DeclBegin == CtxDecl->decls_end())
        return nullptr;
      D = *DeclBegin;
      if (const FunctionDecl* FD = IsNamedFunction(Name, D))
        return FD;
    }
    return nullptr;
  }

  const FunctionDecl* Interpreter::DeclareFunction(StringRef name,
                                                   StringRef code,
                                                   bool withAccessControl,
                                                   bool ExtrnC) {
    /*
    In CallFunc we currently always (intentionally and somewhat necessarily)
    always fully specify member function template, however this can lead to
    an ambiguity with a class template.  For example in
    roottest/cling/functionTemplate we get:

    input_line_171:3:15: warning: lookup of 'set' in member access expression
    is ambiguous; using member of 't'
    ((t*)obj)->set<int>(*(int*)args[0]);
               ^
    roottest/cling/functionTemplate/t.h:19:9: note: lookup in the object type
    't' refers here
    void set(T targ) {
         ^
    /usr/include/c++/4.4.5/bits/stl_set.h:87:11: note: lookup from the
    current scope refers here
    class set
          ^
    This is an intention warning implemented in clang, see
    http://llvm.org/viewvc/llvm-project?view=revision&revision=105518

    which 'should have been' an error:

    C++ [basic.lookup.classref] requires this to be an error, but,
    because it's hard to work around, Clang downgrades it to a warning as
    an extension.</p>

    // C++98 [basic.lookup.classref]p1:
    // In a class member access expression (5.2.5), if the . or -> token is
    // immediately followed by an identifier followed by a <, the identifier
    // must be looked up to determine whether the < is the beginning of a
    // template argument list (14.2) or a less-than operator. The identifier
    // is first looked up in the class of the object expression. If the
    // identifier is not found, it is then looked up in the context of the
    // entire postfix-expression and shall name a class or function template. If
    // the lookup in the class of the object expression finds a template, the
    // name is also looked up in the context of the entire postfix-expression
    // and
    // -- if the name is not found, the name found in the class of the
    // object expression is used, otherwise
    // -- if the name is found in the context of the entire postfix-expression
    // and does not name a class template, the name found in the class of the
    // object expression is used, otherwise
    // -- if the name found is a class template, it must refer to the same
    // entity as the one found in the class of the object expression,
    // otherwise the program is ill-formed.

    See -Wambiguous-member-template

    An alternative to disabling the diagnostics is to use a pointer to
    member function:

    #include <set>
    using namespace std;

    extern "C" int printf(const char*,...);

    struct S {
    template <typename T>
    void set(T) {};

    virtual void virtua() { printf("S\n"); }
    };

    struct T: public S {
    void virtua() { printf("T\n"); }
    };

    int main() {
    S *s = new T();
    typedef void (S::*Func_p)(int);
    Func_p p = &S::set<int>;
    (s->*p)(12);

    typedef void (S::*Vunc_p)(void);
    Vunc_p q = &S::virtua;
    (s->*q)(); // prints "T"
    return 0;
    }
    */
    DiagnosticsEngine& Diag = getDiagnostics();
    Diag.setSeverity(clang::diag::ext_nested_name_member_ref_lookup_ambiguous,
                     clang::diag::Severity::Ignored, SourceLocation());


    LangOptions& LO = const_cast<LangOptions&>(getCI()->getLangOpts());
    bool savedAccessControl = LO.AccessControl;
    LO.AccessControl = withAccessControl;
    cling::Transaction* T = 0;
    cling::Interpreter::CompilationResult CR = declare(code, &T);
    LO.AccessControl = savedAccessControl;

    Diag.setSeverity(clang::diag::ext_nested_name_member_ref_lookup_ambiguous,
                     clang::diag::Severity::Warning, SourceLocation());

    if (CR != cling::Interpreter::kSuccess)
      return 0;

    if (!ExtrnC) {
      code = code.ltrim();
      if (code.startswith("extern ")) {
        code = code.ltrim("extern ");
        if (code.startswith("\"C\""))
          ExtrnC = true;
      }
    }

    for (auto&& DCI : T->decls()) {
      if (DCI.m_Call == cling::Transaction::kCCIHandleTopLevelDecl) {
        Decl* D = *DCI.m_DGR.begin();
        const FunctionDecl* FD = IsNamedFunction(name, D);
        if (FD && (ExtrnC ? FD->getLanguageLinkage() == CLanguageLinkage : true))
          return FD;
        FD = ExtrnC ? FindFunction<LinkageSpecDecl>(name, D)
                    : FindFunction<NamespaceDecl>(name, D);
        if (FD)
          return FD;
      }
    }
    return 0;
  }

  void*
  Interpreter::compileFunction(llvm::StringRef name, llvm::StringRef code,
                               bool ifUnique, bool accessCtrl, bool externC) {
    //
    //  Compile the wrapper code.
    //

    if (isInSyntaxOnlyMode())
      return 0;

    if (ifUnique) {
      if (void* Addr = (void*)getAddressOfGlobal(name)) {
        return Addr;
      }
    }

    const FunctionDecl* FD = DeclareFunction(name, code, accessCtrl, externC);
    if (!FD)
      return 0;
    //
    //  Get the wrapper function pointer
    //  from the ExecutionEngine (the JIT).
    //
    if (const llvm::GlobalValue* GV
        = getLastTransaction()->getModule()->getNamedValue(name))
      return m_Executor->getPointerToGlobalFromJIT(*GV);

    if (!externC) {
      std::string mangledName;
      utils::Analyze::maybeMangleDeclName(FD, mangledName);
      if (!mangledName.empty()) {
        if (const llvm::GlobalValue* GV =
                getLastTransaction()->getModule()->getNamedValue(mangledName))
          return m_Executor->getPointerToGlobalFromJIT(*GV);
      }
    }
    return 0;
  }

  void*
  Interpreter::compileDtorCallFor(const clang::RecordDecl* RD) {
    if (!getSema().getLangOpts().CPlusPlus)
      return nullptr;

    void* &addr = m_DtorWrappers[RD];
    if (addr)
      return addr;

    if (const CXXRecordDecl *CXX = dyn_cast<CXXRecordDecl>(RD)) {
      // Don't generate a stub for a destructor that does nothing
      // This also fixes printing of lambdas and C structures as they
      // have no dtor test/ValuePrinter/Destruction.C
      if (CXX->hasIrrelevantDestructor())
        return nullptr;
    }

    smallstream funcname;
    funcname << "__cling_Destruct_" << RD;

    largestream code;
    code << "extern \"C\" void " << funcname.str() << "(void* obj){(("
         << utils::TypeName::GetFullyQualifiedName(
                clang::QualType(RD->getTypeForDecl(), 0), RD->getASTContext())
         << "*)obj)->~" << RD->getNameAsString() << "();}";

    // ifUniq = false: we know it's unique, no need to check.
    addr = compileFunction(funcname.str(), code.str(), false /*ifUniq*/,
                           false /*withAccessControl*/);
    return addr;
  }

  Interpreter::CompilationResult
  Interpreter::DeclareInternal(const std::string& input,
                               const CompilationOptions& CO,
                               Transaction** T /* = 0 */) const {
    assert(CO.DeclarationExtraction == 0
           && CO.ValuePrinting == 0
           && CO.ResultEvaluation == 0
           && "Compilation Options not compatible with \"declare\" mode.");

    StateDebuggerRAII stateDebugger(this);

    IncrementalParser::ParseResultTransaction PRT
      = m_IncrParser->Compile(input, CO);
    if (PRT.getInt() == IncrementalParser::kFailed)
      return Interpreter::kFailure;

    if (T)
      *T = PRT.getPointer();
    return Interpreter::kSuccess;
  }

  Interpreter::CompilationResult
  Interpreter::EvaluateInternal(const std::string& input,
                                CompilationOptions CO,
                                Value* V, /* = 0 */
                                Transaction** T /* = 0 */,
                                size_t wrapPoint /* = 0*/) {
    StateDebuggerRAII stateDebugger(this);

    // Wrap the expression
    std::string WrapperBuffer;
    const std::string& Wrapper = WrapInput(input, WrapperBuffer, wrapPoint);

    // We have wrapped and need to disable warnings that are caused by
    // non-default C++ at the prompt:
    CO.IgnorePromptDiags = 1;

    IncrementalParser::ParseResultTransaction PRT
      = m_IncrParser->Compile(Wrapper, CO);
    Transaction* lastT = PRT.getPointer();
    if (lastT && lastT->getState() != Transaction::kCommitted) {
      assert((lastT->getState() == Transaction::kCommitted
              || lastT->getState() == Transaction::kRolledBack
              || lastT->getState() == Transaction::kRolledBackWithErrors)
             && "Not committed?");
      if (V)
        *V = Value();
      return kFailure;
    }

    // Might not have a Transaction
    if (PRT.getInt() == IncrementalParser::kFailed) {
      if (V)
        *V = Value();
      return kFailure;
    }

    if (!lastT) {
      // Empty transactions are good, too!
      if (V)
        *V = Value();
      return kSuccess;
    }

    const Transaction *prntT = m_CachedTrns[kPrintValueTransaction];

    Value resultV;
    if (!V)
      V = &resultV;
    if (!lastT->getWrapperFD()) // no wrapper to run
      return Interpreter::kSuccess;
    else {
      ExecutionResult rslt = RunFunction(lastT->getWrapperFD(), V);

      // If print value Transaction has changed value, then lastT is now
      // the transaction that holds the transaction(s) of loading up the
      // value printer and must be recorded as such.
      if ( prntT != m_CachedTrns[kPrintValueTransaction] ) {
        assert(m_CachedTrns[kPrintValueTransaction] != nullptr
               && "Value printer failed to load after success?");
        // As the stack is unwinded from recursive calls to EvaluateInternal
        // we merge the subsequent transactions into the current one.
        // Then m_CachedTrns[kPrintValue] is marked as this transaction as it
        // is the first known Transaction that caused the printer to be loaded.
        m_IncrParser->mergeTransactionsAfter(lastT, true);
        m_CachedTrns[kPrintValueTransaction] = lastT;
      }

      if ( rslt < kExeFirstError) {
        if (lastT->getCompilationOpts().ValuePrinting
            != CompilationOptions::VPDisabled
            && V->isValid()
            // the !V->needsManagedAllocation() case is handled by
            // dumpIfNoStorage.
            && V->needsManagedAllocation())
          V->dump();
        return Interpreter::kSuccess;
      }
    }
    return Interpreter::kSuccess;
  }

  FileEntry Interpreter::lookupFileOrLibrary(FileEntry file) {
    if (file.resolved())
      return file;

    std::string canonicalFile =
                         DynamicLibraryManager::normalizePath(file.mNameOrPath);
    if (canonicalFile.empty())
      canonicalFile = std::move(file.mNameOrPath);

    //Copied from clang's PPDirectives.cpp
    bool isAngled = false;
    // Clang doc says:
    // "LookupFrom is set when this is a \#include_next directive, it
    // specifies the file to start searching from."
    const DirectoryLookup* FromDir = 0;
    const clang::FileEntry* FromFile = 0;
    const DirectoryLookup* CurDir = 0;
    Preprocessor& PP = getCI()->getPreprocessor();
    // PP::LookupFile uses it to issue 'nice' diagnostic
    SourceLocation fileNameLoc;
    const clang::FileEntry* FE = PP.LookupFile(fileNameLoc, canonicalFile,
                       isAngled, FromDir, FromFile,
                       CurDir, /*SearchPath*/0, /*RelativePath*/ 0,
                       /*suggestedModule*/0, 0 /*IsMapped*/, /*SkipCache*/false,
                       /*OpenFile*/ false, /*CacheFail*/ false);
    if (FE && FE->isValid()) {
      // Just because it's valid in the cache, doesn't mean it really is
      // ###TODO: We can avoid re-stating a file when it's actually not from
      // cache by comparing against FileManager::NextFileUID
      vfs::Status Stat;
      FileManager& FM = PP.getSourceManager().getFileManager();
      if (!FM.getNoncachedStatValue(FE->getName(), Stat)) {
        if (Stat.isRegularFile()||Stat.isSymlink()) {
          SmallString<512> path(FE->getName());
          FM.makeAbsolutePath(path);
          return FileEntry(FE, path.c_str());
        }
      }
      else
        FM.invalidateCache(const_cast<clang::FileEntry*>(FE));
    }
    return getDynamicLibraryManager()->lookupLibrary(std::move(canonicalFile));
  }

  Interpreter::CompilationResult
  Interpreter::loadLibrary( FileEntry fileObj, bool permant) {
    FileEntry file = lookupFileOrLibrary(std::move(fileObj));
    if (!file.exists())
      return kMoreInputExpected;
    if (!file.isLibrary())
      return kFailure;

    switch (getDynamicLibraryManager()->loadLibrary(std::move(file), permant)) {
      case DynamicLibraryManager::kLoadLibSuccess: // Intentional fall through
      case DynamicLibraryManager::kLoadLibAlreadyLoaded:
        return kSuccess;
      case DynamicLibraryManager::kLoadLibNotFound:
        assert(0 && "Cannot find library with existing canonical name!");
      default:
        break;
    }
    return kFailure;
  }

  Interpreter::CompilationResult
  Interpreter::loadHeader( FileEntry fileObj, Transaction** T /*= 0*/) {
    FileEntry file = lookupFileOrLibrary(std::move(fileObj));
    if (!file.exists()) {
      getSema().Diag(getSourceLocation(), clang::diag::err_pp_file_not_found)
        << file.name();
      return kFailure;
    }

    std::string code("#include \"");
    code.append(file.filePath());
    code.append("\"");

    CompilationOptions CO(this);
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = 0;
    CO.ResultEvaluation = 0;
    CO.CheckPointerValidity = 1;
    return DeclareInternal(code, CO, T);
  }

  Interpreter::CompilationResult
  Interpreter::loadFile(FileEntry fileObj, bool allowSharedLib /*=true*/,
                        Transaction** T /*= 0*/) {
    FileEntry file = lookupFileOrLibrary(std::move(fileObj));
    if (file.isLibrary()) {
      if (allowSharedLib)
        return loadLibrary(std::move(file));
      return kFailure;
    }

    return loadHeader(std::move(file), T);
  }

  void Interpreter::unload(Transaction& T) {
    // Clear any stored states that reference the llvm::Module.
    // Do it first in case
    llvm::Module* const Module = T.getModule();
    if (Module && !m_StoredStates.empty()) {
      const auto Predicate = [&Module](const ClangInternalState *S) {
        return S->getModule() == Module;
      };
      auto Itr =
          std::find_if(m_StoredStates.begin(), m_StoredStates.end(), Predicate);
      while (Itr != m_StoredStates.end()) {
        if (m_Opts.Verbose()) {
          cling::errs() << "Unloading Transaction forced state '"
                        << (*Itr)->getName() << "' to be destroyed\n";
        }
        m_StoredStates.erase(Itr);

        Itr = std::find_if(m_StoredStates.begin(), m_StoredStates.end(),
                           Predicate);
      }
    }

    // Clear any cached transaction states.
    for (unsigned i = 0; i < kNumTransactions; ++i) {
      if (m_CachedTrns[i] == &T) {
        m_CachedTrns[i] = nullptr;
        break;
      }
    }

    if (InterpreterCallbacks* callbacks = getCallbacks())
      callbacks->TransactionUnloaded(T);
    if (m_Executor) { // we also might be in fsyntax-only mode.
      m_Executor->runAndRemoveStaticDestructors(&T);
      if (!T.getExecutor()) {
        // this transaction might be queued in the executor
        m_Executor->unloadFromJIT(Module,
                                  Transaction::ExeUnloadHandle({(void*)(size_t)-1}));
      }
    }

    // We can revert the most recent transaction or a nested transaction of a
    // transaction that is not in the middle of the transaction collection
    // (i.e. at the end or not yet added to the collection at all).
    assert(!T.getTopmostParent()->getNext() &&
           "Can not revert previous transactions");
    assert((T.getState() != Transaction::kRolledBack ||
            T.getState() != Transaction::kRolledBackWithErrors) &&
           "Transaction already rolled back.");
    if (getOptions().ErrorOut)
      return;

    if (InterpreterCallbacks* callbacks = getCallbacks())
      callbacks->TransactionRollback(T);

    TransactionUnloader U(this, &getCI()->getSema(),
                          m_IncrParser->getCodeGenerator(),
                          m_Executor.get());
    if (U.RevertTransaction(&T))
      T.setState(Transaction::kRolledBack);
    else
      T.setState(Transaction::kRolledBackWithErrors);

    m_IncrParser->deregisterTransaction(T);
  }

  void Interpreter::unload(unsigned numberOfTransactions) {
    const Transaction *First = m_IncrParser->getFirstTransaction();
    if (!First) {
      cling::errs() << "cling: No transactions to unload!";
      return;
    }
    for (unsigned i = 0; i < numberOfTransactions; ++i) {
      cling::Transaction* T = m_IncrParser->getLastTransaction();
      if (T == First) {
        cling::errs() << "cling: Can't unload first transaction!  Unloaded "
                      << i << " of " << numberOfTransactions << "\n";
        return;
      }
      // Mark Interpreter as not having loaded 'RuntimePrintValue.h'
      if (T == m_CachedTrns[kPrintValueTransaction])
        m_CachedTrns[kPrintValueTransaction] = nullptr;

      unload(*T);
    }
  }

  static void runAndRemoveStaticDestructorsImpl(IncrementalExecutor &executor,
                                std::vector<const Transaction*> &transactions,
                                         unsigned int begin, unsigned int end) {

    for(auto i = begin; i != end; --i) {
      if (transactions[i-1] != nullptr)
        executor.runAndRemoveStaticDestructors(const_cast<Transaction*>(transactions[i-1]));
    }
  }

  void Interpreter::runAndRemoveStaticDestructors(unsigned numberOfTransactions) {
    if (!m_Executor)
      return;
    auto transactions( m_IncrParser->getAllTransactions() );
    unsigned int min = 0;
    if (transactions.size() > numberOfTransactions) {
      min = transactions.size() - numberOfTransactions;
    }
    runAndRemoveStaticDestructorsImpl(*m_Executor, transactions,
                                      transactions.size(), min);
  }

  void Interpreter::runAndRemoveStaticDestructors() {
    if (!m_Executor)
      return;
    auto transactions( m_IncrParser->getAllTransactions() );
    runAndRemoveStaticDestructorsImpl(*m_Executor, transactions,
                                      transactions.size(), 0);
  }

  void Interpreter::installLazyFunctionCreator(void* (*fp)(const std::string&)) {
    if (m_Executor)
      m_Executor->installLazyFunctionCreator(fp);
  }

  Value Interpreter::Evaluate(const char* expr, DeclContext* DC,
                                       bool ValuePrinterReq) {
    Sema& TheSema = getCI()->getSema();
    // The evaluation should happen on the global scope, because of the wrapper
    // that is created.
    //
    // We can't PushDeclContext, because we don't have scope.
    Sema::ContextRAII pushDC(TheSema,
                             TheSema.getASTContext().getTranslationUnitDecl());

    Value Result;
    getCallbacks()->SetIsRuntime(true);
    if (ValuePrinterReq)
      echo(expr, &Result);
    else
      evaluate(expr, Result);
    getCallbacks()->SetIsRuntime(false);

    return Result;
  }

  void Interpreter::setCallbacks(std::unique_ptr<InterpreterCallbacks> C) {
    // We need it to enable LookupObject callback.
    if (!m_Callbacks) {
      m_Callbacks.reset(new MultiplexInterpreterCallbacks(this));
      // FIXME: Move to the InterpreterCallbacks.cpp;
      if (DynamicLibraryManager* DLM = getDynamicLibraryManager())
        DLM->setCallbacks(m_Callbacks.get());
    }

    static_cast<MultiplexInterpreterCallbacks*>(m_Callbacks.get())
      ->addCallback(std::move(C));
  }

  const Transaction* Interpreter::getFirstTransaction() const {
    return m_IncrParser->getFirstTransaction();
  }

  const Transaction* Interpreter::getLastTransaction() const {
    return m_IncrParser->getLastTransaction();
  }

  const Transaction* Interpreter::getCurrentTransaction() const {
    return m_IncrParser->getCurrentTransaction();
  }

  const Transaction* Interpreter::getLatestTransaction() const {
    if (const Transaction* T = m_IncrParser->getCurrentTransaction())
      return T;
    return m_IncrParser->getLastTransaction();
  }

  void Interpreter::enableDynamicLookup(bool value /*=true*/) {
    if (!m_DynamicLookupDeclared && value) {
      // No dynlookup for the dynlookup header!
      m_DynamicLookupEnabled = false;
      if (loadModuleForHeader("cling/Interpreter/DynamicLookupRuntimeUniverse.h")
          != kSuccess)
      declare("#include \"cling/Interpreter/DynamicLookupRuntimeUniverse.h\"");
    }
    m_DynamicLookupDeclared = true;

    // Enable it *after* parsing the headers.
    m_DynamicLookupEnabled = value;
  }

  Interpreter::ExecutionResult
  Interpreter::executeTransaction(Transaction& T) {
    assert(!isInSyntaxOnlyMode() && "Running on what?");
    assert(T.getState() == Transaction::kCommitted && "Must be committed");

    llvm::Module* const M = T.getModule();
    if (!M)
      return Interpreter::kExeNoModule;

    IncrementalExecutor::ExecutionResult ExeRes
       = IncrementalExecutor::kExeSuccess;
    if (!isPracticallyEmptyModule(M)) {
      T.setExeUnloadHandle(m_Executor.get(), m_Executor->emitToJIT());

      // Forward to IncrementalExecutor; should not be called by
      // anyone except for IncrementalParser.
      ExeRes = m_Executor->runStaticInitializersOnce(T);
    }

    return ConvertExecutionResult(ExeRes);
  }

  bool Interpreter::addSymbol(const char* symbolName,  void* symbolAddress) {
    // Forward to IncrementalExecutor;
    if (!symbolName || !symbolAddress )
      return false;

    return m_Executor->addSymbol(symbolName, symbolAddress);
  }

  void Interpreter::addModule(llvm::Module* module, int OptLevel, bool Emit) {
    m_Executor->addModule(module, OptLevel);
    if (Emit)
      m_Executor->emitToJIT();
  }


  void* Interpreter::getAddressOfGlobal(const GlobalDecl& GD,
                                        bool* fromJIT /*=0*/) const {
    // Return a symbol's address, and whether it was jitted.
    std::string mangledName;
    utils::Analyze::maybeMangleDeclName(GD, mangledName);
    return getAddressOfGlobal(mangledName, fromJIT);
  }

  void* Interpreter::getAddressOfGlobal(llvm::StringRef SymName,
                                        bool* fromJIT /*=0*/) const {
    // Return a symbol's address, and whether it was jitted.
    if (isInSyntaxOnlyMode())
      return 0;
    return m_Executor->getAddressOfGlobal(SymName, fromJIT);
  }

  void Interpreter::AddAtExitFunc(void (*Func) (void*), void* Arg) {
    m_Executor->AddAtExitFunc(Func, Arg, getLatestTransaction()->getModule());
  }

  void Interpreter::GenerateAutoloadingMap(llvm::StringRef inFile,
                                           llvm::StringRef outFile,
                                           bool enableMacros,
                                           bool enableLogs) {

    const char *const dummy="cling_fwd_declarator";
    // Create an interpreter without any runtime, producing the fwd decls.
    // FIXME: CIFactory appends extra 3 folders to the llvmdir.
    std::string llvmdir
      = getCI()->getHeaderSearchOpts().ResourceDir + "/../../../";
    cling::Interpreter fwdGen(1, &dummy, llvmdir.c_str(), true);

    // Copy the same header search options to the new instance.
    Preprocessor& fwdGenPP = fwdGen.getCI()->getPreprocessor();
    HeaderSearchOptions headerOpts = getCI()->getHeaderSearchOpts();
    clang::ApplyHeaderSearchOptions(fwdGenPP.getHeaderSearchInfo(), headerOpts,
                                    fwdGenPP.getLangOpts(),
                                    fwdGenPP.getTargetInfo().getTriple());


    CompilationOptions CO(this);
    CO.DeclarationExtraction = 0;
    CO.ValuePrinting = 0;
    CO.ResultEvaluation = 0;
    CO.DynamicScoping = 0;


    std::string includeFile = std::string("#include \"") + inFile.str() + "\"";
    IncrementalParser::ParseResultTransaction PRT
      = fwdGen.m_IncrParser->Compile(includeFile, CO);
    cling::Transaction* T = PRT.getPointer();

    // If this was already #included we will get a T == 0.
    if (PRT.getInt() == IncrementalParser::kFailed || !T)
      return;

    std::error_code EC;
    llvm::raw_fd_ostream out(outFile.data(), EC,
                             llvm::sys::fs::OpenFlags::F_None);
    llvm::raw_fd_ostream log((outFile + ".skipped").str().c_str(),
                             EC, llvm::sys::fs::OpenFlags::F_None);
    log << "Generated for :" << inFile << "\n";
    forwardDeclare(*T, fwdGenPP, fwdGen.getCI()->getSema().getASTContext(),
                   out, enableMacros,
                   &log);
  }

  void Interpreter::forwardDeclare(Transaction& T, Preprocessor& P,
                                   clang::ASTContext& Ctx,
                                   llvm::raw_ostream& out,
                                   bool enableMacros /*=false*/,
                                   llvm::raw_ostream* logs /*=0*/,
                                   IgnoreFilesFunc_t ignoreFiles /*= return always false*/) const {
    llvm::raw_null_ostream null;
    if (!logs)
      logs = &null;

    ForwardDeclPrinter visitor(out, *logs, P, Ctx, T, 0, false, ignoreFiles);
    visitor.printStats();

    // Avoid assertion in the ~IncrementalParser.
    T.setState(Transaction::kCommitted);
  }

  namespace runtime {
    namespace internal {
      Value EvaluateDynamicExpression(Interpreter* interp, DynamicExprInfo* DEI,
                                      clang::DeclContext* DC) {
        return interp->Evaluate(DEI->getExpr(), DC,
                                DEI->isValuePrinterRequested());
      }
    } // namespace internal
  }  // namespace runtime

  size_t Interpreter::moveLineNumber(int Offset) {
    return m_IncrParser->moveLineOffset(Offset);
  }

} //end namespace cling
