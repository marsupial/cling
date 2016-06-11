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

  MetaParser::MetaParser(MetaSema* Actions) : m_Lexer("") {
    m_Actions.reset(Actions);
    const InvocationOptions& Opts = Actions->getInterpreter().getOptions();
    MetaLexer metaSymbolLexer(Opts.MetaString);
    Token Tok;
    while(true) {
      metaSymbolLexer.Lex(Tok);
      if (Tok.is(tok::eof))
        break;
      m_MetaSymbolCache.push_back(Tok);
    }
  }

  void MetaParser::enterNewInputLine(llvm::StringRef Line) {
    m_Lexer.reset(Line);
    m_TokenCache.clear();
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

    if (MergedTok.is(stopAt) || MergedTok.is(tok::eof)
        || MergedTok.is(tok::comment))
      return;

    //look ahead for the next token without consuming it
    Token Tok = lookAhead(1);
    Token PrevTok = Tok;
    while (Tok.isNot(stopAt) && Tok.isNot(tok::eof)){
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
    return isCommandSymbol() && doCommand(actionResult, resultValue);
  }

  bool MetaParser::isQuitRequested() const {
    return m_Actions->isQuitRequested();
  }

  bool MetaParser::isCommandSymbol() {
    for (size_t i = 0; i < m_MetaSymbolCache.size(); ++i) {
      if (getCurTok().getKind() != m_MetaSymbolCache[i].getKind())
        return false;
      consumeToken();
    }
    return true;
  }
  
  bool MetaParser::doCommand(MetaSema::ActionResult& actionResult,
                             Value* resultValue) {
    if (resultValue)
      *resultValue = Value();

    // Assume success; some actions don't set it.
    actionResult = MetaSema::AR_Success;
    const Token &tok = getCurTok();
    switch ( tok.getKind() ) {
      case tok::ident: {
        const llvm::StringRef ident = getCurTok().getIdent();
        // .X commands
        switch ( ident.size()==1 ? ident[0] : 0 ) {
          case 'g':
            return doGCommand();
          case 'F':
            return doFCommand(actionResult);
          case 'I':
            return doICommand();
          case 'L':
            return doLCommand(actionResult);
          case 'T':
            return doTCommand(actionResult);
          case 'U':
            return doUCommand(actionResult);

          case 'q':
          case 'Q':
            return doQCommand();
          case 'x':
          case 'X':
            return doXCommand(actionResult, resultValue);
        
          default:
            break;
        }

        // string commands
        if ( ident.equals("rawInput") )
            return doRawInputCommand();
        else if ( ident.equals("help") )
          return doHelpCommand();
        else if ( ident.equals("undo") )
          return doUndoCommand();
        else if ( ident.startswith("O") )
          return doOCommand();
        else if ( ident.equals("include") )
          return doICommand();

        else if ( ident.equals("class") || ident.equals("Class") )
          return doClassCommand();
        else if ( ident.equals("files") )
          return doFilesCommand();
        else if ( ident.equals("fileEx") )
          return doFileExCommand();
        else if ( ident.equals("namespace") )
          return doNamespaceCommand();
        else if ( ident.equals("typedef") )
          return doTypedefCommand();

        else if ( ident.equals("debug") )
          return doDebugCommand();
        else if ( ident.equals("dynamicExtensions") )
          return doDynamicExtensionsCommand();
        else if ( ident.equals("printDebug") )
          return doPrintDebugCommand();
        else if ( ident.equals("stats") )
          return doStatsCommand();
        else if ( ident.equals("storeState") )
          return doStoreStateCommand();
        else if ( ident.equals("compareState") )
          return doCompareStateCommand();
      }

      case tok::quest_mark:
        return doHelpCommand();
      case tok::at:
        return doAtCommand();
      case tok::excl_mark:
        return doShellCommand(actionResult, resultValue);

      case tok::constant:
      case tok::ampersand:
      case tok::greater:
        return doRedirectCommand(actionResult);

      default:
        break;
    }
    return false;
  }

  // L := 'L' FilePath Comment
  // FilePath := AnyString
  // AnyString := .*^('\t' Comment)
  bool MetaParser::doLCommand(MetaSema::ActionResult& actionResult) {
    bool result = false;
    if (getCurTok().is(tok::ident) && getCurTok().getIdent().equals("L")) {
      consumeAnyStringToken(tok::comment);
      if (getCurTok().is(tok::raw_ident)) {
        result = true;
        actionResult = m_Actions->actOnLCommand(getCurTok().getIdent());
        consumeToken();
        if (getCurTok().is(tok::comment)) {
          consumeAnyStringToken(tok::eof);
          m_Actions->actOnComment(getCurTok().getIdent());
        }
      }
    }
    // TODO: Some fine grained diagnostics
    return result;
  }

  // F := 'F' FilePath Comment
  // FilePath := AnyString
  // AnyString := .*^('\t' Comment)
  bool MetaParser::doFCommand(MetaSema::ActionResult& actionResult) {
    bool result = false;
#if defined(__APPLE__)
    if (getCurTok().is(tok::ident) && getCurTok().getIdent().equals("F")) {
      consumeAnyStringToken(tok::comment);
      if (getCurTok().is(tok::raw_ident)) {
        result = true;
        actionResult = m_Actions->actOnFCommand(getCurTok().getIdent());
        consumeToken();
        if (getCurTok().is(tok::comment)) {
          consumeAnyStringToken(tok::eof);
          m_Actions->actOnComment(getCurTok().getIdent());
        }
      }
    }
    // TODO: Some fine grained diagnostics
#endif
    return result;
  }

  // T := 'T' FilePath Comment
  // FilePath := AnyString
  // AnyString := .*^('\t' Comment)
  bool MetaParser::doTCommand(MetaSema::ActionResult& actionResult) {
    bool result = false;
    if (getCurTok().is(tok::ident) && getCurTok().getIdent().equals("T")) {
      consumeAnyStringToken();
      if (getCurTok().is(tok::raw_ident)) {
        std::string inputFile = getCurTok().getIdent();
        consumeAnyStringToken(tok::eof);
        if (getCurTok().is(tok::raw_ident)) {
          result = true;
          std::string outputFile = getCurTok().getIdent();
          actionResult = m_Actions->actOnTCommand(inputFile, outputFile);
        }
      }
    }
    // TODO: Some fine grained diagnostics
    return result;
  }

  // >RedirectCommand := '>' FilePath
  // FilePath := AnyString
  // AnyString := .*^(' ' | '\t')
  bool MetaParser::doRedirectCommand(MetaSema::ActionResult& actionResult) {

    unsigned constant_FD = 0;
    // Default redirect is stdout.
    MetaProcessor::RedirectionScope stream = MetaProcessor::kSTDOUT;

    if (getCurTok().is(tok::constant)) {
      // > or 1> the redirection is for stdout stream
      // 2> redirection for stderr stream
      constant_FD = getCurTok().getConstant();
      if (constant_FD == 2) {
        stream = MetaProcessor::kSTDERR;
      // Wrong constant_FD, do not redirect.
      } else if (constant_FD != 1) {
        llvm::errs() << "cling::MetaParser::isRedirectCommand():"
                     << "invalid file descriptor number " << constant_FD <<"\n";
        return true;
      }
      consumeToken();
    }
    // &> redirection for both stdout & stderr
    if (getCurTok().is(tok::ampersand)) {
      if (constant_FD == 0) {
        stream = MetaProcessor::kSTDBOTH;
      }
      consumeToken();
    }
    llvm::StringRef file;
    if (getCurTok().is(tok::greater)) {
      bool append = false;
      consumeToken();
      // check whether we have >>
      if (getCurTok().is(tok::greater)) {
        append = true;
        consumeToken();
      }
      // check for syntax like: 2>&1
      if (getCurTok().is(tok::ampersand)) {
        if (constant_FD == 0) {
          stream = MetaProcessor::kSTDBOTH;
        }
        consumeToken();
        const Token& Tok = getCurTok();
        if (Tok.is(tok::constant)) {
          switch (Tok.getConstant()) {
            case 1: file = llvm::StringRef("&1"); break;
            case 2: file = llvm::StringRef("&2"); break;
            default: break;
          }
          if (!file.empty()) {
            // Mark the stream name as refering to stderr or stdout, not a name
            stream = MetaProcessor::RedirectionScope(stream |
                                                     MetaProcessor::kSTDSTRM);
            consumeToken();
          }
        }
      }
      if (!getCurTok().is(tok::eof) && !(stream & MetaProcessor::kSTDSTRM)) {
        consumeAnyStringToken(tok::eof);
        if (getCurTok().is(tok::raw_ident)) {
          file = getCurTok().getIdent();
          consumeToken();
        }
      }
      // Empty file means std.
      actionResult =
          m_Actions->actOnRedirectCommand(file/*file*/,
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
  bool MetaParser::doXCommand(MetaSema::ActionResult& actionResult,
                              Value* resultValue) {
    if (resultValue)
      *resultValue = Value();
    const Token& Tok = getCurTok();
    if (Tok.is(tok::ident) && (Tok.getIdent().equals("x")
                               || Tok.getIdent().equals("X"))) {
      // There might be ArgList
      consumeAnyStringToken(tok::l_paren);
      llvm::StringRef file(getCurTok().getIdent());
      consumeToken();
      // '(' to end of string:

      std::string args = getCurTok().getBufStart();
      if (args.empty())
        args = "()";
      actionResult = m_Actions->actOnxCommand(file, args, resultValue);
      return true;
    }

    return false;
  }

  // ExtraArgList := AnyString [, ExtraArgList]
  bool MetaParser::doExtraArgList() {
    // This might be expanded if we need better arg parsing.
    consumeAnyStringToken(tok::r_paren);

    return getCurTok().is(tok::raw_ident);
  }

  bool MetaParser::doQCommand() {
    bool result = false;
    if (getCurTok().is(tok::ident) && getCurTok().getIdent().equals("q")) {
      result = true;
      m_Actions->actOnqCommand();
    }
    return result;
  }

  bool MetaParser::doUCommand(MetaSema::ActionResult& actionResult) {
    if (getCurTok().is(tok::ident) && getCurTok().getIdent().equals("U")) {
      consumeAnyStringToken(tok::eof);
      llvm::StringRef path;
      if (getCurTok().is(tok::raw_ident)) {
        path = getCurTok().getIdent();
        actionResult = m_Actions->actOnUCommand(path);
        return true;
      }
    }
    return false;
  }

  bool MetaParser::doICommand() {
    if (getCurTok().is(tok::ident) &&
        (   getCurTok().getIdent().equals("I")
         || getCurTok().getIdent().equals("include"))) {
      consumeAnyStringToken(tok::eof);
      llvm::StringRef path;
      if (getCurTok().is(tok::raw_ident))
        path = getCurTok().getIdent();
      m_Actions->actOnICommand(path);
      return true;
    }
    return false;
  }

  bool MetaParser::doOCommand() {
    const Token& currTok = getCurTok();
    if (currTok.is(tok::ident)) {
      llvm::StringRef ident = currTok.getIdent();
      if (ident.startswith("O")) {
        if (ident.size() > 1) {
          int level = 0;
          if (!ident.substr(1).getAsInteger(10, level) && level >= 0) {
            consumeAnyStringToken(tok::eof);
            if (getCurTok().is(tok::raw_ident))
              return false;
            //TODO: Process .OXXX here as .O with level XXX.
            return true;
          }
        } else {
          consumeAnyStringToken(tok::eof);
          const Token& lastStringToken = getCurTok();
          if (lastStringToken.is(tok::raw_ident)
              && lastStringToken.getLength()) {
            int level = 0;
            if (!lastStringToken.getIdent().getAsInteger(10, level) && level >= 0) {
              //TODO: process .O XXX
              return true;
            }
          } else {
            //TODO: process .O
            return true;
          }
        }
      }
    }

    return false;
  }

  bool MetaParser::doAtCommand() {
    if (getCurTok().is(tok::at) // && getCurTok().getIdent().equals("@")
        ) {
      consumeToken();
      skipWhitespace();
      m_Actions->actOnAtCommand();
      return true;
    }
    return false;
  }

  bool MetaParser::doRawInputCommand() {
    if (getCurTok().is(tok::ident) &&
        getCurTok().getIdent().equals("rawInput")) {
      MetaSema::SwitchMode mode = MetaSema::kToggle;
      consumeToken();
      skipWhitespace();
      if (getCurTok().is(tok::constant))
        mode = (MetaSema::SwitchMode)getCurTok().getConstantAsBool();
      m_Actions->actOnrawInputCommand(mode);
      return true;
    }
    return false;
  }

  bool MetaParser::doDebugCommand() {
    if (getCurTok().is(tok::ident) &&
        getCurTok().getIdent().equals("debug")) {
      llvm::Optional<int> mode;
      consumeToken();
      skipWhitespace();
      if (getCurTok().is(tok::constant))
        mode = getCurTok().getConstant();
      m_Actions->actOndebugCommand(mode);
      return true;
    }
    return false;
  }

  bool MetaParser::doPrintDebugCommand() {
    if (getCurTok().is(tok::ident) &&
        getCurTok().getIdent().equals("printDebug")) {
      MetaSema::SwitchMode mode = MetaSema::kToggle;
      consumeToken();
      skipWhitespace();
      if (getCurTok().is(tok::constant))
        mode = (MetaSema::SwitchMode)getCurTok().getConstantAsBool();
      m_Actions->actOnprintDebugCommand(mode);
      return true;
    }
    return false;
  }

  bool MetaParser::doStoreStateCommand() {
     if (getCurTok().is(tok::ident) &&
        getCurTok().getIdent().equals("storeState")) {
       //MetaSema::SwitchMode mode = MetaSema::kToggle;
      consumeToken();
      skipWhitespace();
      if (!getCurTok().is(tok::stringlit))
        return false; // FIXME: Issue proper diagnostics
      std::string ident = getCurTok().getIdentNoQuotes();
      consumeToken();
      m_Actions->actOnstoreStateCommand(ident);
      return true;
    }
    return false;
  }

  bool MetaParser::doCompareStateCommand() {
    if (getCurTok().is(tok::ident) &&
        getCurTok().getIdent().equals("compareState")) {
      //MetaSema::SwitchMode mode = MetaSema::kToggle;
      consumeToken();
      skipWhitespace();
      if (!getCurTok().is(tok::stringlit))
        return false; // FIXME: Issue proper diagnostics
      std::string ident = getCurTok().getIdentNoQuotes();
      consumeToken();
      m_Actions->actOncompareStateCommand(ident);
      return true;
    }
    return false;
  }

  bool MetaParser::doStatsCommand() {
    if (getCurTok().is(tok::ident) &&
        getCurTok().getIdent().equals("stats")) {
      consumeToken();
      skipWhitespace();
      if (!getCurTok().is(tok::ident))
        return false; // FIXME: Issue proper diagnostics
      std::string ident = getCurTok().getIdent();
      consumeToken();
      m_Actions->actOnstatsCommand(ident);
      return true;
    }
    return false;
  }

  bool MetaParser::doUndoCommand() {
    if (getCurTok().is(tok::ident) &&
        getCurTok().getIdent().equals("undo")) {
      consumeToken();
      skipWhitespace();
      const Token& next = getCurTok();
      if (next.is(tok::constant))
        m_Actions->actOnUndoCommand(next.getConstant());
      else
        m_Actions->actOnUndoCommand();
      return true;
    }
    return false;
  }

  bool MetaParser::doDynamicExtensionsCommand() {
    if (getCurTok().is(tok::ident) &&
        getCurTok().getIdent().equals("dynamicExtensions")) {
      MetaSema::SwitchMode mode = MetaSema::kToggle;
      consumeToken();
      skipWhitespace();
      if (getCurTok().is(tok::constant))
        mode = (MetaSema::SwitchMode)getCurTok().getConstantAsBool();
      m_Actions->actOndynamicExtensionsCommand(mode);
      return true;
    }
    return false;
  }

  bool MetaParser::doHelpCommand() {
    const Token& Tok = getCurTok();
    if (Tok.is(tok::quest_mark) ||
        (Tok.is(tok::ident) && Tok.getIdent().equals("help"))) {
      m_Actions->actOnhelpCommand();
      return true;
    }
    return false;
  }

  bool MetaParser::doFileExCommand() {
    if (getCurTok().is(tok::ident) && getCurTok().getIdent().equals("fileEx")) {
      m_Actions->actOnfileExCommand();
      return true;
    }
    return false;
  }

  bool MetaParser::doFilesCommand() {
    if (getCurTok().is(tok::ident) && getCurTok().getIdent().equals("files")) {
      m_Actions->actOnfilesCommand();
      return true;
    }
    return false;
  }

  bool MetaParser::doClassCommand() {
    const Token& Tok = getCurTok();
    if (Tok.is(tok::ident)) {
      if (Tok.getIdent().equals("class")) {
        consumeAnyStringToken(tok::eof);
        const Token& NextTok = getCurTok();
        llvm::StringRef className;
        if (NextTok.is(tok::raw_ident))
          className = NextTok.getIdent();
        m_Actions->actOnclassCommand(className);
        return true;
      }
      else if (Tok.getIdent().equals("Class")) {
        m_Actions->actOnClassCommand();
        return true;
      }
    }
    return false;
  }

  bool MetaParser::doNamespaceCommand() {
    const Token& Tok = getCurTok();
    if (Tok.is(tok::ident)) {
      if (Tok.getIdent().equals("namespace")) {
        consumeAnyStringToken(tok::eof);
        if (getCurTok().is(tok::raw_ident))
          return false;
        m_Actions->actOnNamespaceCommand();
        return true;
      }
    }
    return false;
  }

  bool MetaParser::doGCommand() {
    if (getCurTok().is(tok::ident) && getCurTok().getIdent().equals("g")) {
      consumeToken();
      skipWhitespace();
      llvm::StringRef varName;
      if (getCurTok().is(tok::ident))
        varName = getCurTok().getIdent();
      m_Actions->actOngCommand(varName);
      return true;
    }
    return false;
  }

  bool MetaParser::doTypedefCommand() {
    const Token& Tok = getCurTok();
    if (Tok.is(tok::ident)) {
      if (Tok.getIdent().equals("typedef")) {
        consumeAnyStringToken(tok::eof);
        const Token& NextTok = getCurTok();
        llvm::StringRef typedefName;
        if (NextTok.is(tok::raw_ident))
          typedefName = NextTok.getIdent();
        m_Actions->actOnTypedefCommand(typedefName);
        return true;
      }
    }
    return false;
  }

  bool MetaParser::doShellCommand(MetaSema::ActionResult& actionResult,
                                  Value* resultValue) {
    if (resultValue)
      *resultValue = Value();
    const Token& Tok = getCurTok();
    if (Tok.is(tok::excl_mark)) {
      consumeAnyStringToken(tok::eof);
      const Token& NextTok = getCurTok();
      if (NextTok.is(tok::raw_ident)) {
         llvm::StringRef commandLine(NextTok.getIdent());
         if (!commandLine.empty())
            actionResult = m_Actions->actOnShellCommand(commandLine,
                                                        resultValue);
      }
      return true;
    }
    return false;
  }

} // end namespace cling
