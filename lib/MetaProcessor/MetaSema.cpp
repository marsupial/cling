//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vvasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "MetaSema.h"

#include "Display.h"

#include "cling/Interpreter/DynamicLibraryManager.h"
#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Interpreter/Value.h"
#include "cling/MetaProcessor/MetaProcessor.h"
#include "cling/Utils/Output.h"

#include "clang/AST/ASTContext.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CodeGenOptions.h"
#include "clang/Sema/Sema.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "clang/Lex/LexDiagnostic.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/Support/Casting.h"


#include "clang/Lex/Preprocessor.h"
#include "clang/Basic/SourceManager.h"

#include <cstdlib>
#include <iostream>

namespace cling {

  MetaSema::MetaSema(Interpreter& interp, MetaProcessor& meta)
    : m_Interpreter(interp), m_MetaProcessor(meta), m_IsQuitRequested(false) { }

  MetaSema::ActionResult MetaSema::actOnLCommand(llvm::StringRef file,
                                             Transaction** transaction /*= 0*/){
    FileEntry fe = m_Interpreter.lookupFileOrLibrary(file);
    ActionResult result = actOnUCommand(file, fe, false);
    if (result != AR_Success)
      return result;

    // In case of libraries we get .L lib.so, which might automatically pull in
    // decls (from header files). Thus we want to take the restore point before
    // loading of the file and revert exclusively if needed.
    const Transaction* unloadPoint = m_Interpreter.getLastTransaction();
     // fprintf(stderr,"DEBUG: Load for %s unloadPoint is %p\n",file.str().c_str(),unloadPoint);
    // TODO: extra checks. Eg if the path is readable, if the file exists...

    //if (fe.empty())
    //  canFile = file;

    if (m_Interpreter.loadFile(fe, true /*allowSharedLib*/, transaction)
        == Interpreter::kSuccess) {
      registerUnloadPoint(unloadPoint, std::move(fe));
      return AR_Success;
    }
    return AR_Failure;
  }

  MetaSema::ActionResult MetaSema::actOnOCommand(int optLevel) {
    if (optLevel >= 0 && optLevel < 4) {
      m_Interpreter.setDefaultOptLevel(optLevel);
      return AR_Success;
    }
    m_MetaProcessor.getOuts()
      << "Refusing to set invalid cling optimization level "
      << optLevel << '\n';
    return AR_Failure;
  }

  void MetaSema::actOnOCommand() {
    m_MetaProcessor.getOuts() << "Current cling optimization level: "
                              << m_Interpreter.getDefaultOptLevel() << '\n';
  }

  MetaSema::ActionResult MetaSema::actOnTCommand(llvm::StringRef inputFile,
                                                 llvm::StringRef outputFile) {
    m_Interpreter.GenerateAutoloadingMap(inputFile, outputFile);
    return AR_Success;
  }

  MetaSema::ActionResult MetaSema::actOnRedirectCommand(llvm::StringRef file,
                         MetaProcessor::RedirectionScope stream,
                         bool append) {

    m_MetaProcessor.setStdStream(file, stream, append);
    return AR_Success;
  }

  void MetaSema::actOnComment(llvm::StringRef comment) const {
    // Some of the comments are meaningful for the cling::Interpreter
    m_Interpreter.declare(comment);
  }

  namespace {
    /// Replace non-identifier chars by '_'
    std::string normalizeDotXFuncName(const std::string& FuncName) {
      std::string ret = FuncName;
      // Prepend '_' if name starts with a digit.
      if (ret[0] >= '0' && ret[0] <= '9')
        ret.insert(ret.begin(), '_');
      for (char& c: ret) {
        // Instead of "escaping" all non-C++-id chars, only escape those that
        // are fairly certainly file names, to keep helpful error messages for
        // broken quoting or parsing. Example:
        // "Cannot find '_func_1___'" is much less helpful than
        // "Cannot find '/func(1)*&'"
        // I.e. find a compromise between helpful diagnostics and common file
        // name (stem) ingredients.
        if (c == '+' || c == '-' || c == '=' || c == '.' || c == ' '
            || c == '@')
          c = '_';
      }
      return ret;
    }
  }

