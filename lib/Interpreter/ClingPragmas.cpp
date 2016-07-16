//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Axel Naumann <axel@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "ClingPragmas.h"

#include "cling/Interpreter/Interpreter.h"
#include "cling/MetaProcessor/Commands.h"

#include "clang/AST/ASTContext.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Lex/Token.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/ParseDiagnostic.h"

using namespace cling;
using namespace clang;

namespace {
  static void replaceEnvVars(std::string& Path) {
    std::size_t bpos = Path.find("$");
    while (bpos != std::string::npos) {
      std::size_t spos = Path.find("/", bpos + 1);
      std::size_t length = Path.length();

      if (spos != std::string::npos) // if we found a "/"
        length = spos - bpos;

      std::string envVar = Path.substr(bpos + 1, length -1); //"HOME"
      const char* c_Path = getenv(envVar.c_str());
      std::string fullPath;
      if (c_Path != NULL) {
        fullPath = std::string(c_Path);
      } else {
        fullPath = std::string("");
      }
      Path.replace(bpos, length, fullPath);
      bpos = Path.find("$", bpos + 1); //search for next env variable
    }
  }

  struct SkipToEOD {
    Preprocessor& m_PP;
    Token& m_Tok;
    SkipToEOD(Preprocessor& PParg, Token& Tok):
      m_PP(PParg), m_Tok(Tok) {
    }
    ~SkipToEOD() {
      // Can't use Preprocessor::DiscardUntilEndOfDirective, as we may
      // already be on an eod token
      while (!m_Tok.isOneOf(tok::eod, tok::eof))
        m_PP.LexUnexpandedToken(m_Tok);
    }
  };
}

bool ClingPragmaHandler::GetNextLiteral(Preprocessor& PP, Token& Tok,
                                        std::string& Literal,
                                        const char* firstTime) const {
  Literal.clear();
  PP.Lex(Tok);
  if (Tok.isLiteral()) {
    SmallVector<Token, 1> StrToks(1, Tok);
    StringLiteralParser LitParse(StrToks, PP);
    if (!LitParse.hadError)
      Literal = LitParse.GetString();
  }
  else if (Tok.is(tok::comma))
    return GetNextLiteral(PP, Tok, Literal);
  else if (firstTime) {
    if (Tok.is(tok::l_paren)) {
      if (!PP.LexStringLiteral(Tok, Literal, firstTime,
                               false /*allowMacroExpansion*/)) {
        // already diagnosed.
        return false;
      }
    }
  }

  if (Literal.empty())
    return false;

  replaceEnvVars(Literal);
  return true;
}

void ClingPragmaHandler::ReportCommandErr(Preprocessor& PP, const Token& Tok) {
  PP.Diag(Tok.getLocation(), diag::err_expected)
    << "load, add_library_path, or add_include_path";
}

int ClingPragmaHandler::GetCommand(const StringRef CommandStr) const {
  if (CommandStr == "load")
    return 0;
  else if (CommandStr == "add_library_path")
    return 1;
  else if (CommandStr == "add_include_path")
    return 2;

  return -1;
}

void ClingPragmaHandler::LoadCommand(Preprocessor& PP, Token& Tok,
                                     std::string Literal) {

  // Need to parse them all until the end to handle the possible
  // #include stements that will be generated
  std::vector<std::string> Files;
  Files.push_back(std::move(Literal));
  while (GetNextLiteral(PP, Tok, Literal))
    Files.push_back(std::move(Literal));
  
  clang::Parser& P = m_Interp.getParser();
  Parser::ParserCurTokRestoreRAII savedCurToken(P);
  // After we have saved the token reset the current one to something
  // which is safe (semi colon usually means empty decl)
  Token& CurTok = const_cast<Token&>(P.getCurToken());
  CurTok.setKind(tok::semi);
  
  Preprocessor::CleanupAndRestoreCacheRAII cleanupRAII(PP);
  // We can't PushDeclContext, because we go up and the routine that
  // pops the DeclContext assumes that we drill down always.
  // We have to be on the global context. At that point we are in a
  // wrapper function so the parent context must be the global.
  TranslationUnitDecl* TU =
  m_Interp.getCI()->getASTContext().getTranslationUnitDecl();
  Sema::ContextAndScopeRAII pushedDCAndS(m_Interp.getSema(),
                                        TU, m_Interp.getSema().TUScope);
  Interpreter::PushTransactionRAII pushedT(&m_Interp);

  for (std::string& File : Files) {
    if (m_Interp.loadFile(File, true) != Interpreter::kSuccess)
      return;
  }
}

ClingPragmaHandler::ClingPragmaHandler(Interpreter& interp) :
  PragmaHandler("cling"), m_Interp(interp), m_Commands(nullptr) {}

bool ClingPragmaHandler::RunCommand(clang::Lexer* Lex,
                                    const StringRef& CommandStr) const {
  if (m_Commands) {
    SmallString<256> Str;
    (CommandStr+Lex->getBufferLocation()).toStringRef(Str);

    // strip any trailing whitespace
    while (Str.size() && ::isspace(Str[Str.size()-1]))
      Str.resize(Str.size()-1);

    // Error reporting handled via commands, but if command wasn't found
    // print out pragma help text.
    return m_Commands->execute(Str.c_str(), m_Interp, llvm::outs())
            != meta::kCmdNotFound;
  }
  return false;
}

void ClingPragmaHandler::HandlePragma(Preprocessor& PP,
                                      PragmaIntroducerKind Introducer,
                                      Token& FirstToken) {

  Token Tok;
  PP.Lex(Tok);
  SkipToEOD OnExit(PP, Tok);

  // #pragma cling(load, "A")
  if (Tok.is(tok::l_paren))
    PP.Lex(Tok);

  if (Tok.isNot(tok::identifier)) {
    if (Tok.is(tok::raw_identifier) || Tok.is(tok::eof)
        || Tok.isAnnotation() || Tok.isLiteral()) {
      ReportCommandErr(PP, Tok);
      return;
    }
    // Anything else can still be a command name
  }

  const StringRef CommandStr = Tok.getIdentifierInfo()->getName();
  const int Command = GetCommand(CommandStr);
  if (Command < 0) {
    if (!RunCommand(static_cast<Lexer*>(PP.getCurrentLexer()), CommandStr))
      ReportCommandErr(PP, Tok);
    return;
  }

  std::string Literal;
  if (!GetNextLiteral(PP, Tok, Literal, CommandStr.data())) {
    PP.Diag(Tok.getLocation(), diag::err_expected_after)
      << CommandStr << "argument";
    return;
  }

  if (Command == 0)
    return LoadCommand(PP, Tok, std::move(Literal));

  do{
    if (Command == 1)
      m_Interp.getOptions().LibSearchPath.push_back(std::move(Literal));
    else if (Command == 2)
      m_Interp.AddIncludePath(Literal);
  } while (GetNextLiteral(PP, Tok, Literal));
}

ClingPragmaHandler* ClingPragmaHandler::install(Interpreter& interp) {
  Preprocessor& PP = interp.getCI()->getPreprocessor();
  // PragmaNamespace / PP takes ownership of sub-handlers.
  ClingPragmaHandler* Handler = new ClingPragmaHandler(interp);
  PP.AddPragmaHandler(StringRef(), Handler);
  return Handler;
}
