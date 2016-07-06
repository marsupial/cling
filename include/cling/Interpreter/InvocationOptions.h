//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Axel Naumann <axel@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_INVOCATIONOPTIONS_H
#define CLING_INVOCATIONOPTIONS_H

#include <string>
#include <vector>

namespace cling {
  class InvocationOptions {
  public:
    InvocationOptions():
      MetaString("."), ErrorOut(false), NoLogo(false), ShowVersion(false),
      Verbose(false), Help(false), NoRuntime(false), HadOutput(false)  {}

    /// \brief A line starting with this string is assumed to contain a
    ///        directive for the MetaProcessor. Defaults to "."
    std::string MetaString;

    std::vector<std::string> LibsToLoad;
    std::vector<std::string> LibSearchPath;
    std::vector<std::string> Inputs;

    bool ErrorOut;
    bool NoLogo;
    bool ShowVersion;
    bool Verbose;
    bool Help;
    bool NoRuntime;
    bool HadOutput;


    static InvocationOptions CreateFromArgs(int argc, const char* const argv[],
                                            std::vector<unsigned>& leftoverArgs
                                            /* , Diagnostic &Diags */);

    void PrintHelp();

    // Interactive means no input (or one input that's "-")
    bool IsInteractive() const {
      return Inputs.empty() || (Inputs.size() == 1 && Inputs[0] == "-");
    }
  };
}

#endif // INVOCATIONOPTIONS
