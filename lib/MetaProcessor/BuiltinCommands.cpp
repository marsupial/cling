//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author: Roman Zulak
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "Display.h"

#include "cling/MetaProcessor/Commands.h"
#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Value.h"
#include "cling/Utils/Platform.h"

#include "clang/AST/ASTContext.h"
#include "clang/Basic/DebugInfoOptions.h"
#include "clang/Frontend/CodeGenOptions.h"

#include <cctype>

namespace cling {
namespace meta {
namespace {

using namespace clang;

typedef CommandHandler::Argument Argument;

CommandResult TCommand(const Invocation& I, Argument Input, Argument Output) {
  if (Input.empty() || Output.empty())
    return kCmdInvalidSyntax;
  I.Interp.GenerateAutoloadingMap(Input, Output);
  return kCmdSuccess;
}

CommandResult LCommand(const Invocation& I, Argument Arg) {
  if (Arg.empty())
    return kCmdInvalidSyntax;

  if (Arg.IsComment()) {
    //I.Interp.declare(Arg1);
    llvm::errs() << "Declare '" << Arg.str() << "'\n";
  } else
    llvm::errs() << "Load '" << Arg.str() << "'\n";

  return kCmdSuccess;
}

CommandResult UCommand(const Invocation& I, Argument Name) {
  // I.actions().actOnUCommand(Name);
  return kCmdUnimplemented;
}

CommandResult XCommand(const Invocation& I,
                       const CommandHandler::SplitArguments& Args) {
  if (Args.empty())
    return kCmdInvalidSyntax;

  llvm::SmallVector<std::pair<llvm::StringRef, llvm::StringRef>, 8> Calls;
  for (auto Itr = Args.rbegin(), End = Args.rend(); Itr < End; ++Itr) {
    llvm::StringRef Call("()");
    if (Itr->Group == '(') {
      Call = Itr->str();
      if (++Itr >= End)
        return kCmdInvalidSyntax;
    }
    Calls.emplace_back(Itr->str(), Call);
  }
  for (auto& Call : Calls) {
    llvm::errs() << "'" << Call.first << "' : " << Call.second << "\n";
  }
  return kCmdSuccess;
}

CommandResult ClassCommand(const Invocation& I, Argument Name) {
  if (I.Cmd.front() == 'C')
    DisplayClasses(I.Out, &I.Interp, true);
  else if (Name.empty())
    DisplayClasses(I.Out, &I.Interp, false);
  else
    DisplayClass(I.Out, &I.Interp, Name, true);
  return kCmdSuccess;
}

CommandResult IncludesCommand(const Invocation& I, Argument Arg) {
  if (Arg.empty())
    I.Interp.DumpIncludePath(&I.Out);
  else
    I.Interp.AddIncludePath(Arg);
  return kCmdSuccess;
}

CommandResult FilesCommand(const Invocation& I) {
  I.Interp.printIncludedFiles(I.Out);
  return kCmdSuccess;
}

CommandResult NamespaceCommand(const Invocation& I) {
  DisplayNamespaces(I.Out, &I.Interp);
  return kCmdSuccess;
}

CommandResult GCommand(const Invocation& I, Argument Name) {
  if (Name.empty())
    DisplayGlobals(I.Out, &I.Interp);
  else
    DisplayGlobal(I.Out, &I.Interp, Name);
  return kCmdSuccess;
}

CommandResult TypedefCommand(const Invocation& I, Argument Name) {
  if (Name.empty())
    DisplayTypedefs(I.Out, &I.Interp);
  else
    DisplayTypedef(I.Out, &I.Interp, Name);
  return kCmdSuccess;
}

CommandResult StatsCommand(const Invocation& I, Argument Arg0, Argument Arg1) {
  I.Interp.dump(Arg0, Arg1 /* FIXME: Redirection into I.Out*/);
  return kCmdSuccess;
}

CommandResult TraceCommand(const Invocation& I, Argument Arg0, Argument Arg1) {
  llvm::StringRef Name = Arg0;
  if (Name == "ast")
    Name = "asttree";
  I.Interp.dump(Name, Arg1 /* FIXME: Redirection into I.Out*/);
  return kCmdSuccess;
}

CommandResult StoreStateCommand(const Invocation& I, Argument Name) {
  if (Name.empty())
    return kCmdInvalidSyntax;

  I.Interp.storeInterpreterState(Name);
  return kCmdSuccess;
}

CommandResult CompareStateCommand(const Invocation& I, Argument Name) {
  if (Name.empty())
    return kCmdInvalidSyntax;

  I.Interp.compareInterpreterState(Name);
  return kCmdSuccess;
}

CommandResult UndoCommand(const Invocation& I, Argument Arg) {
  const auto Val = Arg.Optional<unsigned>();
  I.Interp.unload(Val.hasValue() ? Val.getValue() : 1);
  return kCmdSuccess;
}

void ShowState(llvm::raw_ostream& Out, bool Flag, llvm::StringRef Stat) {
  if (!Flag) {
    Out << "Not " << std::tolower(Stat.front()) << " ";
    Stat = Stat.substr(1);
  }
  Out << Stat << '\n';
}

CommandResult DynamicExtensionsCommand(const Invocation& I, Argument Arg) {
  const auto Value = Arg.Optional<unsigned>();
  if (!Value) {
    const bool Flag = !I.Interp.isDynamicLookupEnabled();
    I.Interp.enableDynamicLookup(Flag);
    ShowState(I.Out, Flag, "Using dynamic extensions");
  } else
    I.Interp.enableDynamicLookup(Value.getValue());
  return kCmdSuccess;
}

CommandResult PrintDebugCommand(const Invocation& I, Argument Arg) {
  const auto Value = Arg.Optional<unsigned>();
  if (!Value) {
    const bool Flag = !I.Interp.isPrintingDebug();
    I.Interp.enablePrintDebug(Flag);
    ShowState(I.Out, Flag, "Printing Debug");
  } else
    I.Interp.enablePrintDebug(Value.getValue());
  return kCmdSuccess;
}

CommandResult RawInputCommand(const Invocation& I, Argument Arg) {
  const auto Value = Arg.Optional<unsigned>();
  if (!Value) {
    const bool Flag = !I.Interp.isRawInputEnabled();
    I.Interp.enableRawInput(Flag);
    ShowState(I.Out, Flag, "Using raw input");
  } else
    I.Interp.enableRawInput(Value.getValue());
  return kCmdSuccess;
}

CommandResult OCommand(const Invocation& I, Argument Arg) {
  if (Arg.empty()) {
    I.Out << "Current cling optimization level: "
          << I.Interp.getDefaultOptLevel() << '\n';
    return kCmdSuccess;
  }

  bool WasBool;
  const auto Value = Arg.Optional<unsigned>(&WasBool);
  if (!Value || WasBool)
    return kCmdInvalidSyntax;

  const unsigned Level = Value.getValue();
  if (Level >= 4) {
    I.Out << "Refusing to set invalid cling optimization level "
          << Level << '\n';
    return kCmdFailure;
  }

  I.Interp.setDefaultOptLevel(Level);
  return kCmdSuccess;
}

CommandResult FileExCommand(const Invocation& I, Argument Arg) {
  const clang::SourceManager& SM = I.Interp.get<SourceManager>();
  SM.getFileManager().PrintStats(); // I.Out
  I.Out << "\n***\n\n";
  for (auto Itr = SM.fileinfo_begin(), E = SM.fileinfo_end(); Itr != E; ++Itr) {
    I.Out << (*Itr).first->getName();
    I.Out << "\n";
  }

#if 0
  // Need to link to libclangFrontend for this
  clang::ASTReader& Reader = I.Interp.get<ASTReader>();
  const clang::serialization::ModuleManager& ModMan = Reader.getModuleManager();
  for (auto Itr = ModMan.begin(), E = ModMan.end(); Itr != E; ++Itr) {
    for (auto& IFI : (*Itr)->InputFilesLoaded) {
      I.Out << IFI.getFile()->getName();
      I.Out << "\n";
    }
  }
#endif

  return kCmdSuccess;
}

CommandResult DebugCommand(const Invocation& I, Argument Arg) {
  bool WasBool;
  const auto Mode = Arg.Optional<int>(&WasBool);

  clang::CodeGenOptions& CGO = I.Interp.get<CodeGenOptions>();
  if (Mode && !WasBool) {
    const int Value = Mode.getValue();
    const int NumDebInfos = 5;
    if (Value <0 || Value >= NumDebInfos) {
      I.Out << "Debug level must be between 0-" << NumDebInfos-1 << "\n";
      return kCmdInvalidSyntax;
    }
    clang::codegenoptions::DebugInfoKind DebInfos[NumDebInfos] = {
      clang::codegenoptions::NoDebugInfo,
      clang::codegenoptions::LocTrackingOnly,
      clang::codegenoptions::DebugLineTablesOnly,
      clang::codegenoptions::LimitedDebugInfo,
      clang::codegenoptions::FullDebugInfo
    };
    CGO.setDebugInfo(DebInfos[Value]);
    if (!Value)
      I.Out << "Not generating debug symbols\n";
    else
      I.Out << "Generating debug symbols level " << Value << '\n';
  } else {
    // No mode, but an argument, show user how to use the command.
    if (!Mode && !Arg.empty())
      return kCmdInvalidSyntax;

    bool Flag = WasBool
                    ? Mode.getValue()
                    : CGO.getDebugInfo() == clang::codegenoptions::NoDebugInfo;
    CGO.setDebugInfo(Flag ? clang::codegenoptions::LimitedDebugInfo
                          : clang::codegenoptions::NoDebugInfo);
    ShowState(I.Out, Flag, "Generating debug symbols");
  }
  return kCmdSuccess;
}

CommandResult RedirectCommand(const Invocation& I,
                              const CommandHandler::SplitArguments& Args) {
  return kCmdUnimplemented;
}

CommandResult ShellCommand(const Invocation& I) {
  const std::string Trimmed = CommandHandler::Unescape(I.Args.trim());
  if (Trimmed.empty()) {
    // Nothing to run, invalid syntax
    return kCmdInvalidSyntax;
  }

  int ExitCode = 0;
  llvm::SmallString<2048> Buf;
  if (platform::Popen(Trimmed.c_str(), Buf, false/*Not 2>&1*/, &ExitCode)) {
    I.Out << Buf;
    if (I.Val) {
      // Build the result
      clang::ASTContext& Ctx = I.Interp.get<ASTContext>();
      *I.Val = Value(Ctx.IntTy, I.Interp);
      I.Val->getAs<long long>() = ExitCode;
    }
  }
  return ExitCode == 0 ? kCmdSuccess : kCmdFailure;
}

CommandResult QCommand(const Invocation& I) {
  //I.Processor->quit() = true;
  //return kCmdSuccess;
  return kCmdUnimplemented;
}

CommandResult AtCommand(const Invocation& I) {
#if 0
  if (!I.Processor)
    return kCmdFailure;
  I.Processor->cancelContinuation();
  return kCmdSuccess;
#else
  return kCmdUnimplemented;
#endif
}

CommandResult HelpCommand(const Invocation& I) {
  return kCmdUnimplemented;
}

} // anonymous namespace

CommandResult CommandHandler::AddBuiltinCommands() {

  AddCommand("!", ShellCommand, "Run shell command"); // "<cmd> [args]"

  Alias("Class",
    AddCommand("class", &ClassCommand,
               "Prints out class <name> in a CINT-like style")); // "<name>"

  AddCommand("debug", DebugCommand,
             "Generate debug information at given level"); // "[level|true|false]" Debug

  AddCommand("dynamicExtensions", DynamicExtensionsCommand,
             "Toggles the use of the dynamic scopes and the late binding"); // [0|1]

  AddCommand("files", FilesCommand,
             "Prints out some CINT-like file statistics");

  AddCommand("filesEx", FileExCommand, "Prints out some file statistics"); // Debug

  AddCommand("g", GCommand, "Prints out information about global variable "
                            "'name' - if no name is given, print them all"); // "[name]" Debug

  Alias("I",
    AddCommand("include", IncludesCommand,
               "Add give path to list of header search paths"
               ", or show the include paths if none is given.")); // "[path]"

  AddCommand("namespace", NamespaceCommand, ""); // "[name]"

  AddCommand("O", OCommand, "Sets the optimization level (0-3), "
                            "or shows what it  currently is"); // [level]

  AddCommand("printDebug", PrintDebugCommand,
             "Toggles the printing of input's corresponding"
             "\n\t\t\t\t  state changes"); // "[0|1]" Debug

  AddCommand("rawInput", RawInputCommand, "Toggle wrapping and printing "
             "the execution results of the input"); // "[0|1]"

  AddCommand("stats", StatsCommand,
             "Show stats for internal data structures"
             "\n\t\t\t\t  'ast'  abstract syntax tree stats"
             "\n\t\t\t\t  'decl' dump ast declarations"
             "\n\t\t\t\t  'undo' show undo stack"); // "<name>" Debug

  AddCommand("storeState", StoreStateCommand,
             "Store the interpreter's state to as the given name"); // "<name>" Debug);

  AddCommand("compareState", &CompareStateCommand,
             "Compare the interpreter's state with the one saved "
             "with the given name"); // "<name>" Debug);

  AddCommand("trace", TraceCommand, "");

  AddCommand("typedef", TypedefCommand, ""); // "[name]"

  AddCommand("undo", UndoCommand,
             "Unloads the last 'n' inputs lines"); // "[n]"

  AddCommand("q", QCommand, "Exit the program"); //, MetaProcessor);

  AddCommand("@", AtCommand, "Cancels and ignores the multiline input"); //, MetaProcessor);

  Alias("?",
    AddCommand("help", HelpCommand, "Shows this information"));


  AddCommand("L", LCommand, "Load the given file(s) executing the "
                            "last comment if given", CommandHandler::kPassComments); // "<file|library> [//]", MetaProcessor

  AddCommand("T", TCommand,
             "Generate autoloading map from 'infile' to 'outfile'"); // "<infile> <outfile>"

  AddCommand("U", UCommand, "Unloads the given file"); // "<library>" MetaProcessor

  Alias("x",
    AddCommand("X", XCommand,
               "Same as .L and runs a function with signature: "
               "ret_type filename(args)")); // "<filename> [args]" MetaProcessor

  AddCommand(">", RedirectCommand,
        "Redirect command to a given file\n"
        "      '>' or '1>'\t\t- Redirects the stdout stream only\n"
        "      '2>'\t\t\t- Redirects the stderr stream only\n"
        "      '&>' (or '2>&1')\t\t- Redirects both stdout and stderr\n"
        "      '>>'\t\t\t- Appends to the given file"); // "<filename>" MetaProcessor | kCmdCustomSyntax;
  
  return kCmdSuccess;
}

}
}
