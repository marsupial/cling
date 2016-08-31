//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author: Jermaine Dupris <support@greyangle.com>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "MetaParser.h"
#include "MetaActions.h"
#include "Display.h"
#include "cling/Interpreter/ClangInternalState.h"
#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Value.h"
#include "cling/MetaProcessor/MetaProcessor.h"
#include "cling/MetaProcessor/Commands.h"
#include "cling/Utils/Paths.h"
#include "../lib/Interpreter/IncrementalParser.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Format.h"
#include "clang/Sema/Sema.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Frontend/CompilerInstance.h"

using namespace cling;
using namespace cling::meta;

typedef CommandArguments::Argument Argument;

llvm::StringRef static argumentAsString(const Argument &tk) {
  switch (tk.getKind()) {
    case tok::ident:
    case tok::raw_ident:
      return tk.getIdent();
    case tok::stringlit:
      return tk.getIdentNoQuotes();
    default:
      break;
  }
  return llvm::StringRef();
}

CommandArguments::CommandArguments(Parser& Pa, class Interpreter& I,
                                   llvm::raw_ostream& Out,
                                   class Processor* Pr, Value* V)
  : m_Parser(Pa), Interpreter(I), Output(Out),
    CommandName(Pa.getCurTok().getBufStart(), Pa.getCurTok().getLength()),
    Processor(Pr), OutValue(V) {
  if (OutValue)
    *OutValue = Value();
}

const Token& CommandArguments::skipToNextToken() {
  m_Parser.consumeToken();
  m_Parser.skipWhitespace();
  return m_Parser.getCurTok();
}

const Argument& CommandArguments::nextArg(unsigned tk) {
  m_Parser.consumeAnyStringToken(tok::TokenKind(tk));
  return m_Parser.getCurTok();
}

llvm::Optional<int> CommandArguments::optionalInt(bool bools, bool* WasBool) {
  if (WasBool)
    *WasBool = false;

  llvm::Optional<int> value ;
  const Argument &arg = nextArg();
  if (arg.is(tok::constant))
    value = arg.getConstant();
  else if (arg.is(tok::ident)) {
    int tmp;
    const llvm::StringRef str = arg.getIdent();

    // Returns true on error!
    if (str.getAsInteger(10, tmp)) {
      if (bools) {
        if (str.equals("true")) {
          if (WasBool) *WasBool = true;
          value = 1;
        } else if (str.equals("false")) {
          if (WasBool) *WasBool = true;
          value = 0;
        }
      }
    } else
      value = tmp;
  }
  return value;
}

const Argument& CommandArguments::curArg () const {
  return m_Parser.getCurTok();
}

const Argument& CommandArguments::nextArg() {
  return skipToNextToken();
}

llvm::StringRef CommandArguments::nextString(unsigned tok) {
  return argumentAsString(nextArg(tok));
}

llvm::StringRef CommandArguments::nextString() {
  return nextString(tok::space);
}

llvm::StringRef CommandArguments::remaining() {
  return nextString(tok::eof);
}

llvm::StringRef CommandArguments::asPath() {
  const Argument& Arg = curArg();
  if (Arg.isOneOf(tok::slash | tok::comment)) {
    // Like bash allow //path/to/file, just ignore the first /
    const char* Start = Arg.getBufStart() + Arg.is(tok::comment);
    // If path needs spaces, it should be quoted, so next space is next arg
    const Argument& Next = nextArg(tok::space);
    // Length is length of Next + 1 for initial "/". When at eof the path is "/"
    return llvm::StringRef(Start, Next.is(tok::eof) ? 1 : Next.getLength()+1);
  }
  return argumentAsString(Arg);
}

CommandResult CommandArguments::execute(llvm::StringRef Cmd,
                                        std::string* Str,
                                        cling::Value* Value) {
  if (Str) {
    llvm::raw_string_ostream Out(*Str);
    return Commands::get().execute(Cmd, Interpreter, Out,
                                   Processor, Value ? Value : OutValue);
  }
  return Commands::get().execute(Cmd, Interpreter, Output,
                                 Processor, Value ? Value : OutValue);
}

Actions& CommandArguments::actions() const {
  assert(Processor && "MetaProcessor not available");
  return Processor->getActions();
}

