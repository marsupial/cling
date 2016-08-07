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

#include "llvm/Support/Signals.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/ManagedStatic.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>

int main( int argc, char **argv ) {

  llvm::llvm_shutdown_obj shutdownTrigger;

  //llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);
  //llvm::PrettyStackTraceProgram X(argc, argv);

  // Set up the interpreter
  cling::Interpreter interp(argc, argv);
  const cling::InvocationOptions& opts = interp.getOptions();

  if (!interp.isValid()) {
    if (opts.Help || opts.ShowVersion || opts.HadOutput)
      return EXIT_SUCCESS;

    // FIXME: Diagnose what went wrong, until then we can't even be sure
    // llvm::errs is valid...
    ::perror("Could not create Interpreter instance");
    return EXIT_FAILURE;
  }

  clang::CompilerInstance* CI = interp.getCI();
  interp.AddIncludePath(".");

  for (size_t I = 0, N = opts.LibsToLoad.size(); I < N; ++I) {
    interp.loadFile(opts.LibsToLoad[I]);
  }

  cling::UserInterface ui(interp);
  // If we are not interactive we're supposed to parse files
  if (!opts.IsInteractive()) {
    for (const std::string &input : opts.Inputs) {
      std::string cmd;
      cling::Interpreter::CompilationResult compRes;
      const std::string file = interp.lookupFileOrLibrary(input);
      if (!file.empty()) {
        std::ifstream infile(file);
        std::string line;
        std::getline(infile, line);
        if (line[0] == '#' && line[1] == '!') {
          // TODO: Check whether the filename specified after #! is the current
          // executable.
          while(std::getline(infile, line)) {
            ui.getMetaProcessor()->process(line.c_str(), compRes, 0);
          }
          continue;
        }
        else
          cmd += ".x ";
      }
      cmd += input;
      ui.getMetaProcessor()->process(cmd.c_str(), compRes, 0);
    }
  }
  else {
    ui.runInteractively(opts.NoLogo);
  }

  bool ret = CI->getDiagnostics().getClient()->getNumErrors();

  // if we are running with -verify a reported has to be returned as unsuccess.
  // This is relevant especially for the test suite.
  if (CI->getDiagnosticOpts().VerifyDiagnostics) {
    // If there was an error that came from the verifier we must return 1 as
    // an exit code for the process. This will make the test fail as expected.
    clang::DiagnosticConsumer* client = CI->getDiagnostics().getClient();
    client->EndSourceFile();
    ret = client->getNumErrors();

    // The interpreter expects BeginSourceFile/EndSourceFiles to be balanced.
    client->BeginSourceFile(CI->getLangOpts(), &CI->getPreprocessor());
  }

  return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}
