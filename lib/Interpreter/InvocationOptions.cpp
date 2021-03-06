//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Axel Naumann <axel@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/Interpreter/InvocationOptions.h"
#include "cling/Interpreter/ClingOptions.h"
#include "cling/Utils/Output.h"

#include "clang/Driver/Options.h"

#include "llvm/ADT/Triple.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Support/Path.h"

#include <memory>
// strncasecmp
#ifndef LLVM_ON_WIN32
 #include <strings.h>
#else
 #include <string.h>
 #define strncasecmp _strnicmp
#endif

using namespace clang;
using namespace clang::driver;

using namespace llvm;
using namespace llvm::opt;

using namespace cling;
using namespace cling::driver::clingoptions;

namespace {

// MSVC C++ backend currently does not support -nostdinc++. Translate it to
// -nostdinc so users scripts are insulated from mundane implementation details.
#if defined(LLVM_ON_WIN32) && !defined(_LIBCPP_VERSION)
#define CLING_TRANSLATE_NOSTDINCxx
// Likely to be string-pooled, but make sure it's valid after func exit.
static const char kNoStdInc[] = "-nostdinc";
#endif

#define PREFIX(NAME, VALUE) const char *const NAME[] = VALUE;
#define OPTION(PREFIX, NAME, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS, PARAM, \
               HELPTEXT, METAVAR)
#include "cling/Interpreter/ClingOptions.inc"
#undef OPTION
#undef PREFIX

  static const OptTable::Info ClingInfoTable[] = {
#define PREFIX(NAME, VALUE)
#define OPTION(PREFIX, NAME, ID, KIND, GROUP, ALIAS, ALIASARGS, FLAGS, PARAM, \
               HELPTEXT, METAVAR)   \
  { PREFIX, NAME, HELPTEXT, METAVAR, OPT_##ID, Option::KIND##Class, PARAM, \
    FLAGS, OPT_##GROUP, OPT_##ALIAS, ALIASARGS },
#include "cling/Interpreter/ClingOptions.inc"
#undef OPTION
#undef PREFIX
  };

  class ClingOptTable : public OptTable {
  public:
    ClingOptTable()
      : OptTable(ClingInfoTable) {}
  };

  static OptTable* CreateClingOptTable() {
    return new ClingOptTable();
  }

  static void ParseStartupOpts(cling::InvocationOptions& Opts,
                               InputArgList& Args) {
    Opts.ErrorOut = Args.hasArg(OPT__errorout);
    Opts.NoLogo = Args.hasArg(OPT__nologo);
    Opts.ShowVersion = Args.hasArg(OPT_version);
    Opts.Help = Args.hasArg(OPT_help);
    Opts.NoRuntime = Args.hasArg(OPT_noruntime);
    if (Arg* MetaStringArg = Args.getLastArg(OPT__metastr, OPT__metastr_EQ)) {
      Opts.MetaString = MetaStringArg->getValue();
      if (Opts.MetaString.empty()) {
        cling::errs() << "ERROR: meta string must be non-empty! Defaulting to '.'.\n";
        Opts.MetaString = ".";
      }
    }

    if (Arg* JitArg = Args.getLastArg(OPT__jit, OPT__jit_EQ)) {
      const char* JitFmt = JitArg->getValue();
      const char Elf[] = "elf";
      const char Coff[] = "coff";
      const char MachO[] = "mach-o";
      const std::pair<const char*, llvm::Triple::ObjectFormatType> Default =
#if defined(__APPLE__)
        std::make_pair(MachO, llvm::Triple::MachO);
#elif defined(LLVM_ON_WIN32)
        std::make_pair(Coff, llvm::Triple::COFF);
#else
        std::make_pair(Elf, llvm::Triple::ELF);
#endif
      if (::strncasecmp(JitFmt, Elf, sizeof(Elf)) == 0)
        Opts.CompilerOpts.JITFormat = llvm::Triple::ELF;
      else if (::strncasecmp(JitFmt, MachO, sizeof(MachO)) == 0)
        Opts.CompilerOpts.JITFormat = llvm::Triple::MachO;
      else if (::strncasecmp(JitFmt, Coff, sizeof(Coff)) == 0) {
#if !defined(LLVM_ON_WIN32)
        cling::errs() << "COFF only supported on Windows, using: '"
                      << Default.first << "'\n";
#else
        Opts.CompilerOpts.JITFormat = llvm::Triple::COFF;
#endif
      }
      else
        cling::errs() << "Unkown JIT format: '" << JitFmt << "', using '"
                      << Default.first << "'\n";

      // If default was specified don't bother marking the format.
      if (Opts.CompilerOpts.JITFormat == Default.second)
        Opts.CompilerOpts.JITFormat = 0;
    }
  }

  static void Extend(std::vector<std::string>& A, std::vector<std::string> B) {
    A.reserve(A.size()+B.size());
    for (std::string& Val: B)
      A.push_back(std::move(Val));
  }

  static void ParseLinkerOpts(cling::InvocationOptions& Opts,
                              InputArgList& Args /* , Diags */) {
    Extend(Opts.LibsToLoad, Args.getAllArgValues(OPT_l));
    Extend(Opts.LibSearchPath, Args.getAllArgValues(OPT_L));
  }
}