namespace {
  
// Command syntax: MetaCommand := <CommandSymbol><Command>
//                 CommandSymbol := '.' | '//.'
//                 Command := LCommand | XCommand | qCommand | UCommand |
//                            ICommand | OCommand | RawInputCommand |
//                            PrintDebugCommand | DynamicExtensionsCommand |
//                            HelpCommand | FileExCommand | FilesCommand |
//                            ClassCommand | GCommand | StoreStateCommand |
//                            CompareStateCommand | StatsCommand | undoCommand
//                 LCommand := 'L' FilePath
//                 FCommand := 'F' FilePath[.h|.framework] // OS X only
//                 TCommand := 'T' FilePath FilePath
//                 >Command := '>' FilePath
//                 qCommand := 'q'
//                 XCommand := 'x' FilePath[ArgList] | 'X' FilePath[ArgList]
//                 UCommand := 'U' FilePath
//                 ICommand := 'I' [FilePath]
//                 OCommand := 'O'[' ']Constant
//                 RawInputCommand := 'rawInput' [Constant]
//                 PrintDebugCommand := 'printDebug' [Constant]
//                 DebugCommand := 'debug' [Constant]
//                 StoreStateCommand := 'storeState' "Ident"
//                 CompareStateCommand := 'compareState' "Ident"
//                 StatsCommand := 'stats' ['ast']
//                 undoCommand := 'undo' [Constant]
//                 DynamicExtensionsCommand := 'dynamicExtensions' [Constant]
//                 HelpCommand := 'help'
//                 FileExCommand := 'fileEx'
//                 FilesCommand := 'files'
//                 ClassCommand := 'class' AnyString | 'Class'
//                 GCommand := 'g' [Ident]
//                 FilePath := AnyString
//                 ArgList := (ExtraArgList) ' ' [ArgList]
//                 ExtraArgList := AnyString [, ExtraArgList]
//                 AnyString := *^(' ' | '\t')
//                 Constant := {0-9}
//                 Ident := a-zA-Z{a-zA-Z0-9}
//

CommandResult doLCommand(CommandArguments& Params) {
  llvm::StringRef File = Params.nextString(tok::comment|tok::space);
  CommandResult Result = kCmdInvalidSyntax;
  if (!File.empty()) {
    do {
      Result = Params.actions().actOnLCommand(File);

      const Argument &arg = Params.nextArg(tok::comment|tok::space);
      if (arg.is(tok::comment)) {
        Params.Interpreter.declare(Params.remaining());
        break;
      }

      File = argumentAsString(arg);
    } while (!File.empty() && Result == kCmdSuccess);
  }
  return Result;
}

CommandResult doUCommand(CommandArguments& Params, llvm::StringRef Name) {
  Params.actions().actOnUCommand(Name);
  return kCmdSuccess;
}

// F := 'F' FilePath Comment
// FilePath := AnyString
// AnyString := .*^('\t' Comment)
CommandResult doFCommand(CommandArguments& Params, llvm::StringRef Name) {
#if defined(__APPLE__)
  return Params.actions().actOnFCommand(Name);
#endif
  return kCmdFailure;
}

// T := 'T' FilePath Comment
// FilePath := AnyString
// AnyString := .*^('\t' Comment)
CommandResult doTCommand(CommandArguments& Params) {
  const llvm::StringRef inputFile = Params.nextString();
  if (!inputFile.empty()) {
    const llvm::StringRef outputFile = Params.nextString();
    if (!outputFile.empty()) {
      Params.Interpreter.GenerateAutoloadingMap(inputFile, outputFile);
      return kCmdSuccess;
    }
  }
  return kCmdInvalidSyntax;
}

// XCommand := 'x' FilePath[ArgList] | 'X' FilePath[ArgList]
// FilePath := AnyString
// ArgList := (ExtraArgList) ' ' [ArgList]
// ExtraArgList := AnyString [, ExtraArgList]
CommandResult doXCommand(CommandArguments& Params) {
  llvm::StringRef F = Params.nextString(tok::l_paren|tok::space);
  if (F.empty())
    return kCmdInvalidSyntax;

  // actOnxCommand sorts out the arguments
  return Params.actions().actOnxCommand(F, Params.remaining(), Params.OutValue);
}

CommandResult doDebugCommand(CommandArguments& Params) {
  bool argWasBool;
  llvm::Optional<int> mode = Params.optionalInt(true, &argWasBool);
  clang::CodeGenOptions& CGO = Params.Interpreter.getCI()->getCodeGenOpts();
  if (mode && !argWasBool) {
    const int value = mode.getValue();
    const int NumDebInfos = 5;
    if (value > 0 && value < NumDebInfos) {
      clang::codegenoptions::DebugInfoKind DebInfos[NumDebInfos] = {
        clang::codegenoptions::NoDebugInfo,
        clang::codegenoptions::LocTrackingOnly,
        clang::codegenoptions::DebugLineTablesOnly,
        clang::codegenoptions::LimitedDebugInfo,
        clang::codegenoptions::FullDebugInfo
      };
      CGO.setDebugInfo(DebInfos[value]);
      if (!value)
        Params.Output << "Not generating debug symbols\n";
      else
        Params.Output << "Generating debug symbols level " << value << '\n';
    } else {
      llvm::errs() << "Debug level must be between 0-" << NumDebInfos-1 << "\n";
      return kCmdFailure;
    }
  } else {
    // No mode, but we had an argument, show user how to use the command
    if (!mode && !Params.curArg().is(tok::eof))
      return kCmdInvalidSyntax;

    bool flag = argWasBool ? mode.getValue() :
                       CGO.getDebugInfo() == clang::codegenoptions::NoDebugInfo;
    if (flag)
      CGO.setDebugInfo(clang::codegenoptions::LimitedDebugInfo);
    else
      CGO.setDebugInfo(clang::codegenoptions::NoDebugInfo);
    // FIXME:
    Params.Output << (flag ? "G" : "Not g") << "enerating debug symbols\n";
  }
  return kCmdSuccess;
}

CommandResult doQCommand(CommandArguments& Params) {
  Params.Processor->quit() = true;
  return kCmdSuccess;
}

CommandResult doAtCommand(CommandArguments& Params) {
  Params.Processor->cancelContinuation();
  return kCmdSuccess;
}

CommandResult doICommand(CommandArguments& Params) {
  llvm::StringRef PathArg = Params.nextString();
  if (!PathArg.empty()) {
    do {
      std::string Path = PathArg.str();
      char Delim[2] = {0,0};

      // Get the delimitor, can be specified as '?' or one of our tokens
      const Argument& Arg = Params.nextArg();
      size_t offset = 0;
      switch (Arg.getKind()) {
        case tok::charlit:
          offset = 1;
        default:
          Delim[0] = *(Arg.getBufStart()+offset);
          PathArg = Params.nextString();
          break;
        case tok::slash:
          // So .I /  has the same semantics as .I /Dir/Path
          PathArg = llvm::StringRef("/");
          break;
        case tok::space:
        case tok::stringlit:
        case tok::constant:
        case tok::ident:
        case tok::raw_ident:
        case tok::eof:
          PathArg = argumentAsString(Arg);
          break;
      }
      // PathArg now holds next argument (if any)

      cling::utils::ExpandEnvVars(Path);
      Params.Interpreter.AddIncludePaths(Path, Delim);

    } while(!PathArg.empty());
  }
  else
    Params.Interpreter.DumpIncludePath(&Params.Output);
  return kCmdSuccess;
}

CommandResult doRawInputCommand(CommandArguments& Params) {
  const llvm::Optional<int> mode = Params.optionalInt();
  if (!mode.hasValue()) {
    bool flag = !Params.Interpreter.isRawInputEnabled();
    Params.Interpreter.enableRawInput(flag);
    Params.Output << (flag ? "U" :"Not u") << "sing raw input\n";
  }
  else
    Params.Interpreter.enableRawInput(mode.getValue());
  return kCmdSuccess;
}

CommandResult doPrintDebugCommand(CommandArguments& Params) {
  const llvm::Optional<int> mode = Params.optionalInt();
  if (!mode.hasValue()) {
    bool flag = !Params.Interpreter.isPrintingDebug();
    Params.Interpreter.enablePrintDebug(flag);
    Params.Output << (flag ? "P" : "Not p") << "rinting Debug\n";
  }
  else
    Params.Interpreter.enablePrintDebug(mode.getValue());
  return kCmdSuccess;
}

CommandResult doDynamicExtensionsCommand(CommandArguments& Params) {
  const llvm::Optional<int> mode = Params.optionalInt();
  if (!mode.hasValue()) {
    bool flag = !Params.Interpreter.isDynamicLookupEnabled();
    Params.Interpreter.enableDynamicLookup(flag);
    Params.Output << (flag ? "U" : "Not u") << "sing dynamic extensions\n";
  }
  else
    Params.Interpreter.enableDynamicLookup(mode.getValue());
  return kCmdSuccess;
}

CommandResult doStoreStateCommand(CommandArguments& Params,
                                  llvm::StringRef Name) {
  if (Name.empty())
      return kCmdInvalidSyntax;

  Params.Interpreter.storeInterpreterState(Name);
  return kCmdSuccess;
}

CommandResult
doCompareStateCommand(CommandArguments& Params, llvm::StringRef Name) {
  if (Name.empty())
      return kCmdInvalidSyntax;

  Params.Interpreter.compareInterpreterState(Name);
  return kCmdSuccess;
}

CommandResult doStatsCommand(CommandArguments& Params, llvm::StringRef Name) {
  if (Name.equals("decl")) {
    ClangInternalState::printLookupTables(Params.Output,
      Params.Interpreter.getCI()->getSema().getASTContext());
    return kCmdSuccess;
  }
  else if (Name.equals("ast")) {
    Params.Interpreter.getCI()->getSema().getASTContext().PrintStats();
    return kCmdSuccess;
  } else if (Name.equals("undo")) {
    Params.Interpreter.getIncrParser().printTransactionStructure();
    return kCmdSuccess;
  }
  return kCmdInvalidSyntax;
}

void printLocation(clang::SourceLocation Loc, clang::Sema& Sema,
                   llvm::raw_ostream& Out = llvm::outs()) {
  clang::PresumedLoc PLoc = Sema.getSourceManager().getPresumedLoc(Loc, true);
  if (PLoc.isValid()) {
    Out << PLoc.getFilename() << ", line: " << PLoc.getLine()
                              << ", col: " << PLoc.getColumn() << "\n";
  }
}

CommandResult doDumpDeclCommand(CommandArguments& Params, llvm::StringRef Adr) {
  uintptr_t ptr;
  if (!Adr.getAsInteger(0, ptr)) {
    clang::Decl* D = reinterpret_cast<clang::Decl*>(ptr);
    printLocation(D->getLocation(), Params.Interpreter.getSema(), Params.Output);
    D->dump(Params.Output);
    return kCmdSuccess;
  }
  return kCmdInvalidSyntax;
}


CommandResult doUndoCommand(CommandArguments& Params) {
  const llvm::Optional<int> arg = Params.optionalInt();
  Params.Interpreter.unload(arg.hasValue() ? arg.getValue() : 1);
  return kCmdSuccess;
}

CommandResult doFileExCommand(CommandArguments& Params) {
  clang::CompilerInstance* CI = Params.Interpreter.getCI();

  const clang::SourceManager& SM = CI->getSourceManager();
  SM.getFileManager().PrintStats();

  Params.Output << "\n***\n\n";

  for (clang::SourceManager::fileinfo_iterator I = SM.fileinfo_begin(),
         E = SM.fileinfo_end(); I != E; ++I) {
    Params.Output << (*I).first->getName();
    Params.Output << "\n";
  }

#if 0
  // Need to link to libclangFrontend for this
  llvm::IntrusiveRefCntPtr<clang::ASTReader> Reader = CI->getModuleManager();
  const clang::serialization::ModuleManager& ModMan = Reader->getModuleManager();
  for (clang::serialization::ModuleManager::ModuleConstIterator I
         = ModMan.begin(), E = ModMan.end(); I != E; ++I) {
    for (auto& IFI : (*I)->InputFilesLoaded) {
      Params.Output << IFI.getFile()->getName();
      Params.Output << "\n";
    }
  }
#endif

  return kCmdSuccess;
}

CommandResult doFilesCommand(CommandArguments& Params) {
  Params.Interpreter.printIncludedFiles(Params.Output);
  return kCmdSuccess;
}

CommandResult doNamespaceCommand(CommandArguments& Params) {
  DisplayNamespaces(Params.Output, &Params.Interpreter);
  return kCmdSuccess;
}

CommandResult doClassCommand(CommandArguments& Params, llvm::StringRef Name) {
  if (Name.empty()) {
    const bool verbose = Params.CommandName[0] == 'C';
    DisplayClasses(Params.Output, &Params.Interpreter, verbose);
  } else
    DisplayClass(Params.Output, &Params.Interpreter, Name.str().c_str(), true);
  return kCmdSuccess;
}

CommandResult doGCommand(CommandArguments& Params, llvm::StringRef Name) {
  if (Name.empty())
    DisplayGlobals(Params.Output, &Params.Interpreter);
  else
    DisplayGlobal(Params.Output, &Params.Interpreter, Name.str().c_str());
  return kCmdSuccess;
}

CommandResult doTypedefCommand(CommandArguments& Params, llvm::StringRef Name) {
  if (Name.empty())
    DisplayTypedefs(Params.Output, &Params.Interpreter);
  else
    DisplayTypedef(Params.Output, &Params.Interpreter, Name.str().c_str());
  return kCmdSuccess;
}

CommandResult doOCommand(CommandArguments& Params) {
  // Does nothing
  return kCmdUnimplemented;

  llvm::StringRef cmd = argumentAsString(Params.curArg());
  int             level = -1;
  if (cmd.size() == 1) {
    const Argument &arg = Params.nextArg(tok::constant);
    if (arg.is(tok::constant))
      level = arg.getConstant();
  }
  else if (cmd.substr(1).getAsInteger(10, level) || level < 0)
    return kCmdInvalidSyntax;

  return kCmdSuccess;
  // ### TODO something.... with level and maybe more
  llvm::StringRef fName = Params.nextString();
  if (!fName.empty()) {
    do {
      fName = Params.nextString();
    } while (!fName.empty());
  }
  return kCmdSuccess;
}

// >RedirectCommand := '>' FilePath
// FilePath := AnyString
// AnyString := .*^(' ' | '\t')
CommandResult doRedirectCommand(CommandArguments& Params) {
  unsigned constant_FD = 0;
  // Default redirect is stdout.
  MetaProcessor::RedirectionScope stream = MetaProcessor::kSTDOUT;

  const Argument *arg = &Params.curArg();
  if (arg->is(tok::constant)) {
    // > or 1> the redirection is for stdout stream
    // 2> redirection for stderr stream
    constant_FD = arg->getConstant();
    if (constant_FD == 2) {
      stream = MetaProcessor::kSTDERR;
    // Wrong constant_FD, do not redirect.
    } else if (constant_FD != 1) {
      llvm::errs() << "invalid file descriptor number " << constant_FD <<"\n";
      return kCmdInvalidSyntax;
    }
    arg = &Params.nextArg();
  }

  // &> redirection for both stdout & stderr
  if (arg->is(tok::ampersand)) {
    if (constant_FD == 0) {
      stream = MetaProcessor::kSTDBOTH;
    }
    arg = &Params.nextArg();
  }

  llvm::StringRef file;
  if (arg->is(tok::greater)) {
    bool append = false;
    arg = &Params.nextArg();
    // check whether we have >>
    if (arg->is(tok::greater)) {
      append = true;
      arg = &Params.nextArg();
    }
    // check for syntax like: 2>&1
    if (arg->is(tok::ampersand)) {
      if (constant_FD == 0) {
        stream = MetaProcessor::kSTDBOTH;
      }
      arg = &Params.nextArg();
      if (arg->is(tok::constant)) {
        switch (arg->getConstant()) {
          case 1: file = llvm::StringRef("&1"); break;
          case 2: file = llvm::StringRef("&2"); break;
          default: break;
        }
        if (!file.empty()) {
          // Mark the stream name as refering to stderr or stdout, not a name
          stream = MetaProcessor::RedirectionScope(stream |
                                                   MetaProcessor::kSTDSTRM);
        }
      }
    }
    if (!arg->is(tok::eof) && !(stream & MetaProcessor::kSTDSTRM))
      file = Params.asPath();

    // Empty file means std.
    Params.Processor->setStdStream(file/*file*/,
                                   stream/*which stream to redirect*/,
                                   append/*append mode*/);
    return kCmdSuccess;
  }
  // return kCmdUnimplemented if we only read the first arguments
  return arg == &Params.curArg() ? kCmdUnimplemented : kCmdInvalidSyntax;
}

CommandResult doShellCommand(CommandArguments& Params) {
  llvm::StringRef CommandLine;
  const Argument *Arg = &Params.curArg();
  if (Arg->is(tok::comment))
    CommandLine = Params.asPath();
  else if (Arg->isOneOf(tok::excl_mark|tok::slash))
    CommandLine = Params.remaining();
  else
    return kCmdInvalidSyntax;

  CommandLine = CommandLine.trim();
  if (!CommandLine.empty()) {
    int Result = std::system(CommandLine.str().c_str());

    // Build the result
    clang::ASTContext& Ctx = Params.Interpreter.getCI()->getASTContext();
    if (Params.OutValue) {
      *Params.OutValue = Value(Ctx.IntTy, Params.Interpreter);
      Params.OutValue->getAs<long long>() = Result;
    }
    return (Result == 0) ? kCmdSuccess : kCmdFailure;
  }
  return kCmdFailure;
}

CommandResult doSourceCommand(CommandArguments& Params) {
  Params.Interpreter.getIncrParser().dump(Params.Output);
  return kCmdSuccess;
}

} // anonymous namespace

