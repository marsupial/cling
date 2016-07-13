//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author: Jermaine Dupris <support@greyangle.com>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "CommandTable.h"
#include "MetaParser.h"
#include "MetaSema.h"
#include "cling/MetaProcessor/MetaProcessor.h"
#include "cling/Interpreter/Value.h"
#include "llvm/Support/Format.h"

namespace cling {
namespace meta {

typedef Token Argument;

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

class CommandArguments
{
  MetaParser m_Parser;

  const Token& skipToNextToken() {
    m_Parser.consumeToken();
    m_Parser.skipWhitespace();
    return m_Parser.getCurTok();
  }
  
public:
  const MetaProcessor *Processor; // Can be null!
  MetaSema::ActionResult Result;
  Value* OutValue;

  CommandArguments(llvm::StringRef Cmd, const MetaProcessor* Pr, Value* V) :
    m_Parser(Cmd), Processor(Pr), Result(MetaSema::AR_Success), OutValue(V) {
    if (OutValue) *OutValue = Value();
  }

  const Argument& nextArg(unsigned tk) {
    m_Parser.consumeAnyStringToken(tok::TokenKind(tk));
    return m_Parser.getCurTok();
  }
  llvm::Optional<int> optionalInt() {
    llvm::Optional<int> value ;
    const Argument      &arg = nextArg();
    if (arg.is(tok::constant))
      value = arg.getConstant();
    else if (arg.is(tok::ident)) {
      int tmp;
      if (!arg.getIdent().getAsInteger(10, tmp))
        value = tmp;
    }
    return value;
  }

  MetaSema::SwitchMode modeToken() {
    llvm::Optional<int> mode = optionalInt();
    return mode.hasValue() ? MetaSema::SwitchMode(mode.getValue()) : MetaSema::kToggle;
  }

  const Argument&  curArg ()     { return m_Parser.getCurTok(); }
  llvm::StringRef  remaining()   { return argumentAsString(nextArg(tok::eof)); }
  const Argument&  nextArg()     { return skipToNextToken(); }
  llvm::StringRef  nextString()  { return argumentAsString(nextArg(tok::space));}
  bool             hadMore()     { return nextArg(tok::eof).is(tok::raw_ident); }

