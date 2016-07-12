//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vvasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "MetaParser.h"
#include "MetaLexer.h"
#include "MetaSema.h"

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/InvocationOptions.h"
#include "cling/Interpreter/Value.h"

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

namespace cling {

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

class MetaParser::CommandParamters
{
  MetaParser             &m_Parser;

  const Token& skipToNextToken() {
    m_Parser.consumeToken();
    m_Parser.skipWhitespace();
    return m_Parser.getCurTok();
  }
  
public:
  MetaSema               &execute;
  MetaSema::ActionResult &result;
  Value                  *value;

  CommandParamters(MetaParser &p, MetaSema &s, MetaSema::ActionResult &r, Value *v) :
    m_Parser(p), execute(s), result(r), value(v) {
      // Assume success; some actions don't set it.
    result = MetaSema::AR_Success;
      if (value) *value = Value();
  }

  const Argument&  cmdArg ()     { return m_Parser.getCurTok(); }
  llvm::StringRef  commandName() { return cmdArg().getIdent(); }

  const Argument&  nextArg(unsigned tk) {
    m_Parser.consumeAnyStringToken(tok::TokenKind(tk));
    return m_Parser.getCurTok();
  }

  llvm::StringRef  remaining()   { return argumentAsString(nextArg(tok::eof)); }
  const Argument&  nextArg()     { return skipToNextToken(); }
  llvm::StringRef  nextString()  { return argumentAsString(nextArg(tok::space)); }
  bool             hadMore()     { return nextArg(tok::eof).is(tok::raw_ident); }

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

  static bool actOnRemainingArguments(MetaParser::CommandParamters &params,
                                void (MetaSema::*action)(llvm::StringRef) const,
                                bool canBeEmpty = true) {
    llvm::StringRef tName = params.nextString();
    if (!tName.empty()) {
      do {
        (params.execute.*action)(tName);
        tName = params.nextString();
      } while (!tName.empty());
    } else if (!canBeEmpty)
      return false;
    else
      (params.execute.*action)(tName);

    return true;
  }

  static bool actOnRemainingArguments(MetaParser::CommandParamters &params,
                  MetaSema::ActionResult (MetaSema::*action)(llvm::StringRef)) {
    llvm::StringRef tName = params.nextString();
    if (tName.empty())
      return false;

    do {
      params.result =(params.execute.*action)(tName);
      tName = params.nextString();
    } while (!tName.empty() && params.result == MetaSema::AR_Success);

    return true;
  }

  static bool doLCommand(MetaParser::CommandParamters& params) {
    llvm::StringRef file =
                      argumentAsString(params.nextArg(tok::comment|tok::space));
    if (file.empty())
        return false;  // TODO: Some fine grained diagnostics

    do {
      params.result = params.execute.actOnLCommand(file);

      const Argument &arg = params.nextArg(tok::comment|tok::space);
      if (arg.is(tok::comment)) {
        params.execute.actOnComment(params.remaining());
        break;
      }

      file = argumentAsString(arg);
    } while (!file.empty() && params.result == MetaSema::AR_Success);

    return true;
  }

  static bool doUCommand(MetaParser::CommandParamters& params) {
    return actOnRemainingArguments(params, &MetaSema::actOnUCommand);
  }

  // F := 'F' FilePath Comment
  // FilePath := AnyString
  // AnyString := .*^('\t' Comment)
  static bool doFCommand(MetaParser::CommandParamters& params) {
#if defined(__APPLE__)
    return actOnRemainingArguments(params, &MetaSema::actOnFCommand);
#endif
    return false;
  }

  // T := 'T' FilePath Comment
  // FilePath := AnyString
  // AnyString := .*^('\t' Comment)
  static bool doTCommand(MetaParser::CommandParamters& params) {
    const llvm::StringRef inputFile = params.nextString();
    if (!inputFile.empty()) {
      const llvm::StringRef outputFile = params.nextString();
      if (!outputFile.empty()) {
        params.result = params.execute.actOnTCommand(inputFile, outputFile);
        return true;
      }
    }
    // TODO: Some fine grained diagnostics
    return false;
  }

  // >RedirectCommand := '>' FilePath
  // FilePath := AnyString
  // AnyString := .*^(' ' | '\t')
  static bool doRedirectCommand(MetaParser::CommandParamters& params) {

    unsigned constant_FD = 0;
    // Default redirect is stdout.
    MetaProcessor::RedirectionScope stream = MetaProcessor::kSTDOUT;

    const Argument *arg = &params.cmdArg();
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
      arg = &params.nextArg();
    }
    // &> redirection for both stdout & stderr
    if (arg->is(tok::ampersand)) {
      if (constant_FD != 2) {
        stream = MetaProcessor::kSTDBOTH;
      }
      arg = &params.nextArg();
    }
    if (arg->is(tok::greater)) {
      bool append = false;
      arg = &params.nextArg();
      // check for syntax like: 2>&1
      if (arg->is(tok::ampersand)) {
        if (constant_FD != 2) {
          stream = MetaProcessor::kSTDBOTH;
        }
        arg = &params.nextArg();
      } else {
        // check whether we have >>
        if (arg->is(tok::greater)) {
          append = true;
          arg = &params.nextArg();
        }
      }
      llvm::StringRef file;
      if (arg->is(tok::constant)) {
        if (arg->getConstant() != 1)
          return false;
        file = llvm::StringRef("_IO_2_1_stdout_");
      } else
      file = argumentAsString(*arg);

      // Empty file means std.
      params.result =
          params.execute.actOnRedirectCommand(file/*file*/,
                                          stream/*which stream to redirect*/,
                                          append/*append mode*/);
      return true;
    }
    return false;
  }