struct Commands::CommandObj {
  // Internal flags, make sure 'Flags' has enough bits
  enum {
    kCmdBuiltin   = 8,
    kCmdCallback1 = 16,
  };

  union CommandCallback {
    CmdCallback0 Callback0;
    CmdCallback1 Callback1;
    CommandCallback() : Callback0(nullptr) {}
  } Callback;
  const char* Syntax;
  const char* Help;
  unsigned Flags : 5;
  
  // llvm::StringMap needs this
  CommandObj() : Syntax(nullptr), Help(nullptr), Flags(0) {}
};

namespace cling {
 namespace meta {
  namespace {
  
  // Commands implementation
  class CommandTable : public Commands {

    llvm::StringMap<CommandObj*> m_Commands;
    bool m_HasBuiltins;

    static bool sort(const llvm::StringMap<CommandObj*>::iterator& A,
                     const llvm::StringMap<CommandObj*>::iterator& B) {
      const CommandObj* LHS = A->second, * RHS = B->second;

      // Sort by group: builtin, custom, debug
      if (LHS->Flags & kCmdDebug) {
        if (!(RHS->Flags & kCmdDebug))
          return false;
      } else if (RHS->Flags & kCmdDebug)
        return true;
      else if (LHS->Flags & CommandObj::kCmdBuiltin) {
        if (!(RHS->Flags & CommandObj::kCmdBuiltin))
          return true;
      } else if (RHS->Flags & CommandObj::kCmdBuiltin)
        return false;

      // Sorth alphabetically, with alphanumeric chars first
      const llvm::StringRef NameA = A->first(), NameB = B->first();
      if (::isalpha(NameA[0])) {
        if (!::isalpha(NameB[0]))
          return true;
      } else if (::isalpha(NameB[0]))
        return false;

      if (NameA.size() == NameB.size())
        return NameA.compare_lower(NameB) == -1;

      return NameA.size() < NameB.size();
    }

