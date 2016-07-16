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
#include "MetaActions.h"

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/InvocationOptions.h"
#include "cling/Interpreter/Value.h"

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace cling::meta;

void Parser::consumeToken() {
  if (m_TokenCache.size())
    m_TokenCache.erase(m_TokenCache.begin());

  lookAhead(0);
}

void Parser::consumeAnyStringToken(tok::TokenKind stopAt/*=tok::space*/) {
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

const Token& Parser::lookAhead(unsigned N) {
  if (N < m_TokenCache.size())
    return m_TokenCache[N];

  for (unsigned C = N+1 - m_TokenCache.size(); C > 0; --C) {
    m_TokenCache.push_back(Token());
    m_Lexer.Lex(m_TokenCache.back());
  }
  return m_TokenCache.back();
}

void Parser::skipWhitespace() {
  while(getCurTok().is(tok::space))
    consumeToken();
}
