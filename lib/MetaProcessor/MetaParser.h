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

#include "MetaLexer.h"
#include "llvm/ADT/SmallVector.h"
#include <memory>


namespace cling {
  namespace meta {

    class Parser {
    private:
      MetaLexer m_Lexer;
      llvm::SmallVector<Token, 2> m_TokenCache;

      const Token& lookAhead(unsigned Num);

    public:
      ///\brief Returns the current token without consuming it.
      ///
      inline const Token& getCurTok() { return lookAhead(0); }

      ///\brief Consume the current 'peek' token.
      ///
      void consumeToken();
      void skipWhitespace();
      void consumeAnyStringToken(tok::TokenKind stopAt = tok::space);

      Parser(llvm::StringRef Line) : m_Lexer(Line) {}
    };

  } // end namespace meta
  typedef meta::Parser MetaParser;
} // end namespace cling

#endif // CLING_META_PARSER_H
