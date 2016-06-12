//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vasil.georgiev.vasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_META_PARSER_H
#define CLING_META_PARSER_H

#include "MetaLexer.h" // for cling::Token
#include "MetaSema.h" // for ActionResult
#include "llvm/ADT/SmallVector.h"

#include <memory>

namespace llvm {
  class StringRef;
}

namespace cling {
  class MetaLexer;
  class MetaSema;
  class Value;

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
  class MetaParser {
  private:
    MetaLexer m_Lexer;
    std::unique_ptr<MetaSema> m_Actions;
    llvm::SmallVector<Token, 2> m_TokenCache;
    llvm::SmallVector<Token, 4> m_MetaSymbolCache;
  private:
    const Token& lookAhead(unsigned Num);
	int modeToken();
    ///\brief Returns the current token without consuming it.
    ///
    inline const Token& getCurTok() { return lookAhead(0); }

    ///\brief Consume the current 'peek' token.
    ///
    void consumeToken();
    void skipWhitespace();
    void consumeAnyStringToken(tok::TokenKind stopAt = tok::space);
    const Token& consumeToTokenNext(tok::TokenKind stopAt = tok::space) {
      consumeAnyStringToken(stopAt);
      return getCurTok();
    }
    const Token& skipToNextToken() {
      consumeToken();
      skipWhitespace();
      return getCurTok();
    }

    
    bool isCommandSymbol();
    bool doCommand(MetaSema::ActionResult& actionResult,
                   Value* resultValue);
    bool doLCommand(MetaSema::ActionResult& actionResult);
    bool doTCommand(MetaSema::ActionResult& actionResult);
    bool doRedirectCommand(MetaSema::ActionResult& actionResult);
    bool doExtraArgList();
    bool doXCommand(MetaSema::ActionResult& actionResult,
                    Value* resultValue);
    bool doAtCommand();
    bool doQCommand();
    bool doUCommand(MetaSema::ActionResult& actionResult);
    bool doICommand();
    bool doOCommand();
    bool doRawInputCommand();
    bool doDebugCommand();
    bool doPrintDebugCommand();
    bool doStoreStateCommand();
    bool doCompareStateCommand();
    bool doStatsCommand();
    bool doUndoCommand();
    bool doDynamicExtensionsCommand();
    bool doHelpCommand();
    bool doFileExCommand();
    bool doFilesCommand();
    bool doClassCommand();
    bool doNamespaceCommand();
    bool doGCommand();
    bool doTypedefCommand();
    bool doShellCommand(MetaSema::ActionResult& actionResult,
                        Value* resultValue);

    bool doFCommand(MetaSema::ActionResult& actionResult); // OS X framework

  public:
    MetaParser(MetaSema* Actions);
    void enterNewInputLine(llvm::StringRef Line);

    ///\brief Drives the recursive decendent parsing.
    ///
    ///\returns true if it was meta command.
    ///
    bool doMetaCommand(MetaSema::ActionResult& actionResult,
                       Value* resultValue);

    ///\brief Returns whether quit was requested via .q command
    ///
    bool isQuitRequested() const;

    MetaSema& getActions() const { return *m_Actions.get(); }
    llvm::StringRef consumeToNextString();
  };
} // end namespace cling

#endif // CLING_META_PARSER_H
