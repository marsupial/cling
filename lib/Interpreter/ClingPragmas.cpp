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

  class ClingPragmaHandler: public PragmaHandler {
    Interpreter& m_Interp;

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

    void ReportCommandErr(Preprocessor& PP, const Token& Tok) {
      PP.Diag(Tok.getLocation(), diag::err_expected)
        << "load, add_library_path, or add_include_path";
    }

    int GetCommand(const StringRef CommandStr) {
      if (CommandStr == "load")
        return 0;
      else if (CommandStr == "add_library_path")
        return 1;
      else if (CommandStr == "add_include_path")
        return 2;

      return -1;
    }
    
  public:
    ClingPragmaHandler(Interpreter& interp):
      PragmaHandler("cling"), m_Interp(interp) {}

    void HandlePragma(Preprocessor& PP,
                      PragmaIntroducerKind Introducer,
                      Token& FirstToken) override {

      Token Tok;
      PP.Lex(Tok);
      SkipToEOD OnExit(PP, Tok);

      if (Tok.isNot(tok::identifier)) {
        ReportCommandErr(PP, Tok);
        return;
      }

      const StringRef CommandStr = Tok.getIdentifierInfo()->getName();
      const int Command = GetCommand(CommandStr);
      if (Command < 0) {
        ReportCommandErr(PP, Tok);
        return;
      }

      PP.Lex(Tok);
      if (Tok.isNot(tok::l_paren)) {
        PP.Diag(Tok.getLocation(), diag::err_expected_lparen_after)
                << CommandStr;
        return;
      }

      std::string Literal;
      if (!PP.LexStringLiteral(Tok, Literal, CommandStr.str().c_str(),
                               false /*allowMacroExpansion*/)) {
        // already diagnosed.
        return;
      }

      replaceEnvVars(Literal);

      if (Command == 0) {
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
        
        m_Interp.loadFile(Literal, true /*allowSharedLib*/);
      }
      else if (Command == 1)
        m_Interp.getOptions().LibSearchPath.push_back(std::move(Literal));
      else if (Command == 2)
        m_Interp.AddIncludePath(Literal);
    }
  };
}

void cling::addClingPragmas(Interpreter& interp) {
  Preprocessor& PP = interp.getCI()->getPreprocessor();
  // PragmaNamespace / PP takes ownership of sub-handlers.
  PP.AddPragmaHandler(StringRef(), new ClingPragmaHandler(interp));
}