  MetaSema::ActionResult MetaSema::actOnxCommand(llvm::StringRef file,
                                                 llvm::StringRef args,
                                                 Value* result) {

    // Check if there is a function named after the file.
    assert(!args.empty() && "Arguments must be provided (at least \"()\"");
    cling::Transaction* T = 0;
    MetaSema::ActionResult actionResult = actOnLCommand(file, &T);
    // T can be nullptr if there is no code (but comments)
    if (actionResult == AR_Success && T) {
      std::string expression;
      std::string FuncName = llvm::sys::path::stem(file);
      if (!FuncName.empty()) {
        FuncName = normalizeDotXFuncName(FuncName);
        if (T->containsNamedDecl(FuncName)) {
          expression = FuncName + args.str();
          // Give the user some context in case we have a problem invoking
          expression += " /* invoking function corresponding to '.x' */";

          // Above transaction might have set a different OptLevel; use that.
          int prevOptLevel = m_Interpreter.getDefaultOptLevel();
          m_Interpreter.setDefaultOptLevel(T->getCompilationOpts().OptLevel);
          if (m_Interpreter.echo(expression, result) != Interpreter::kSuccess)
            actionResult = AR_Failure;
          m_Interpreter.setDefaultOptLevel(prevOptLevel);
        }
      } else
        FuncName = file; // Not great, but pass the diagnostics below something

      if (expression.empty()) {
        using namespace clang;
        DiagnosticsEngine& Diags = m_Interpreter.getDiagnostics();
        unsigned diagID
          = Diags.getCustomDiagID (DiagnosticsEngine::Level::Warning,
                                   "cannot find function '%0()'; falling back to .L");
        //FIXME: Figure out how to pass in proper source locations, which we can
        // use with -verify.
        Diags.Report(SourceLocation(), diagID) << FuncName;
        return AR_Success;
      }
    }
    return actionResult;
  }

  void MetaSema::actOnqCommand() {
    m_IsQuitRequested = true;
  }

  void MetaSema::actOnAtCommand() {
    m_MetaProcessor.cancelContinuation();
  }

  MetaSema::ActionResult MetaSema::actOnUndoCommand(unsigned N/*=1*/) {
    m_Interpreter.unload(N);
    return AR_Success;
  }

  MetaSema::ActionResult MetaSema::actOnUCommand(const llvm::StringRef &file,
                                                 FileEntry fe, bool canFail) {
    if (!m_Watermarks.get())
      return canFail ? AR_Failure : AR_Success;

    const clang::FileEntry* Entry = fe;
    clang::FileManager& FM = m_Interpreter.get<clang::FileManager>();
    if (!Entry) {
      if (!(Entry = FM.getFile(fe.name(), false, false)))
        return AR_Failure;
    }

    auto Pos = m_Watermarks->find(Entry);
    if (Pos == m_Watermarks->end())
      return canFail ? AR_Failure : AR_Success;

    // Search for the transaction, i.e. verify that is has not already
    // been unloaded ; This can be removed once all transaction unload
    // properly information MetaSema that it has been unloaded.
    const Transaction* unloadPoint = nullptr;
    for (const Transaction* T = m_Interpreter.getFirstTransaction(); T != 0;
         T = T->getNext()) {
      if (T == Pos->second) {
        unloadPoint = T;
        break;
      }
    }
    llvm::SmallString<512> Buf;
    DynamicLibraryManager* DLM = m_Interpreter.getDynamicLibraryManager();
    if (unloadPoint) {
      while (m_Interpreter.getLastTransaction() != unloadPoint) {
        auto Unloaded =
            m_Watermarks->rfind_value(m_Interpreter.getLastTransaction());
        if (Unloaded.first != m_Watermarks->end()) {
          llvm::StringRef Path = Unloaded.first->first->tryGetRealPathName();
          if (Path.empty()) {
            Buf = Unloaded.first->first->getName();
            FM.makeAbsolutePath(Buf);
            Path = Buf;
          }
          m_Watermarks->erase(Unloaded);
          DLM->unloadLibrary(Path);
        }
        m_Interpreter.unload(/*numberOfTransactions*/ 1);
      }
    } else {
      cling::errs() << ">>> ERROR: Transaction for file: '" << file
                    << "' has already been unloaded\n";
    }
    DLM->unloadLibrary(std::move(fe));
    m_Watermarks->erase(Pos);
    return AR_Success;
  }