CompilerOptions::CompilerOptions(int argc, const char* const* argv) :
  Language(0), ResourceDir(false), SysRoot(false), NoBuiltinInc(false),
  NoCXXInc(false), StdVersion(false), StdLib(false), HasOutput(false),
  Verbose(false), JITFormat(0) {
  if (argc && argv) {
    // Preserve what's already in Remaining, the user might want to push args
    // to clang while still using main's argc, argv
    // insert should/usually does call reserve, but its not part of the standard
    Remaining.reserve(Remaining.size() + argc);
    Remaining.insert(Remaining.end(), argv, argv+argc);
    Parse(argc, argv);
  }
}

void CompilerOptions::Parse(int argc, const char* const argv[],
                            std::vector<std::string>* Inputs) {
  unsigned MissingArgIndex, MissingArgCount;
  std::unique_ptr<OptTable> OptsC1(createDriverOptTable());
  ArrayRef<const char *> ArgStrings(argv+1, argv + argc);

  InputArgList Args(OptsC1->ParseArgs(ArgStrings, MissingArgIndex,
                    MissingArgCount, 0,
                    options::NoDriverOption | options::CLOption));

  for (const Arg* arg : Args) {
    switch (arg->getOption().getID()) {
      // case options::OPT_d_Flag:
      case options::OPT_E:
      case options::OPT_o: HasOutput = true; break;
      case options::OPT_x: Language = kLanguageSet; break;
      case options::OPT_resource_dir: ResourceDir = true; break;
      case options::OPT_isysroot: SysRoot = true; break;
      case options::OPT_std_EQ: StdVersion = true; break;
      case options::OPT_stdlib_EQ: StdLib = true; break;
      // case options::OPT_nostdlib:
      case options::OPT_nobuiltininc: NoBuiltinInc = true; break;
      // case options::OPT_nostdinc:
      case options::OPT_nostdincxx: NoCXXInc = true; break;
      case options::OPT_v: Verbose = true; break;

      default:
        if (Inputs && arg->getOption().getKind() == Option::InputClass) {
          Inputs->push_back(arg->getValue());
#ifdef CLING_OBJC_SUPPORT
          if (!Language) {
            const llvm::StringRef ext = llvm::sys::path::extension(Inputs->back());
            if (ext.equals(".m"))
              Language = kLanguageObjC;
            else if (ext.equals(".mm"))
              Language = kLanguageObjCXX;
            else if (ext.startswith(".m("))
              Language = kLanguageObjC;
            else if (ext.startswith(".mm("))
              Language = kLanguageObjCXX;
          }
#endif
        }
        break;
    }
  }
}

InvocationOptions::InvocationOptions(int argc, const char* const* argv) :
  MetaString("."), ErrorOut(false), NoLogo(false), ShowVersion(false),
  Help(false), NoRuntime(false) {

  ArrayRef<const char *> ArgStrings(argv, argv + argc);
  unsigned MissingArgIndex, MissingArgCount;
  std::unique_ptr<OptTable> Opts(CreateClingOptTable());

  InputArgList Args(Opts->ParseArgs(ArgStrings, MissingArgIndex,
                    MissingArgCount, 0,
                    options::NoDriverOption | options::CLOption));

  // Forward unknown arguments.
  for (const Arg* arg : Args) {
    switch (arg->getOption().getKind()) {
      case Option::FlagClass:
        // pass -v to clang as well
        if (arg->getOption().getID() != OPT_v)
          break;
      case Option::UnknownClass:
      case Option::InputClass:
        // prune "-" we need to control where it appears when invoking clang
        if (!arg->getSpelling().equals("-")) {
          if (const char* Arg = argv[arg->getIndex()]) {
#ifdef CLING_TRANSLATE_NOSTDINCxx
            if (!::strcmp(Arg, "-nostdinc++"))
              Arg = kNoStdInc;
#endif
            CompilerOpts.Remaining.push_back(Arg);
          }
        }
      default:
        break;
    }
  }

  // Get Input list and any compiler specific flags we're interested in
  CompilerOpts.Parse(argc, argv, &Inputs);

  ParseStartupOpts(*this, Args);
  ParseLinkerOpts(*this, Args);
}

void InvocationOptions::PrintHelp() {
  std::unique_ptr<OptTable> Opts(CreateClingOptTable());

  Opts->PrintHelp(cling::outs(), "cling",
                  "cling: LLVM/clang C++ Interpreter: http://cern.ch/cling");

  cling::outs() << "\n\n";

  std::unique_ptr<OptTable> OptsC1(createDriverOptTable());
  OptsC1->PrintHelp(cling::outs(), "clang -cc1",
                    "LLVM 'Clang' Compiler: http://clang.llvm.org");
}
