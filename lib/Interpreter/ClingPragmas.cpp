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
#include "cling/Interpreter/Transaction.h"
#include "cling/MetaProcessor/Commands.h"
#include "cling/Utils/Output.h"
#include "cling/Utils/Paths.h"

#include "clang/AST/ASTContext.h"
#include "clang/Basic/TokenKinds.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/LiteralSupport.h"
#include "clang/Lex/Token.h"
#include "clang/Parse/Parser.h"
#include "clang/Parse/ParseDiagnostic.h"

#include <cstdlib>

using namespace cling;
using namespace clang;

namespace {
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

    enum {
      kLoadFile,
      kAddLibrary,
      kAddInclude,
      // Put all commands that expand environment variables above this
      kExpandEnvCommands,

      // Put all commands that only take string literals above this
      kArgumentsAreLiterals,

      kOptimize,
      kInvalidCommand,
    };

    llvm::StringRef GetName(Preprocessor& PP, Token& Tok, std::string& Buf) const {
      if (Tok.is(tok::identifier))
        return Tok.getIdentifierInfo()->getName();

      llvm::SmallString<64> Buffer;
      Buf = PP.getSpelling(Tok, Buffer).str();
      return Buf;
    }

    static llvm::StringRef GetString(const SourceManager& SM, SourceRange SR,
                                     bool& Invalid) {
      const char* CStart = SM.getCharacterData(SR.getBegin(), &Invalid);
      if (Invalid)
        return "";

      const char* CEnd = SM.getCharacterData(SR.getEnd(), &Invalid);
      if (Invalid)
        return "";

      return llvm::StringRef(CStart, CEnd - CStart);
    }

    static llvm::StringRef GetString(Preprocessor& PP, Token& Tok, bool& Invalid,
                                     tok::TokenKind K2 = tok::eof) {
      const SourceLocation Begin = Tok.getLocation();
      while (!Tok.isOneOf(tok::eod, K2))
        PP.LexUnexpandedToken(Tok);

      const SourceLocation End = Tok.getLocation();
      return GetString(PP.getSourceManager(), SourceRange(Begin, End), Invalid);
    }

    static bool GetArgument(Preprocessor& PP, const Token& Tok,
                            SourceRange& Range, std::string& Flat) {
      Range.setEnd(Tok.getLocation());
      bool Err;
      const llvm::StringRef Str = GetString(PP.getSourceManager(), Range, Err);
      Range.setBegin(Tok.getLocation().getLocWithOffset(1));
      if (!Err) Flat += " " + Str.str();
      return Err;
    }

    static bool FlattenArgs(Preprocessor& PP, Token& Tok, size_t Paren,
                            std::string& Flat) {
      // CommentHandler subclass to append comments into the flat arguments.
      class AppendComment : public clang::CommentHandler {
        Preprocessor& PP;
        std::string* Out;
      public:
        AppendComment(Preprocessor& P, std::string& S) : PP(P), Out(&S) {
          P.addCommentHandler(this);
        }
        ~AppendComment() { PP.removeCommentHandler(this); }

        bool HandleComment(Preprocessor& PP, SourceRange Comment) {
          if (Out) {
            bool Err;
            const auto Str = GetString(PP.getSourceManager(), Comment, Err);
            if (!Err) *Out += " " + Str.str();
          }
          return false;
        }
        void operator() (std::string* S) { Out = S; }
      } AC(PP, Flat);

      PP.Lex(Tok);
      if (Paren) {
        if (Tok.is(tok::r_paren)) {
          // Already reached the end, no need to parse arguments.
          Paren = false;
          PP.Lex(Tok);
        }
        else if (!Tok.is(tok::comma)) // No comma, no functional arguments
          Paren = false;
      } else // #pragma cling CMD(args)
        Paren = Tok.is(tok::l_paren);

      if (!Paren)
        return false;

      size_t Group = 0;
      llvm::SmallString<64> Buffer;
      SourceRange Range(Tok.getLocation().getLocWithOffset(1));

      // Loop eats chunks of text between commas, so no need to append comments.
      AC(nullptr);
      while (Paren) {
        PP.Lex(Tok);
        switch (Tok.getKind()) {
          case tok::l_square:
          case tok::l_brace: ++Group; break;
          case tok::r_square:
          case tok::r_brace: --Group; break;
          case tok::l_paren: ++Paren; break;
          case tok::r_paren:
            if (--Paren == 0)
              GetArgument(PP, Tok, Range, Flat);
            break;
          case tok::comma:
            if (Paren == 1 && !Group)
              GetArgument(PP, Tok, Range, Flat);
            break;
          case tok::eod:
          case tok::eof:
            return true;
          default: break;
        }
      }
      assert(Tok.is(tok::r_paren));

      // Eat the last relevant token and append any remaining comments.
      AC(&Flat);
      PP.Lex(Tok);
      return false;
    }
  