    bool checkContext(const CommandObj* Cmd, MetaProcessor* Mp) {
      return Cmd->Flags & kCmdRequireProcessor ? Mp != nullptr : true;
    }

    public:

    static void showHelp(const llvm::StringMap<CommandObj*>::iterator& Itr,
                         llvm::raw_ostream& Out) {
      const CommandObj* Cmd = Itr->second;
      const llvm::StringRef CmdName = Itr->first();
      if (Cmd->Syntax) {
        llvm::SmallString<80> Buf;
        llvm::Twine Joined(CmdName, " ");
        Out << llvm::left_justify(Joined.concat(Cmd->Syntax).toStringRef(Buf),
                                                25);
      } else
        Out << llvm::left_justify(CmdName, 25);

      if (Cmd->Help)
        Out << Cmd->Help;

      Out << "\n";
    }

    static CommandResult doHelpCommand(CommandArguments& Params,
                                       llvm::StringRef Cmd) {
      CommandTable& Cmds = static_cast<CommandTable&>(Commands::get());

      bool showAll = false;

      // Filter help for a given command
      if (!Cmd.empty()) {
        // 'help all' has special meaning
        if (!Cmd.equals("all")) {
          llvm::StringMap<CommandObj*>::iterator Itr = Cmds.m_Commands.find(Cmd);
          if (Itr == Cmds.m_Commands.end()) {
            Params.Output << "Command '" << Cmd << "' not found.\n";
            return kCmdNotFound;
          }

          showHelp(Itr, Params.Output);
          return kCmdSuccess;
        } else
          showAll = true;
      }

      // Help preamble
      std::string& Meta = Params.Interpreter.getOptions().MetaString;
      llvm::raw_ostream& Out = Params.Output;
      Params.Output << "\n Cling (C/C++ interpreter) meta commands usage\n"
        " All commands must be preceded by a '" << Meta << "', except\n"
        " for the evaluation statement { }\n" <<
        std::string(80, '=') << "\n" <<
        " Syntax: " << Meta << "Command [arg0 arg1 ... argN]\n"
        "\n";

      // Sort the stringmap alphabetically
      std::vector<llvm::StringMap<CommandObj*>::iterator> sorted;
      sorted.reserve(Cmds.m_Commands.size());

      for (llvm::StringMap<CommandObj*>::iterator Itr = Cmds.m_Commands.begin(),
           End = Cmds.m_Commands.end(); Itr != End; ++Itr) {
        if (showAll || !(Itr->second->Flags&kCmdDebug))
          sorted.push_back(Itr);
      }

      std::sort(sorted.begin(), sorted.end(), &CommandTable::sort);

      for (const auto& CmdPair : sorted) {
        Out << "   " << Meta;
        showHelp(CmdPair, Out);
      }
      Out << "\n";

      return kCmdSuccess;
    }