  MetaSema::ActionResult MetaSema::actOnUCommand(llvm::StringRef file) {
    //Get the canonical path, taking into account interp and system search paths
    return actOnUCommand(file, m_Interpreter.lookupFileOrLibrary(file), true);
  }

  void MetaSema::actOnICommand(llvm::StringRef path) const {
    if (path.empty())
      m_Interpreter.DumpIncludePath();
    else
      m_Interpreter.AddIncludePath(path.str());
  }

  void MetaSema::actOnrawInputCommand(SwitchMode mode/* = kToggle*/) const {
    if (mode == kToggle) {
      bool flag = !m_Interpreter.isRawInputEnabled();
      m_Interpreter.enableRawInput(flag);
      // FIXME:
      m_MetaProcessor.getOuts() << (flag ? "U" :"Not u") << "sing raw input\n";
    }
    else
      m_Interpreter.enableRawInput(mode);
  }

  void MetaSema::actOndebugCommand(llvm::Optional<int> mode) const {
    clang::CodeGenOptions& CGO = m_Interpreter.get<clang::CodeGenOptions>();
    if (!mode) {
      bool flag = CGO.getDebugInfo() == clang::codegenoptions::NoDebugInfo;
      if (flag)
        CGO.setDebugInfo(clang::codegenoptions::LimitedDebugInfo);
      else
        CGO.setDebugInfo(clang::codegenoptions::NoDebugInfo);
      // FIXME:
      m_MetaProcessor.getOuts() << (flag ? "G" : "Not g")
                                << "enerating debug symbols\n";
    }
    else {
      static const int NumDebInfos = 5;
      clang::codegenoptions::DebugInfoKind DebInfos[NumDebInfos] = {
        clang::codegenoptions::NoDebugInfo,
        clang::codegenoptions::LocTrackingOnly,
        clang::codegenoptions::DebugLineTablesOnly,
        clang::codegenoptions::LimitedDebugInfo,
        clang::codegenoptions::FullDebugInfo
      };
      if (*mode >= NumDebInfos)
        mode = NumDebInfos - 1;
      else if (*mode < 0)
        mode = 0;
      CGO.setDebugInfo(DebInfos[*mode]);
      if (!*mode) {
        m_MetaProcessor.getOuts() << "Not generating debug symbols\n";
      } else {
        m_MetaProcessor.getOuts() << "Generating debug symbols level "
                                  << *mode << '\n';
      }
    }
  }

  void MetaSema::actOnprintDebugCommand(SwitchMode mode/* = kToggle*/) const {
    if (mode == kToggle) {
      bool flag = !m_Interpreter.isPrintingDebug();
      m_Interpreter.enablePrintDebug(flag);
      // FIXME:
      m_MetaProcessor.getOuts() << (flag ? "P" : "Not p") << "rinting Debug\n";
    }
    else
      m_Interpreter.enablePrintDebug(mode);
  }

  void MetaSema::actOnstoreStateCommand(llvm::StringRef name) const {
    m_Interpreter.storeInterpreterState(name);
  }

  void MetaSema::actOncompareStateCommand(llvm::StringRef name) const {
    m_Interpreter.compareInterpreterState(name);
  }

  void MetaSema::actOnstatsCommand(llvm::StringRef name,
                                   llvm::StringRef args) const {
    m_Interpreter.dump(name, args);
  }

  void MetaSema::actOndynamicExtensionsCommand(SwitchMode mode/* = kToggle*/)
    const {
    if (mode == kToggle) {
      bool flag = !m_Interpreter.isDynamicLookupEnabled();
      m_Interpreter.enableDynamicLookup(flag);
      // FIXME:
      m_MetaProcessor.getOuts()
        << (flag ? "U" : "Not u") << "sing dynamic extensions\n";
    }
    else
      m_Interpreter.enableDynamicLookup(mode);
  }