    bool GetNextLiteral(Preprocessor& PP, Token& Tok, std::string& Literal,
                        unsigned Cmd, const char* firstTime = nullptr) const {
      Literal.clear();

      PP.Lex(Tok);
      if (Tok.isLiteral()) {
        if (clang::tok::isStringLiteral(Tok.getKind())) {
          SmallVector<Token, 1> StrToks(1, Tok);
          StringLiteralParser LitParse(StrToks, PP);
          if (!LitParse.hadError)
            Literal = LitParse.GetString();
        } else {
          llvm::SmallString<64> Buffer;
          Literal = PP.getSpelling(Tok, Buffer).str();
        }
      }
      else if (Tok.is(tok::comma))
        return GetNextLiteral(PP, Tok, Literal, Cmd);
      else if (firstTime) {
        if (Tok.is(tok::l_paren)) {
          if (Cmd < kArgumentsAreLiterals) {
            if (!PP.LexStringLiteral(Tok, Literal, firstTime,
                                     false /*allowMacroExpansion*/)) {
              // already diagnosed.
              return false;
            }
          } else {
            PP.Lex(Tok);
            llvm::SmallString<64> Buffer;
            Literal = PP.getSpelling(Tok, Buffer).str();
          }
        }
      }

      if (Literal.empty())
        return false;

      if (Cmd < kExpandEnvCommands)
        utils::ExpandEnvVars(Literal);

      return true;
    }

    void ReportCommandErr(Preprocessor& PP, SourceLocation Loc) {
      PP.Diag(Loc, diag::err_expected)
        << "load, add_library_path, or add_include_path";
    }

    int GetCommand(const StringRef CommandStr) {
      if (CommandStr == "load")
        return kLoadFile;
      else if (CommandStr == "add_library_path")
        return kAddLibrary;
      else if (CommandStr == "add_include_path")
        return kAddInclude;
      else if (CommandStr == "optimize")
        return kOptimize;
      return kInvalidCommand;
    }

    void LoadCommand(Preprocessor& PP, Token& Tok, std::string Literal) {
      // No need to load libraries when not executing anything.
      if (m_Interp.isInSyntaxOnlyMode())
        return;

      // Need to parse them all until the end to handle the possible
      // #include stements that will be generated
      std::vector<std::string> Files;
      Files.push_back(std::move(Literal));
      while (GetNextLiteral(PP, Tok, Literal, kLoadFile))
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
      m_Interp.get<ASTContext>().getTranslationUnitDecl();
      Sema::ContextAndScopeRAII pushedDCAndS(m_Interp.getSema(),
                                            TU, m_Interp.getSema().TUScope);
      Interpreter::PushTransactionRAII pushedT(&m_Interp);

      for (std::string& File : Files) {
        if (m_Interp.loadFile(File, true) != Interpreter::kSuccess)
          return;
      }
    }

    void OptimizeCommand(const char* Str) {
      char* ConvEnd = nullptr;
      int OptLevel = std::strtol(Str, &ConvEnd, 10 /*base*/);
      if (!ConvEnd || ConvEnd == Str) {
        cling::errs() << "cling::PHOptLevel: "
          "missing or non-numerical optimization level.\n" ;
        return;
      }
      auto T = const_cast<Transaction*>(m_Interp.getCurrentTransaction());
      assert(T && "Parsing code without transaction!");
      // The topmost Transaction drives the jitting.
      T = T->getTopmostParent();
      CompilationOptions& CO = T->getCompilationOpts();
      if (CO.OptLevel != m_Interp.getDefaultOptLevel()) {
        // Another #pragma already changed the opt level, a conflict that
        // cannot be resolve here.  Mention and keep the lower one.
        cling::errs() << "cling::PHOptLevel: "
          "conflicting `#pragma cling optimize` directives: "
          "was already set to " << CO.OptLevel << '\n';
        if (CO.OptLevel > OptLevel) {
          CO.OptLevel = OptLevel;
          cling::errs() << "Setting to lower value of " << OptLevel << '\n';
        } else {
          cling::errs() << "Ignoring higher value of " << OptLevel << '\n';
        }
      } else
        CO.OptLevel = OptLevel;
  }