    CommandResult execute(llvm::StringRef CmdStr, Interpreter &Intp,
                          llvm::raw_ostream& Out,  MetaProcessor* Mp,
                          Value* Val) override {

      if (!m_HasBuiltins) {
        Commands::get(true);
        assert(m_HasBuiltins && "No builtin commands loaded");
      }

      MetaParser Parser(CmdStr);
      CommandArguments CmdArgs(Parser, Intp, Out, Mp, Val);

      llvm::StringMap<CommandObj*>::iterator  CmdItr = m_Commands.find(
                                                           CmdArgs.CommandName),
                                              End = m_Commands.end();

      CommandResult Result = kCmdNotFound;
      if (CmdItr != End) {
        const CommandObj* Cmd = CmdItr->second;
        if (!checkContext(Cmd, Mp)) {
          llvm::errs() << "Command cannot be run in this context\n";
          return kCmdFailure;
        }

        // Callback variation 2, each argument is parsed and sent to the cmd
        // First invocation is allowed to be empty / non-existant argument
        if (Cmd->Flags & Commands::CommandObj::kCmdCallback1) {
          llvm::StringRef Argument = CmdArgs.nextString();
          do {
            Result = Cmd->Callback.Callback1(CmdArgs, Argument);
            Argument = CmdArgs.nextString();
          } while (!Argument.empty() && Result==kCmdSuccess);
          
        } else
          Result = Cmd->Callback.Callback0(CmdArgs);

      } else {
        for (CmdItr = m_Commands.begin(); CmdItr != End; ++CmdItr) {
          auto& CmdPair = *CmdItr;
          const CommandObj* Cmd = CmdPair.second;
          if (Cmd->Flags & kCmdCustomSyntax && checkContext(Cmd, Mp)) {
            Result = Cmd->Callback.Callback0(CmdArgs);
            if (Result < kCmdUnimplemented)
              break;
          }
        }
      }

      if (Result == kCmdInvalidSyntax) {
        if (CmdItr != End)
          showHelp(CmdItr, llvm::errs());
        else
          doHelpCommand(CmdArgs, "");
      }

      return Result;
    }