  MetaSema& actions() const {
    assert(Processor && "MetaProcessor not available");
    return Processor->getActions();
  }
};

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

static bool doLCommand(CommandArguments& Params) {
  llvm::StringRef File =
                    argumentAsString(Params.nextArg(tok::comment|tok::space));
  if (File.empty())
      return false;  // TODO: Some fine grained diagnostics

  do {
    Params.Result = Params.actions().actOnLCommand(File);

    const Argument &arg = Params.nextArg(tok::comment|tok::space);
    if (arg.is(tok::comment)) {
      Params.actions().actOnComment(Params.remaining());
      break;
    }

    File = argumentAsString(arg);
  } while (!File.empty() && Params.Result == MetaSema::AR_Success);

  return true;
}

static bool doUCommand(CommandArguments& Params, llvm::StringRef Name) {
  Params.Result = Params.actions().actOnUCommand(Name);
  return true;
}

// F := 'F' FilePath Comment
// FilePath := AnyString
// AnyString := .*^('\t' Comment)
static bool doFCommand(CommandArguments& Params, llvm::StringRef Name) {
#if defined(__APPLE__)
  Params.Result = Params.actions().actOnFCommand(Name);
  return true;
#endif
  return false;
}

// T := 'T' FilePath Comment
// FilePath := AnyString
// AnyString := .*^('\t' Comment)
static bool doTCommand(CommandArguments& Params) {
  const llvm::StringRef inputFile = Params.nextString();
  if (!inputFile.empty()) {
    const llvm::StringRef outputFile = Params.nextString();
    if (!outputFile.empty()) {
      Params.Result = Params.actions().actOnTCommand(inputFile, outputFile);
      return true;
    }
  }
  // TODO: Some fine grained diagnostics
  return false;
}

// XCommand := 'x' FilePath[ArgList] | 'X' FilePath[ArgList]
// FilePath := AnyString
// ArgList := (ExtraArgList) ' ' [ArgList]
// ExtraArgList := AnyString [, ExtraArgList]
static bool doXCommand(CommandArguments& Params) {
  llvm::StringRef F = argumentAsString(Params.nextArg(tok::l_paren|tok::space));
  if (F.empty())
    return false;

  // '(' to end of string:
  llvm::StringRef Args = Params.remaining();
  if (Args.empty())
    Args = "()";
  Params.Result = Params.actions().actOnxCommand(F, Args, Params.OutValue);
  return true;
}

static bool doQCommand(CommandArguments& Params) {
  Params.actions().actOnqCommand();
  return true;
}

static bool doICommand(CommandArguments& Params, llvm::StringRef Argument) {
  Params.actions().actOnICommand(Argument);
  return true;
}

static bool doAtCommand(CommandArguments& Params) {
  Params.actions().actOnAtCommand();
  return true;
}

static bool doRawInputCommand(CommandArguments& Params) {
  Params.actions().actOnrawInputCommand(Params.modeToken());
  return true;
}

static bool doDebugCommand(CommandArguments& Params) {
  Params.actions().actOndebugCommand(Params.optionalInt());
  return true;
}

static bool doPrintDebugCommand(CommandArguments& Params) {
  Params.actions().actOnprintDebugCommand(Params.modeToken());
  return true;
}

static bool doStoreStateCommand(CommandArguments& Params) {
  const llvm::StringRef name = Params.nextString();
  if (name.empty())
      return false; // FIXME: Issue proper diagnostics

  Params.actions().actOnstoreStateCommand(name);
  return true;
}

static bool
doCompareStateCommand(CommandArguments& Params, llvm::StringRef Name) {
  if (Name.empty())
    return false;
  Params.actions().actOncompareStateCommand(Name);
  return true;
}

static bool doStatsCommand(CommandArguments& Params, llvm::StringRef Name) {
  if (Name.empty())
    return false;
  Params.actions().actOnstatsCommand(Name);
  return true;
}

static bool doUndoCommand(CommandArguments& Params) {
  llvm::Optional<int> arg = Params.optionalInt();
  if (arg.hasValue())
    Params.actions().actOnUndoCommand(arg.getValue());
  else
    Params.actions().actOnUndoCommand();
  return true;
}

static bool doDynamicExtensionsCommand(CommandArguments& Params) {
  Params.actions().actOndynamicExtensionsCommand(Params.modeToken());
  return true;
}

static bool doFileExCommand(CommandArguments& Params) {
  Params.actions().actOnfileExCommand();
  return true;
}

static bool doFilesCommand(CommandArguments& Params) {
  Params.actions().actOnfilesCommand();
  return true;
}

static bool doNamespaceCommand(CommandArguments& Params) {
  if (Params.hadMore())
    return false;

  Params.actions().actOnNamespaceCommand();
  return true;
}

static bool doClassCommand(CommandArguments& Params, llvm::StringRef Name) {
  if (Name.empty())
    Params.actions().actOnClassCommand();
  else
    Params.actions().actOnclassCommand(Name);
  return true;
}

static bool doGCommand(CommandArguments& Params, llvm::StringRef Name) {
  Params.actions().actOngCommand(Name);
  return true;
}

static bool doTypedefCommand(CommandArguments& Params, llvm::StringRef Name) {
  Params.actions().actOnTypedefCommand(Name);
  return true;
}

static bool doOCommand(CommandArguments& Params) {
  llvm::StringRef cmd = argumentAsString(Params.curArg());
  int             level = -1;
  if (cmd.size() == 1) {
    const Argument &arg = Params.nextArg(tok::constant);
    if (arg.is(tok::constant))
      level = arg.getConstant();
  }
  else if (cmd.substr(1).getAsInteger(10, level) || level < 0)
    return false;

  return true;
  // ### TODO something.... with level and maybe more
  llvm::StringRef fName = Params.nextString();
  if (!fName.empty()) {
    do {
      fName = Params.nextString();
    } while (!fName.empty());
  }
  return true;
}

static llvm::StringRef rawPath(const Argument& Arg) {
  if (Arg.is(tok::slash))
    return Arg.getBufStart();
  else if (Arg.is(tok::comment))
    return Arg.getBufStart() + 1;
  return argumentAsString(Arg);
}

// >RedirectCommand := '>' FilePath
// FilePath := AnyString
// AnyString := .*^(' ' | '\t')
static bool doRedirectCommand(CommandArguments& Params) {

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
      llvm::errs() << "cling::MetaParser::isRedirectCommand():"
                   << "invalid file descriptor number " << constant_FD <<"\n";
      return true;
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
      file = rawPath(*arg);

    // Empty file means std.
    Params.Result =
        Params.actions().actOnRedirectCommand(file/*file*/,
                                              stream/*stream to redirect*/,
                                              append/*append mode*/);
    return true;
  }
  return false;
}

static bool doShellCommand(CommandArguments& Params) {
  llvm::StringRef CommandLine;
  const Argument *Arg = &Params.curArg();
  if (Arg->is(tok::comment))
    CommandLine = rawPath(*Arg);
  else if (Arg->isOneOf(tok::excl_mark|tok::slash))
    CommandLine = Params.remaining();

  if (CommandLine.empty())
    return false;

  Params.Result = Params.actions().actOnShellCommand(CommandLine, Params.OutValue);
  return true;
}
}