  bool RunCommand(clang::Preprocessor& PP, const StringRef& CommandStr) const {
    using namespace meta;
    CommandHandler* Cmds = m_Interp.getCommandHandler();
    if (!Cmds)
      return false;

    clang::Lexer* Lex = static_cast<Lexer*>(PP.getCurrentLexer());
    if (!Lex)
      return false;

    SmallString<256> Buffer;
    (CommandStr+Lex->getBufferLocation()).toStringRef(Buffer);

    // Just trim the trailing newline spaces, CommandHandler will scan the rest
    const llvm::StringRef Cmd =  Buffer.str().rtrim();
    if (Cmd.empty())
      return false;

    DiagnosticErrorTrap Trap(PP.getDiagnostics());
    meta::Invocation Invok = {Cmd, m_Interp, cling::outs(), *Cmds, nullptr, ""};
    if (Cmds->Execute(Invok) == meta::kCmdSuccess)
      return true;

    // Return if any command has printed diagnostics.
    return Trap.hasErrorOccurred();
  }

  public:
    ClingPragmaHandler(Interpreter& interp):
      PragmaHandler("cling"), m_Interp(interp) {}

    void HandlePragma(Preprocessor& PP,
                      PragmaIntroducerKind Introducer,
                      Token& FirstToken) override {

      Token Tok;
      PP.Lex(Tok);
      if (Tok.isOneOf(tok::eod, tok::eof)) {
        ReportCommandErr(PP, FirstToken.getLocation());
        return;
      }

      const SourceLocation Start = Tok.getLocation();
      SkipToEOD OnExit(PP, Tok);

      // #pragma cling(load, "A")
      bool Paren = Tok.is(tok::l_paren);
      if (Paren)
        PP.Lex(Tok);

      std::string StrBuffer;
      const llvm::StringRef CommandStr = GetName(PP, Tok, StrBuffer);
      if (CommandStr.empty()) {
        ReportCommandErr(PP, Start);
        return;
      }

      const unsigned Command = GetCommand(CommandStr);
      assert(Command != kArgumentsAreLiterals && Command != kExpandEnvCommands);

      if (Command == kInvalidCommand) {
        std::string Flat = CommandStr.str();
        bool Invalid = FlattenArgs(PP, Tok, Paren, Flat);
        // Eat everything until the end and append to Flat.
        if (!Tok.isOneOf(tok::eod, tok::eof)) {
          const std::string Next = GetString(PP, Tok, Invalid);
          if (!Invalid && !Next.empty())
            Flat += " " + Next;
        }
        if (Invalid || !RunCommand(PP, Flat))
          ReportCommandErr(PP, Start);
        return;
      }

      std::string Literal;
      if (!GetNextLiteral(PP, Tok, Literal, Command, CommandStr.data())) {
        PP.Diag(Tok.getLocation(), diag::err_expected_after)
          << CommandStr << "argument";
        return;
      }

      switch (Command) {
        case kLoadFile:
        return LoadCommand(PP, Tok, std::move(Literal));
        case kOptimize:
          return OptimizeCommand(Literal.c_str());

        default:
          do {
            if (Command == kAddLibrary)
              m_Interp.getOptions().LibSearchPath.push_back(std::move(Literal));
            else if (Command == kAddInclude)
              m_Interp.AddIncludePath(Literal);
          } while (GetNextLiteral(PP, Tok, Literal, Command));
          break;
      }
    }
  };
}

void cling::addClingPragmas(Interpreter& interp) {
  Preprocessor& PP = interp.get<Preprocessor>();
  // PragmaNamespace / PP takes ownership of sub-handlers.
  PP.AddPragmaHandler(StringRef(), new ClingPragmaHandler(interp));
}
