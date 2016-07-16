//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Lukasz Janyst <ljanyst@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/Interpreter/Interpreter.h"
#include "cling/MetaProcessor/MetaProcessor.h"
#include "cling/UserInterface/UserInterface.h"

#include "clang/Basic/LangOptions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/FrontendTool/Utils.h"

#include "llvm/Support/Signals.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>

#if defined(WIN32) && defined(_MSC_VER)
#include <crtdbg.h>
#endif

// If we are running with -verify a reported has to be returned as unsuccess.
// This is relevant especially for the test suite.
static int checkDiagErrors(clang::CompilerInstance* CI) {

  bool Ret = CI->getDiagnostics().getClient()->getNumErrors();

  if (CI->getDiagnosticOpts().VerifyDiagnostics) {
    // If there was an error that came from the verifier we must return 1 as
    // an exit code for the process. This will make the test fail as expected.
    clang::DiagnosticConsumer* Client = CI->getDiagnostics().getClient();
    Client->EndSourceFile();
    Ret = Client->getNumErrors();

    // The interpreter expects BeginSourceFile/EndSourceFiles to be balanced.
    Client->BeginSourceFile(CI->getLangOpts(), &CI->getPreprocessor());
  }

  return Ret ? EXIT_FAILURE : EXIT_SUCCESS;
}

int main( int argc, char **argv ) {

  llvm::llvm_shutdown_obj shutdownTrigger;

  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);
  llvm::PrettyStackTraceProgram X(argc, argv);

#if defined(_WIN32) && defined(_MSC_VER)
  // Suppress error dialogs to avoid hangs on build nodes.
  // One can use an environment variable (Cling_GuiOnAssert) to enable
  // the error dialogs.
  const char *EnablePopups = getenv("Cling_GuiOnAssert");
  if (EnablePopups == nullptr || EnablePopups[0] == '0') {
    ::_set_error_mode(_OUT_TO_STDERR);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
  }
#endif

  // Set up the interpreter
  cling::Interpreter Interp(argc, argv);
  const cling::InvocationOptions& Opts = Interp.getOptions();

  if (!Interp.isValid()) {
    if (Opts.Help || Opts.ShowVersion)
      return EXIT_SUCCESS;

    if (Opts.CompilerOpts.HasOutput) {
      if (clang::CompilerInstance* CI = Interp.getCIOrNull()) {
        if (ExecuteCompilerInvocation(CI))
          return checkDiagErrors(CI);
        
        checkDiagErrors(CI);
      }

      // If !CI then we can't output what was requested.
      return EXIT_FAILURE;
    }

    // FIXME: Diagnose what went wrong, until then we can't even be sure
    // llvm::errs is valid...
    ::perror("Could not create Interpreter instance");
    return EXIT_FAILURE;
  }

  Interp.AddIncludePath(".");

  for (const std::string& Lib : Opts.LibsToLoad)
    Interp.loadFile(Lib);

  cling::ui::UserInterface Ui(Interp);
  // If we are not interactive we're supposed to parse files
  if (!Opts.IsInteractive()) {
    for (const std::string &Input : Opts.Inputs) {
      std::string Cmd;
      cling::Interpreter::CompilationResult Result;
      const cling::FileEntry FE = Interp.lookupFileOrLibrary(Input);
      if (FE.exists()) {
        std::ifstream File(FE.filePath());
        std::string Line;
        std::getline(File, Line);
        if (Line[0] == '#' && Line[1] == '!') {
          // TODO: Check whether the filename specified after #! is the current
          // executable.
          while (std::getline(File, Line)) {
            Ui.getMetaProcessor()->process(Line.c_str(), Result, 0);
          }
          continue;
        }
        else
          Cmd = ".x ";
      } else {
        const llvm::StringRef Ext = llvm::sys::path::extension(Input);
        const size_t ArgBegin = Ext.find('(');
        if (ArgBegin != llvm::StringRef::npos) {
          if (Ext.substr(ArgBegin).find(')') != llvm::StringRef::npos) {
            if (Interp.lookupFileOrLibrary(
                        Input.substr(0, Input.size() - (Ext.size()-ArgBegin)))
                    .exists())
              Cmd = ".x ";
          }
        }
      }
      Cmd += Input;
      Ui.getMetaProcessor()->process(Cmd.c_str(), Result, 0);
    }
  }
  else {
    Ui.runInteractively(Opts.NoLogo);
  }

  return checkDiagErrors(Interp.getCI());
}
