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

  ///\brief Class that stores options that are used by both cling and
  /// clang::CompilerInvocation.
  ///
  class CompilerOptions {
  public:
    enum {
      kLanguageSet     = 1,  ///< Use provided -x flag
      kLanguageObjC    = 2,  ///< Input file is '.m'
      kLanguageObjCXX  = 3,  ///< Input file is '.mm'
    };
    
    /// \brief Construct CompilerOptions from given arguments. When argc & argv
    /// are 0, all arguments are saved into Remaining to pass to clang. If argc
    /// or argv is 0, caller is must fill in Remaining with any arguments that
    /// should be passed to clang.
    ///
    CompilerOptions(int argc = 0, const char* const *argv = nullptr);

    ///\brief Parse argc, and argv into our variables.
    ///
    ///\param [in] argc - argument count
    ///\param [in] argv - arguments
    ///\param [out] Inputs - save all arguments that are inputs/files here
    ///
    void Parse(int argc, const char* const argv[],
               std::vector<std::string>* Inputs = nullptr);

    unsigned Language : 2;
    unsigned ResourceDir : 1;
    unsigned SysRoot : 1;
    unsigned NoBuiltinInc : 1;
    unsigned NoCXXInc : 1;
    unsigned StdVersion : 1;
    unsigned StdLib : 1;
    unsigned HasOutput : 1;
    unsigned Verbose : 1;
    unsigned JITFormat : 2;

    ///\brief The remaining arguments to pass to clang.
    ///
    std::vector<const char*> Remaining;
  };

  class InvocationOptions {
  public:
    InvocationOptions(int argc, const char* const argv[]);

    /// \brief A line starting with this string is assumed to contain a
    ///        directive for the MetaProcessor. Defaults to "."
    std::string MetaString;

    std::vector<std::string> LibsToLoad;
    std::vector<std::string> LibSearchPath;
    std::vector<std::string> Inputs;
    CompilerOptions CompilerOpts;

    unsigned ErrorOut : 1;
    unsigned NoLogo : 1;
    unsigned ShowVersion : 1;
    unsigned Help : 1;
    unsigned NoRuntime : 1;
    bool Verbose() const { return CompilerOpts.Verbose; }

    static void PrintHelp();

    // Interactive means no input (or one input that's "-")
    bool IsInteractive() const {
      return Inputs.empty() || (Inputs.size() == 1 && Inputs[0] == "-");
    }
  };
}

#endif // INVOCATIONOPTIONS