CommandTable::~CommandTable() {
  std::set<CommandObj*> done;
  for (auto& CmdPair : m_Commands) {
    CommandObj* Cmd = CmdPair.second;
    if (done.insert(Cmd).second)
      delete Cmd;
  }
}

CommandTable::CommandObj*
CommandTable::add(const char* Name, CommandObj* Cmd) {
  if (Cmd) {
    CommandObj*& Old = m_Commands[Name];
    if (Old && Old != Cmd)
      delete Old;
    Old = Cmd;
  }
  return Cmd;
}

template <> CommandTable::CommandObj*
CommandTable::add<CommandTable::CommandObj::CommandCallback0>(
    const char* Name, CommandObj::CommandCallback0 Callback, const char* Syntax,
    const char* Help, unsigned Flags) {
  assert(!(Flags&kCmdCallback1) && "Cannot set kCmdCallback1 flag");
  if (CommandObj* Cmd = new CommandObj) {
    Cmd->Callback.Callback0 = Callback;
    Cmd->Syntax = Syntax;
    Cmd->Help = Help;
    Cmd->Flags = Flags;
    return add(Name, Cmd);
  }
  return nullptr;
}

template <> CommandTable::CommandObj*
CommandTable::add<CommandTable::CommandObj::CommandCallback1>(
    const char* Name, CommandObj::CommandCallback1 Callback, const char* Syntax,
    const char* Help, unsigned Flags) {
  assert(!(Flags&kCmdCustomSyntax) && "Cannot set kCmdCustomSyntax flag");
  Flags |= kCmdCallback1;
  if (CommandObj* Cmd = new CommandObj) {
    Cmd->Callback.Callback1 = Callback;
    Cmd->Syntax = Syntax;
    Cmd->Help = Help;
    Cmd->Flags = Flags;
    return add(Name, Cmd);
  }
  return nullptr;
}