  void MetaSema::actOnhelpCommand() const {
    std::string& metaString = m_Interpreter.getOptions().MetaString;
    llvm::raw_ostream& outs = m_MetaProcessor.getOuts();
    outs << "\n Cling (C/C++ interpreter) meta commands usage\n"
      " All commands must be preceded by a '" << metaString << "', except\n"
      " for the evaluation statement { }\n"
      " ==============================================================================\n"
      " Syntax: " << metaString << "Command [arg0 arg1 ... argN]\n"
      "\n"
      "   " << metaString << "L <filename>\t\t- Load the given file or library\n\n"

      "   " << metaString << "(x|X) <filename>[args]\t- Same as .L and runs a function with"
                             "\n\t\t\t\t  signature: ret_type filename(args)\n"
      "\n"
      "   " << metaString << "> <filename>\t\t- Redirect command to a given file\n"
        "      '>' or '1>'\t\t- Redirects the stdout stream only\n"
        "      '2>'\t\t\t- Redirects the stderr stream only\n"
        "      '&>' (or '2>&1')\t\t- Redirects both stdout and stderr\n"
        "      '>>'\t\t\t- Appends to the given file\n"
      "\n"
      "   " << metaString << "undo [n]\t\t\t- Unloads the last 'n' inputs lines\n"
      "\n"
      "   " << metaString << "U <filename>\t\t- Unloads the given file\n"
      "\n"
      "   " << metaString << "I [path]\t\t\t- Shows the include path. If a path is given -"
                             "\n\t\t\t\t  adds the path to the include paths\n"
      "\n"
      "   " << metaString << "O <level>\t\t\t- Sets the optimization level (0-3)"
                             "\n\t\t\t\t  (not yet implemented)\n"
      "\n"
      "   " << metaString << "class <name>\t\t- Prints out class <name> in a CINT-like style\n"
      "\n"
      "   " << metaString << "files \t\t\t- Prints out some CINT-like file statistics\n"
      "\n"
      "   " << metaString << "fileEx \t\t\t- Prints out some file statistics\n"
      "\n"
      "   " << metaString << "g \t\t\t\t- Prints out information about global variable"
                             "\n\t\t\t\t  'name' - if no name is given, print them all\n"
      "\n"
      "   " << metaString << "@ \t\t\t\t- Cancels and ignores the multiline input\n"
      "\n"
      "   " << metaString << "rawInput [0|1]\t\t- Toggle wrapping and printing the"
                             "\n\t\t\t\t  execution results of the input\n"
      "\n"
      "   " << metaString << "dynamicExtensions [0|1]\t- Toggles the use of the dynamic scopes and the"
                             "\n\t\t\t\t  late binding\n"
      "\n"
      "   " << metaString << "printDebug [0|1]\t\t- Toggles the printing of input's corresponding"
                             "\n\t\t\t\t  state changes\n"
      "\n"
      "   " << metaString << "storeState <filename>\t- Store the interpreter's state to a given file\n"
      "\n"
      "   " << metaString << "compareState <filename>\t- Compare the interpreter's state with the one"
                             "\n\t\t\t\t  saved in a given file\n"
      "\n"
      "   " << metaString << "stats [name]\t\t- Show stats for internal data structures\n"
                             "\t\t\t\t  'ast'  abstract syntax tree stats\n"
                             "\t\t\t\t  'asttree [filter]'  abstract syntax tree layout\n"
                             "\t\t\t\t  'decl' dump ast declarations\n"
                             "\t\t\t\t  'undo' show undo stack\n"
      "\n"
      "   " << metaString << "help\t\t\t- Shows this information\n"
      "\n"
      "   " << metaString << "q\t\t\t\t- Exit the program\n"
      "\n";
  }

  void MetaSema::actOnfileExCommand() const {
    const clang::SourceManager& SM = m_Interpreter.get<clang::SourceManager>();
    SM.getFileManager().PrintStats();

    m_MetaProcessor.getOuts() << "\n***\n\n";

    for (clang::SourceManager::fileinfo_iterator I = SM.fileinfo_begin(),
           E = SM.fileinfo_end(); I != E; ++I) {
      m_MetaProcessor.getOuts() << (*I).first->getName();
      m_MetaProcessor.getOuts() << "\n";
    }
    /* Only available in clang's trunk:
    clang::ASTReader* Reader = m_Interpreter.get<ASTReader*>();
    const clang::serialization::ModuleManager& ModMan
      = Reader->getModuleManager();
    for (clang::serialization::ModuleManager::ModuleConstIterator I
           = ModMan.begin(), E = ModMan.end(); I != E; ++I) {
      typedef
        std::vector<llvm::PointerIntPair<const clang::FileEntry*, 1, bool> >
        InputFiles_t;
      const InputFiles_t& InputFiles = (*I)->InputFilesLoaded;
      for (InputFiles_t::const_iterator IFI = InputFiles.begin(),
             IFE = InputFiles.end(); IFI != IFE; ++IFI) {
        m_MetaProcessor.getOuts() << IFI->getPointer()->getName();
        m_MetaProcessor.getOuts() << "\n";
      }
    }
    */
  }