    bool validate(const char* Name, unsigned Flags, unsigned ChkNot) {
      if (Flags & ChkNot) {
        llvm::errs() << "Illegal flag value: " << Flags << "\n";
        return false;
      }

      // Reserved for .help all
      if (!strncmp(Name, "all", 3)) {
        llvm::errs() << "'" << Name << "' cannot be used as a command name\n";
        return false;
      }

      // Only allow builtins to be built from this file
      if (m_HasBuiltins && (Flags & CommandObj::kCmdBuiltin)) {
        llvm::errs() << "Cannot add builtin '" << Name << "'\n";
        return false;
      }

      // And don't let them be replaced
      llvm::StringMap<CommandObj*>::iterator CmdItr = m_Commands.find(Name);
      if (CmdItr != m_Commands.end()) {
        const CommandObj* Cmd = CmdItr->second;
        if (Cmd->Flags & CommandObj::kCmdBuiltin) {
          llvm::errs() << "Cannot replace builtin command '" << Name << "'\n";
          return false;
        }
      }
      return true;
    }

    CommandObj*& operator[] (llvm::StringRef Name) { return m_Commands[Name]; }
    bool& builtins()  { return m_HasBuiltins; }

    ~CommandTable() {
      // Any object in m_Commands is not neccessarily unique
      std::set<CommandObj*> done;
      for (auto& CmdPair : m_Commands) {
        CommandObj* Cmd = CmdPair.second;
        if (done.insert(Cmd).second)
          delete Cmd;
      }
    }
  };

} // anonymous namespace


Commands::CommandObj*
Commands::alias(const char* Name, CommandObj* Cmd) {
  if (Cmd) {
    CommandObj*& Old = (*static_cast<CommandTable*>(this))[Name];
    if (Old && Old != Cmd) {
      // If a custom command was registered before a builtin one of the same
      // name, report that.
      if (!(Old->Flags & CommandObj::kCmdBuiltin) &&
            Cmd->Flags & CommandObj::kCmdBuiltin) {
        llvm::errs() << "Custom command '" << Name
                     << "' is being replaced with a builtin\n";
      } else
        llvm::outs() << "Replaced command '" << Name << "'\n";

      delete Old;
    }
    Old = Cmd;
  }
  return Cmd;
}

template <> CommandTable::CommandObj*
Commands::add<Commands::CmdCallback0>(const char* Name, CmdCallback0 Callback,
                                      const char* Syntax, const char* Help,
                                      unsigned Flags) {

  if (!static_cast<CommandTable*>(this)->validate(Name, Flags,
                                                  CommandObj::kCmdCallback1)) {
    return nullptr;
  }

  if (CommandObj* Cmd = new CommandObj) {
    Cmd->Callback.Callback0 = Callback;
    Cmd->Syntax = Syntax;
    Cmd->Help = Help;
    Cmd->Flags = Flags;
    return alias(Name, Cmd);
  }
  return nullptr;
}

template <> CommandTable::CommandObj*
Commands::add<Commands::CmdCallback1>(const char* Name, CmdCallback1 Callback,
                                      const char* Syntax, const char* Help,
                                      unsigned Flags) {

  if (!static_cast<CommandTable*>(this)->validate(Name, Flags,
                                                  kCmdCustomSyntax)) {
    return nullptr;
  }

  if (CommandObj* Cmd = new CommandObj) {
    Cmd->Callback.Callback1 = Callback;
    Cmd->Syntax = Syntax;
    Cmd->Help = Help;
    Cmd->Flags = Flags | CommandObj::kCmdCallback1;
    return alias(Name, Cmd);
  }
  return nullptr;
}

Commands& Commands::get(bool Populate) {
  static CommandTable sCommands;
  if (Populate && !sCommands.builtins()) {
    const unsigned Flags = CommandObj::kCmdBuiltin;
    const unsigned MetaProcessor = Flags | kCmdRequireProcessor;
    const unsigned Debug = Flags | kCmdDebug;

    sCommands.add("L", &doLCommand, "<file|library> [//]",
                  "Load the given file(s) executing the last comment if given",
                  MetaProcessor);

    sCommands.alias("x",
      sCommands.add("X", &doXCommand, "<filename> [args]",
        "Same as .L and runs a function with signature: "
        "ret_type filename(args)", MetaProcessor));

    sCommands.add("U", &doUCommand, "<library>", "Unloads the given file",
                  MetaProcessor);

    sCommands.add("F", &doFCommand, "<framework>", "Load the given framework",
                  MetaProcessor);

    sCommands.add("q", &doQCommand, nullptr, "Exit the program", MetaProcessor);

    sCommands.add("@", &doAtCommand, nullptr,
                  "Cancels and ignores the multiline input", MetaProcessor);

    sCommands.add(">", &doRedirectCommand, "<filename>",
      "Redirect command to a given file\n"
      "      '>' or '1>'\t\t- Redirects the stdout stream only\n"
      "      '2>'\t\t\t- Redirects the stderr stream only\n"
      "      '&>' (or '2>&1')\t\t- Redirects both stdout and stderr\n"
      "      '>>'\t\t\t- Appends to the given file",
                MetaProcessor | kCmdCustomSyntax);

    sCommands.add("I", &doICommand, "[path]",
      "Add give path to list of header search paths,"
      " or show the include paths if none is given.", Flags);

    sCommands.alias("?",
      sCommands.add("help", &CommandTable::doHelpCommand,
                    nullptr, "Shows this information", Flags));
  
    sCommands.add("T", doTCommand, "<infile> <outfile>",
                  "Generate autoloading map from 'infile' to 'outfile'", Flags);

    sCommands.alias("/", sCommands.add("!", doShellCommand, "<cmd> [args]",
                             "Run shell command", Flags));

    sCommands.add("undo", &doUndoCommand, "[n]",
                  "Unloads the last 'n' inputs lines", Flags);

    sCommands.add("rawInput", &doRawInputCommand, "[0|1]",
      "Toggle wrapping and printing the execution results of the input", Flags);

    sCommands.alias("Class",
      sCommands.add("class", &doClassCommand, "<name>",
        "Prints out class <name> in a CINT-like style", Flags));

    sCommands.add("dynamicExtensions", &doDynamicExtensionsCommand, "[0|1]",
      "Toggles the use of the dynamic scopes and the late binding", Flags);

    sCommands.add("O", &doOCommand, "<level>",
      "Sets the optimization level (0-3) (not yet implemented)", Flags);

    sCommands.add("files", &doFilesCommand, nullptr,
      "Prints out some CINT-like file statistics", Debug);

    sCommands.add("filesEx", &doFileExCommand, nullptr,
      "Prints out some file statistics", Debug);

    sCommands.add("g", &doGCommand, "[name]",
      "Prints out information about global variable"
      " 'name' - if no name is given, print them all", Debug);

    sCommands.add("printDebug", &doPrintDebugCommand, "[0|1]",
      "Toggles the printing of input's corresponding"
      "\n\t\t\t\t  state changes", Debug);

    sCommands.add("storeState", &doStoreStateCommand, "<filename>",
      "Store the interpreter's state to a given file", Debug);

    sCommands.add("compareState", &doCompareStateCommand, "<filename>",
      "Compare the interpreter's state with the one saved in a given file",
      Debug);

    sCommands.add("stats", &doStatsCommand, "<name>",
      "Show stats for internal data structures"
      "\n\t\t\t\t  'ast'  abstract syntax tree stats"
      "\n\t\t\t\t  'decl' dump ast declarations"
      "\n\t\t\t\t  'undo' show undo stack", Debug);


    sCommands.add("debug", doDebugCommand, "[level|true|false]",
                  "Generate debug information at level given", Debug);

    sCommands.add("namespace", doNamespaceCommand, nullptr, nullptr, Debug);
    sCommands.add("typedef", doTypedefCommand, nullptr, nullptr, Debug);

    sCommands.add("ddump", doDumpDeclCommand, "<address>",
                  "reinterpret_cast<Clang::Decl*>(address)->dump()", Debug);

    sCommands.add("source", doSourceCommand, nullptr,
                  "Show interpreter source");

    sCommands.builtins() = true;
  }
  return static_cast<Commands&>(sCommands);
}

 } // namespace meta
} // namespace cling