CommandTable* CommandTable::create() {
  static CommandTable sCommands;
  if (sCommands.m_Commands.empty()) {

    sCommands.add("L", &doLCommand, "<file|library>[//]",
                  "Load the given file(s) executing the last comment if given");

    sCommands.add("x",
      sCommands.add("X", &doXCommand, "<filename>[args]",
        "Same as .L and runs a function with signature: "
        "ret_type filename(args)"));

    sCommands.add(">", &doRedirectCommand, "<filename>",
      "Redirect command to a given file\n"
      "      '>' or '1>'\t\t- Redirects the stdout stream only\n"
      "      '2>'\t\t\t- Redirects the stderr stream only\n"
      "      '&>' (or '2>&1')\t\t- Redirects both stdout and stderr\n"
      "      '>>'\t\t\t- Appends to the given file", kCmdCustomSyntax);

    sCommands.add("U", &doUCommand, "<library>", "Unloads the given file");
    sCommands.add("F", &doFCommand, "<framework>", "Load the given framework");
    sCommands.add("I", &doICommand, "[path]",
      "Add give path to list of header search paths,"
      " or show the include paths if none is given.");

    sCommands.add("O", &doOCommand, "<level>",
      "Sets the optimization level (0-3) (not yet implemented)");

    sCommands.add("undo", &doUndoCommand, "[n]",
      "Unloads the last 'n' inputs lines");

    sCommands.add("rawInput", &doRawInputCommand, "[0|1]",
      "Toggle wrapping and printing the execution results of the input");

    sCommands.add("@", &doAtCommand, nullptr,
      "Cancels and ignores the multiline input");

    sCommands.add("Class",
      sCommands.add("class", &doClassCommand, "<name>",
        "Prints out class <name> in a CINT-like style"));

    sCommands.add("dynamicExtensions", &doDynamicExtensionsCommand, "[0|1]",
      "Toggles the use of the dynamic scopes and the late binding");

    sCommands.add("files", &doFilesCommand, nullptr,
      "Prints out some CINT-like file statistics", kCmdDebug);

    sCommands.add("filesEx", &doFileExCommand, nullptr,
      "Prints out some file statistics", kCmdDebug);

    sCommands.add("g", &doGCommand, "[name]",
      "Prints out information about global variable"
      " 'name' - if no name is given, print them all", kCmdDebug);

    sCommands.add("printDebug", &doPrintDebugCommand, "[0|1]",
      "Toggles the printing of input's corresponding"
      "\n\t\t\t\t  state changes", kCmdDebug);

    sCommands.add("storeState", &doStoreStateCommand, "<filename>",
      "Store the interpreter's state to a given file", kCmdDebug);

    sCommands.add("compareState", &doCompareStateCommand, "<filename>",
      "Compare the interpreter's state with the one saved in a given file",
      kCmdDebug);

    sCommands.add("stats", &doStatsCommand, "<name>",
      "Show stats for internal data structures"
      "\n\t\t\t\t  'ast'  abstract syntax tree stats"
      "\n\t\t\t\t  'decl' dump ast declarations"
      "\n\t\t\t\t  'undo' show undo stack", kCmdDebug);

    sCommands.add("?",
      sCommands.add("help", &doHelpCommand, nullptr, "Shows this information"));

    sCommands.add("q", &doQCommand, nullptr, "Exit the program");
  
    sCommands.add("T", doTCommand);
    sCommands.add("/", sCommands.add("!", doShellCommand, nullptr,
                             "Run shell command", kCmdCustomSyntax));

    sCommands.add("debug", doDebugCommand, nullptr, nullptr, kCmdDebug);
    sCommands.add("namespace", doNamespaceCommand, nullptr, nullptr, kCmdDebug);
    sCommands.add("typedef", doTypedefCommand, nullptr, nullptr, kCmdDebug);
  }
  return &sCommands;
}

