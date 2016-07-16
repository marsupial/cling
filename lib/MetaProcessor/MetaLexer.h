//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vasil.georgiev.vasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_META_LEXER_H
#define CLING_META_LEXER_H

#include "llvm/ADT/StringRef.h"

namespace cling {
  namespace meta {

    namespace tok {
      enum TokenKind {
        l_square   = 1 << 0,   // "["
        r_square   = 1 << 1,   // "]"
        l_paren    = 1 << 2,   // "("
        r_paren    = 1 << 3,   // ")"
        l_brace    = 1 << 4,   // "{"
        r_brace    = 1 << 5,   // "}"
        stringlit  = 1 << 6,   // ""...""
        charlit    = 1 << 7,   // "'.'"
        comma      = 1 << 8,   // ","
        dot        = 1 << 9,   // "."
        excl_mark  = 1 << 10,  // "!"
        quest_mark = 1 << 11,  // "?"
        slash      = 1 << 12,  // "/"
        backslash  = 1 << 13,  // "\"
        greater    = 1 << 14,  // ">"
        ampersand  = 1 << 15,  // "&"
        hash       = 1 << 16,  // "#"
        ident      = 1 << 17,  // (a-zA-Z)[(0-9a-zA-Z)*]
        raw_ident  = 1 << 18,  // .*^(' '|'\t')
        comment    = 1 << 19,  // //
        space      = 1 << 20,  // (' ' | '\t')*
        constant   = 1 << 21,  // {0-9}
        at         = 1 << 22,  // @
        asterik    = 1 << 23,  // *
        semicolon  = 1 << 24,  // ;
        eof        = 1 << 25,  // 0
        unknown    = 1 << 26,
      };
    }

    class Token {
    private:
      tok::TokenKind kind;
      const char* bufStart;
      unsigned length;
      mutable unsigned value;
    public:
      void startToken(const char* Pos = 0) {
        kind = tok::unknown;
        bufStart = Pos;
        value = ~0U;
        length = 0;
      }
      tok::TokenKind getKind() const { return kind; }
      void setKind(tok::TokenKind K) { kind = K; }
      unsigned getLength() const { return length; }
      void setLength(unsigned L) { length = L; }
      const char* getBufStart() const { return bufStart; }
      void setBufStart(const char* Pos) { bufStart = Pos; }

      bool isNot(tok::TokenKind K) const { return kind != K; }
      bool is(tok::TokenKind K) const { return kind == K; }
      bool isOneOf(int K) const { return kind & K; }

      llvm::StringRef getIdent() const;
      llvm::StringRef getIdentNoQuotes() const {
        if (getKind() >= tok::stringlit && getKind() <= tok::charlit)
          return getIdent().drop_back().drop_front();
        return getIdent();
      }
      bool getConstantAsBool() const;
      unsigned getConstant() const;
    };

    class Lexer {
    protected:
      const char* bufferStart;
      const char* curPos;
    public:
      Lexer(llvm::StringRef input, bool skipWhiteSpace = false);
      void reset(llvm::StringRef Line);

      void Lex(Token& Tok);
      void LexAnyString(Token& Tok);

      static void LexPunctuator(const char* C, Token& Tok);
      // TODO: Revise. We might not need that.
      static void LexPunctuatorAndAdvance(const char*& curPos, Token& Tok);
      static void LexQuotedStringAndAdvance(const char*& curPos, Token& Tok);
      void LexConstant(char C, Token& Tok);
      void LexIdentifier(char C, Token& Tok);
      void LexEndOfFile(char C, Token& Tok);
      void LexWhitespace(char C, Token& Tok);
      void SkipWhitespace();
      const char* getLocation() const { return curPos; }
    };

  } //end namespace meta

  typedef meta::Lexer MetaLexer;

} //end namespace cling

#endif // CLING_PUNCTUATION_LEXER_H