  void MetaSema::actOnfilesCommand() const {
    m_Interpreter.printIncludedFiles(m_MetaProcessor.getOuts());
  }

  void MetaSema::actOnclassCommand(llvm::StringRef className) const {
    if (!className.empty())
      DisplayClass(m_MetaProcessor.getOuts(),
                   &m_Interpreter, className.str().c_str(), true);
    else
      DisplayClasses(m_MetaProcessor.getOuts(), &m_Interpreter, false);
  }

  void MetaSema::actOnClassCommand() const {
    DisplayClasses(m_MetaProcessor.getOuts(), &m_Interpreter, true);
  }

  void MetaSema::actOnNamespaceCommand() const {
    DisplayNamespaces(m_MetaProcessor.getOuts(), &m_Interpreter);
  }

  void MetaSema::actOngCommand(llvm::StringRef varName) const {
    if (varName.empty())
      DisplayGlobals(m_MetaProcessor.getOuts(), &m_Interpreter);
    else
      DisplayGlobal(m_MetaProcessor.getOuts(),
                    &m_Interpreter, varName.str().c_str());
  }

  void MetaSema::actOnTypedefCommand(llvm::StringRef typedefName) const {
    if (typedefName.empty())
      DisplayTypedefs(m_MetaProcessor.getOuts(), &m_Interpreter);
    else
      DisplayTypedef(m_MetaProcessor.getOuts(),
                     &m_Interpreter, typedefName.str().c_str());
  }

  MetaSema::ActionResult
  MetaSema::actOnShellCommand(llvm::StringRef commandLine,
                              Value* result) const {
    llvm::StringRef trimmed(commandLine.trim(" \t\n\v\f\r "));
    if (!trimmed.empty()) {
      int ret = std::system(trimmed.str().c_str());

      // Build the result
      clang::ASTContext& Ctx = m_Interpreter.get<clang::ASTContext>();
      if (result) {
        *result = Value(Ctx.IntTy, m_Interpreter);
        result->getAs<long long>() = ret;
      }

      return (ret == 0) ? AR_Success : AR_Failure;
    }
    if (result)
      *result = Value();
    // nothing to run - should this be success or failure?
    return AR_Failure;
  }

  bool MetaSema::registerUnloadPoint(const Transaction* unloadPoint,
                                     FileEntry inEntry ) {
    FileEntry fileEntry = m_Interpreter.lookupFileOrLibrary(std::move(inEntry));
    const clang::FileEntry* Entry = fileEntry;
    if (!Entry) {
      // There's a small chance clang couldn't resolve the relative path, and
      // fileEntry now contains a resolved absolute one.
      if (fileEntry.resolved())
        Entry =
          m_Interpreter.getSema().getSourceManager().getFileManager().getFile(
              fileEntry.name(), /*OpenFile*/ false, /*CacheFailure*/ false);
    }
    if (Entry) {
      if (!m_Watermarks.get()) {
        m_Watermarks.reset(new Watermarks);
        if (!m_Watermarks.get()) {
          ::perror("Could not allocate watermarks");
          return false;
        }
      }
      // register as a watermark, or error if already registered
      if (m_Watermarks->emplace(Entry, unloadPoint).second)
        return true;

      m_Interpreter.getDiagnostics().Report(m_Interpreter.getSourceLocation(),
                         clang::diag::err_duplicate_member) << fileEntry.name();
    } else {
      m_Interpreter.getDiagnostics().Report(m_Interpreter.getSourceLocation(),
                        clang::diag::err_pp_file_not_found) << fileEntry.name();
    }
    return false;
  }
} // end namespace cling