  // XCommand := 'x' FilePath[ArgList] | 'X' FilePath[ArgList]
  // FilePath := AnyString
  // ArgList := (ExtraArgList) ' ' [ArgList]
  // ExtraArgList := AnyString [, ExtraArgList]
  static bool doXCommand(MetaParser::CommandParamters& params) {
    // There might be ArgList
    llvm::StringRef file = argumentAsString(params.nextArg(tok::l_paren));
    // '(' to end of string:

    std::string args = params.nextArg().getBufStart();
    if (args.empty())
      args = "()";
    params.result = params.execute.actOnxCommand(file, args, params.value);
    return true;
  }
  
  static bool doQCommand(MetaParser::CommandParamters& params) {
    params.execute.actOnqCommand();
    return true;
  }

  static bool doICommand(MetaParser::CommandParamters& params) {
    return actOnRemainingArguments(params, &MetaSema::actOnICommand);
  }

  static bool doOCommand(MetaParser::CommandParamters& params) {
    llvm::StringRef cmd = params.commandName();
    int             level = -1;
    if (cmd.size() == 1) {
      const Argument &arg = params.nextArg(tok::constant);
      if (arg.is(tok::constant))
        level = arg.getConstant();
    }
    else if (cmd.substr(1).getAsInteger(10, level) || level < 0)
      return false;

    return true;
    // ### TODO something.... with level and maybe more
    llvm::StringRef fName = params.nextString();
    if (!fName.empty()) {
      do {
        fName = params.nextString();
      } while (!fName.empty());
    }
    return true;
  }

  static bool doAtCommand(MetaParser::CommandParamters& params) {
    // consumeToken();
    // skipWhitespace();
    params.execute.actOnAtCommand();
    return true;
  }
  
  static bool doRawInputCommand(MetaParser::CommandParamters& params) {
    params.execute.actOnrawInputCommand(params.modeToken());
    return true;
  }

  static bool doDebugCommand(MetaParser::CommandParamters& params) {
    params.execute.actOndebugCommand(params.optionalInt());
    return true;
  }

  static bool doPrintDebugCommand(MetaParser::CommandParamters& params) {
    params.execute.actOnprintDebugCommand(params.modeToken());
    return true;
  }

  static bool doStoreStateCommand(MetaParser::CommandParamters& params) {
    const llvm::StringRef name = params.nextString();
    if (name.empty())
        return false; // FIXME: Issue proper diagnostics

    params.execute.actOnstoreStateCommand(name);
    return true;
  }

  static bool doCompareStateCommand(MetaParser::CommandParamters& params) {
    return actOnRemainingArguments(params, &MetaSema::actOncompareStateCommand,
                                   false);
  }

  static bool doStatsCommand(MetaParser::CommandParamters& params) {
    return actOnRemainingArguments(params, &MetaSema::actOnstatsCommand, false);
  }

  static bool doUndoCommand(MetaParser::CommandParamters& params) {
    llvm::Optional<int> arg = params.optionalInt();
    if (arg.hasValue())
      params.execute.actOnUndoCommand(arg.getValue());
    else
      params.execute.actOnUndoCommand();
    return true;
  }

  static bool doDynamicExtensionsCommand(MetaParser::CommandParamters& params) {
    params.execute.actOndynamicExtensionsCommand(params.modeToken());
    return true;
  }
  
  static bool doHelpCommand(MetaParser::CommandParamters& params) {
    params.execute.actOnhelpCommand();
    return true;
  }

  static bool doFileExCommand(MetaParser::CommandParamters& params) {
    params.execute.actOnfileExCommand();
    return true;
  }
  
  static bool doFilesCommand(MetaParser::CommandParamters& params) {
    params.execute.actOnfilesCommand();
    return true;
  }

  static bool doNamespaceCommand(MetaParser::CommandParamters& params) {
    if (params.hadMore())
      return false;

    params.execute.actOnNamespaceCommand();
    return true;
  }
  
  static bool doClassCommand(MetaParser::CommandParamters& params) {
    if (params.commandName().startswith("c"))
      return actOnRemainingArguments(params, &MetaSema::actOnclassCommand);

    params.execute.actOnClassCommand();
    return true;
  }

  static bool doGCommand(MetaParser::CommandParamters& params) {
    return actOnRemainingArguments(params, &MetaSema::actOngCommand);
  }

  static bool doTypedefCommand(MetaParser::CommandParamters& params) {
    return actOnRemainingArguments(params, &MetaSema::actOnTypedefCommand);
  }
  
