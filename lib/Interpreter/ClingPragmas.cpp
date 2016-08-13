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
#include "cling/Utils/Paths.h"

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
  enum PragmaCmd {
    kPragmaLoad    = 0,
    kPragmaAddLib  ,
    kPragmaAddInc  ,

    kPragmaUnknown = -1
  };
  
  static PragmaCmd GetCommand(const StringRef CommandStr) {
    if (CommandStr == "load")
      return kPragmaLoad;
    else if (CommandStr == "add_library_path")
      return kPragmaAddLib;
    else if (CommandStr == "add_include_path")
      return kPragmaAddInc;
    return kPragmaUnknown;
  }

  static bool LitToString(Preprocessor& PP, Token& Tok, std::string& Literal) {
    assert(Tok.isLiteral() && "Token is not a literal");
    SmallVector<Token, 1> StrToks(1, Tok);
    StringLiteralParser LitParse(StrToks, PP);
    if (LitParse.hadError)
      return false;
    Literal = LitParse.GetString();
    return true;
  }
  
  static bool GetNextLiteral(Preprocessor& PP, Token& Tok, std::string& Literal,
                             const char* firstTime = nullptr) {
    Literal.clear();
    PP.Lex(Tok);
    if (Tok.isLiteral()) {
      LitToString(PP, Tok, Literal);
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

    cling::utils::ExpandEnvVars(Literal);
    return true;
  }

  static void ReportCommandErr(
      Preprocessor& PP, const Token& Tok,
      const char* Msg = "load, add_library_path, or add_include_path") {
    PP.Diag(Tok.getLocation(), diag::err_expected) << Msg;
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
  const bool FuncStyle = Tok.is(tok::l_paren);
  if (FuncStyle)
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
  const PragmaCmd Command = GetCommand(CommandStr);
  if (Command == kPragmaUnknown) {
    // Pass it off to the MetaProcessor
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

  if (Command == kPragmaLoad)
    return LoadCommand(PP, Tok, std::move(Literal));

  do{
    if (Command == kPragmaAddInc) {
      // Because the pragma can be invoked function or bash style
      PP.Lex(Tok);
      // add_include_path "PATHA" "PATHB"
      if (!Tok.isLiteral() && !Tok.isOneOf(tok::eod, tok::eof)) {
        // add_include_path "PATHA,PATHB,PATHC" ,
        // add_include_path("$ENV",:)
        bool Invalid = false;
        if (FuncStyle) {
          if (Tok.is(tok::comma)) {
            PP.Lex(Tok);
            Invalid = Tok.isLiteral();
          }
          else
            Invalid = Tok.is(tok::r_paren);
        }
        if (!Invalid) {
          std::string Delim = PP.getSpelling(Tok, &Invalid);
          if (!Invalid && Delim.size() == 1)
            m_Interp.AddIncludePaths(Literal, Delim.c_str());
          else
            ReportCommandErr(PP, Tok, "string literal or single character");
        
          continue;
        }
      }
      m_Interp.AddIncludePath(Literal);
      if (Tok.isLiteral()) {
        if (LitToString(PP, Tok, Literal)) {
          cling::utils::ExpandEnvVars(Literal);
          m_Interp.AddIncludePath(Literal);
        } else
          ReportCommandErr(PP, Tok, "string literal conversion");
      }
    }
    else if (Command == kPragmaAddLib)
      m_Interp.getOptions().LibSearchPath.push_back(std::move(Literal));

  } while (!Tok.isOneOf(tok::eod, tok::eof) && GetNextLiteral(PP, Tok, Literal));
}

ClingPragmaHandler* ClingPragmaHandler::install(Interpreter& interp) {
  Preprocessor& PP = interp.getCI()->getPreprocessor();
  // PragmaNamespace / PP takes ownership of sub-handlers.
  ClingPragmaHandler* Handler = new ClingPragmaHandler(interp);
  PP.AddPragmaHandler(StringRef(), Handler);
  return Handler;
}