bool CommandTable::sort(
                 const llvm::StringMap<CommandTable::CommandObj*>::iterator& A,
                 const llvm::StringMap<CommandTable::CommandObj*>::iterator& B) {

  const CommandObj* LHS = A->second, * RHS = B->second;
  if (RHS->Flags & kCmdExperimental) {
    if (!(LHS->Flags & kCmdExperimental))
      return true;
  }
  else if (RHS->Flags & kCmdDebug) {
    if (!(LHS->Flags & kCmdDebug))
      return true;
  }
  // Sorth alphabetically, with alphanumeric chars first
  const llvm::StringRef NameA = A->first(), NameB = B->first();
  if (!::isalpha(NameA[0])) {
    if (::isalpha(NameB[0]))
      return false;
  }
  return A->first() < B->first();
}

void CommandTable::showHelp(const llvm::StringMap<CommandObj*>::iterator& Itr,
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

bool CommandTable::doHelpCommand(CommandArguments& Params,
                                 llvm::StringRef Cmd) {
  CommandTable* Cmds = CommandTable::create();

  if (!Cmd.empty()) {
    llvm::StringMap<CommandObj*>::iterator itr = Cmds->m_Commands.find(Cmd);
    if (itr != Cmds->m_Commands.end())
      showHelp(itr, Params.Output);
    else
      Params.Output << "Command '" << Cmd << "' not found.\n";
    return true;
  }

  std::vector<llvm::StringMap<CommandObj*>::iterator> sorted;
  sorted.reserve(Cmds->m_Commands.size());

  for (llvm::StringMap<CommandObj*>::iterator itr = Cmds->m_Commands.begin(),
       end = Cmds->m_Commands.end(); itr != end; ++itr)
    sorted.push_back(itr);
  
  std::sort(sorted.begin(), sorted.end(), &CommandTable::sort);

  std::string& Meta = Params.Processor->getInterpreter().getOptions().MetaString;
  llvm::raw_ostream& Out = Params.Processor->getOuts();
  Out << "\n Cling (C/C++ interpreter) meta commands usage\n"
    " All commands must be preceded by a '" << Meta << "', except\n"
    " for the evaluation statement { }\n" <<
    std::string(80, '=') << "\n" <<
    " Syntax: " << Meta << "Command [arg0 arg1 ... argN]\n"
    "\n";

  for (const auto& CmdPair : sorted) {
    Out << "   " << Meta;
    showHelp(CmdPair, Out);
  }
  Out << "\n";
  return true;
}
  
int
CommandTable::execute(llvm::StringRef CmdStr, const MetaProcessor* Mp, Value* Val) {
  CommandTable* Cmds = create();
  if (!Cmds)
    return 0;
  
  CommandArguments CmdArgs(CmdStr, Mp, Val);

  const Argument Arg0 = CmdArgs.curArg();
  llvm::StringMap<CommandObj*>::iterator CmdItr = m_Commands.find(
                         llvm::StringRef(Arg0.getBufStart(), Arg0.getLength()));

  if (CmdItr == Cmds->m_Commands.end()) {
    for (auto& CmdPair : Cmds->m_Commands) {
      const CommandObj* Cmd = CmdPair.second;
      if (Cmd->Flags & kCmdCustomSyntax) {
        if (Cmd->Callback.Callback0(CmdArgs))
          return CmdArgs.Result == MetaSema::AR_Success ? 1 : -1;
      }
    }
    // No command found
    return 0;
  }

  const CommandObj* Cmd = CmdItr->second;
  if (Cmd->Flags & kCmdCallback1) {
    llvm::StringRef Argument = CmdArgs.nextString();
    do {
      if (!Cmd->Callback.Callback1(CmdArgs, Argument)) {
        showHelp(CmdItr, Output);
        return 0;
      }
      Argument = CmdArgs.nextString();
    } while (!Argument.empty());
    
  } else if (!Cmd->Callback.Callback0(CmdArgs)) {
    showHelp(CmdItr, Output);
    return 0;
  }

  return CmdArgs.Result == MetaSema::AR_Success ? 1 : -1;
}

} //namespace meta
} // namespace cling