  static bool doShellCommand(MetaParser::CommandParamters& params) {
    llvm::StringRef commandLine;
    const Argument *arg = &params.cmdArg();
    if (!arg->is(tok::slash)) {
      arg = &params.nextArg();
      commandLine = arg->is(tok::slash) ? arg->getBufStart() :
                                          argumentAsString(*arg);
    }
    else
      commandLine = arg->getBufStart();

    if (commandLine.empty())
      return false;

    params.result = params.execute.actOnShellCommand(commandLine, params.value);
    return true;
  }
}

  MetaParser::MetaParser(llvm::StringRef input, MetaSema& Actions) :
    m_Lexer(input), m_Actions(Actions) {
  }

  void MetaParser::consumeToken() {
    if (m_TokenCache.size())
      m_TokenCache.erase(m_TokenCache.begin());

    lookAhead(0);
  }

  void MetaParser::consumeAnyStringToken(tok::TokenKind stopAt/*=tok::space*/) {
    consumeToken();
    // we have to merge the tokens from the queue until we reach eof token or
    // space token
    skipWhitespace();
    // Add the new token in which we will merge the others.
    Token& MergedTok = m_TokenCache.front();

    if (MergedTok.isOneOf(stopAt) || MergedTok.is(tok::eof)
        || MergedTok.is(tok::comment) || MergedTok.is(tok::stringlit))
      return;

    //look ahead for the next token without consuming it
    Token Tok = lookAhead(1);
    Token PrevTok = Tok;
    while (!Tok.isOneOf(stopAt) && Tok.isNot(tok::eof)){
      //MergedTok.setLength(MergedTok.getLength() + Tok.getLength());
      m_TokenCache.erase(m_TokenCache.begin() + 1);
      PrevTok = Tok;
      //look ahead for the next token without consuming it
      Tok = lookAhead(1);
    }
    MergedTok.setKind(tok::raw_ident);
    if (PrevTok.is(tok::space)) {
      // for "id <space> eof" the merged token should contain "id", not
      // "id <space>".
      Tok = PrevTok;
    }
    MergedTok.setLength(Tok.getBufStart() - MergedTok.getBufStart());
  }

  const Token& MetaParser::lookAhead(unsigned N) {
    if (N < m_TokenCache.size())
      return m_TokenCache[N];

    for (unsigned C = N+1 - m_TokenCache.size(); C > 0; --C) {
      m_TokenCache.push_back(Token());
      m_Lexer.Lex(m_TokenCache.back());
    }
    return m_TokenCache.back();
  }

  void MetaParser::skipWhitespace() {
    while(getCurTok().is(tok::space))
      consumeToken();
  }
  
  bool MetaParser::doMetaCommand(MetaSema::ActionResult& actionResult,
                                 Value* resultValue) {

    CommandParamters params(*this, m_Actions, actionResult, resultValue);

    const Token &tok = getCurTok();
    switch (tok.getKind()) {
      case tok::ident: {
        const llvm::StringRef ident = getCurTok().getIdent();
        // .X commands
        switch (ident.size()==1 ? ident[0] : 0) {
          case 'g':
            return doGCommand(params);
          case 'F':
            return doFCommand(params);
          case 'I':
            return doICommand(params);
          case 'L':
            return doLCommand(params);
          case 'O':
            return doOCommand(params);
          case 'T':
            return doTCommand(params);
          case 'U':
            return doUCommand(params);

          case 'q':
          case 'Q':
            return doQCommand(params);
          case 'x':
          case 'X':
            return doXCommand(params);
        
          default:
            break;
        }

        // string commands
        if (ident.equals("rawInput"))
            return doRawInputCommand(params);
        else if (ident.equals("help"))
          return doHelpCommand(params);
        else if (ident.equals("undo"))
          return doUndoCommand(params);
        else if (ident.startswith("O"))
          return doOCommand(params);
        else if (ident.equals("include"))
          return doICommand(params);
        else if (ident.equals("files"))
          return doFilesCommand(params);
        else if (ident.equals("fileEx"))
          return doFileExCommand(params);
        else if (ident.equals("namespace"))
          return doNamespaceCommand(params);
        else if (ident.equals("typedef"))
          return doTypedefCommand(params);
        else if (ident.equals("class") || ident.equals("Class"))
          return doClassCommand(params);

        else if (ident.equals("debug"))
          return doDebugCommand(params);
        else if (ident.equals("dynamicExtensions"))
          return doDynamicExtensionsCommand(params);
        else if (ident.equals("printDebug"))
          return doPrintDebugCommand(params);
        else if (ident.equals("stats"))
          return doStatsCommand(params);
        else if (ident.equals("storeState"))
          return doStoreStateCommand(params);
        else if (ident.equals("compareState"))
          return doCompareStateCommand(params);

        return false;
      }

      case tok::quest_mark:
      return doHelpCommand(params);
      case tok::at:
        return doAtCommand(params);

      case tok::excl_mark:
      case tok::slash:
        return doShellCommand(params);

      case tok::constant:
      case tok::ampersand:
      case tok::greater:
        return doRedirectCommand(params);

      default:
        break;
    }
    return false;
  }

} // end namespace cling
