//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Axel Naumann <axel@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "InputValidator.h"
#include "MetaLexer.h"
#include <algorithm>

namespace cling {
  bool InputValidator::inBlockComment() const {
    return std::find(m_ParenStack.begin(), m_ParenStack.end(), tok::slash)
             != m_ParenStack.end();
  }

  static int findNestedBlockComments(const char* startPos, const char* endPos) {
    // While probably not standard compliant, it should work fine for the indent
    // Let the real parser error if the balancing is incorrect

    // search forward for //, then backward for block comments
    // */ last, comment has ended, doesn't matter how many /* before
    // /* last, comment has begun, doesn't matter if priors ended or not
    char commentTok = 0;
    while (startPos < endPos) {
      if (*startPos == '/') {
        if (++commentTok == 2) {
          while (endPos > startPos) {
            switch (*endPos) {
              case '*':
                if (commentTok == '*')
                  return -1;
                else
                  commentTok = '/';
                break;
              case '/':
                if (commentTok == '/')
                  return 1;
                else
                  commentTok = '*';
                break;
              default:
                commentTok = 0;
                break;
            }
            --endPos;
          }
          return 0;
        }
      } else if (commentTok)
        commentTok = 0; // need a new start to double slash
      ++startPos;
    }
    return 0;
  }

  static void unwindTokens(std::deque<int>& queue, int tok) {
    assert(!queue.empty() && "Token stack is empty!");
    while (queue.back() != tok) {
      queue.pop_back();
      assert(!queue.empty() && "Token stack is empty!");
    }
    queue.pop_back();
  }

  static bool endIdentifier(Token& Tok, const char* curPos, const char *end,
                            const char** begin, std::deque<int>& m_ParenStack) {
      const int kind = Tok.getKind();
      MetaLexer Lex(curPos);
      Lex.SkipWhitespace();
      Lex.LexAnyString(Tok);
      const llvm::StringRef iDent = Tok.getIdent();
      if (iDent == end) {
        if (m_ParenStack.empty() || m_ParenStack.back() != kind)
          return false;

        m_ParenStack.pop_back();
      }
      else {
        for (unsigned i = 0; begin[i]; ++i) {
          if (iDent.startswith(begin[i])) {
            m_ParenStack.push_back(kind);
            break;
          }
        }
      }

    return true;
  }

  InputValidator::ValidationResult
  InputValidator::validate(llvm::StringRef line, bool objC) {
    ValidationResult Res = kComplete;

    Token Tok;
    const char* curPos = line.data();
    bool multilineComment = inBlockComment();
    int commentTok = multilineComment ? tok::asterik : tok::slash;

    if (!multilineComment && m_ParenStack.empty()) {
      // Only check for 'template' if we're not already indented
      MetaLexer Lex(curPos, true);
      Lex.Lex(Tok);
      curPos = Lex.getLocation();
      if (Tok.is(tok::ident)) {
        if (Tok.getIdent()=="template")
          m_ParenStack.push_back(tok::greater);
      } else
        curPos -= Tok.getLength();   // Static Lex methods below rewuire rewound buffer
    }

    do {
      const char* prevStart = curPos;
      MetaLexer::LexPunctuatorAndAdvance(curPos, Tok);
      const int kind = (int)Tok.getKind();

      if (kind == commentTok) {
        if (kind == tok::slash) {
          if (multilineComment) {
            // exiting a comment, unwind the stack
            multilineComment = false;
            commentTok = tok::slash;
            unwindTokens(m_ParenStack, tok::slash);
          }
          // If we have a closing comment without a start it will be transformed
          // to */; and clang reports an error for both the */ and the ;
          // If we return kIncomplete, then just one error is printed, but too
          // late: after the user has another expression which will always fail.
          // So just deal with two errors for now
          // else if (prevKind == tok::asterik) {
          //  Res = kIncomplete;
          // break;
          // }
          else   // wait for an asterik
            commentTok = tok::asterik;
        }
        else {
          assert(commentTok == tok::asterik && "Comment token not / or *");
          if (!multilineComment) {
            // entering a new comment
            multilineComment = true;
            m_ParenStack.push_back(tok::slash);
          }
          else // wait for closing / (must be next token)
            commentTok = tok::slash;
        }
      }
      else {
        // If we're in a multiline, and waiting for the closing slash
        // we gonna have to wait for another asterik first
        if (multilineComment) {
          if (kind == tok::eof) {
            switch (findNestedBlockComments(prevStart, curPos)) {
              case -1: unwindTokens(m_ParenStack, tok::slash);
              case  1:
              case  0: break;
              default: assert(0 && "Nested block comment count"); break;
            }
            // eof, were done anyway
            break;
          }
          else if (commentTok == tok::slash)
            commentTok = tok::asterik;
        }

        // Balance braces
        if (kind >= tok::l_square && kind <= tok::r_brace) {
          if (kind & (tok::r_square|tok::r_paren|tok::r_brace)) {
            // closing the proper one?
            if (m_ParenStack.empty() || m_ParenStack.back() != (kind >> 1)) {
              if (multilineComment)
                continue;

              Res = kMismatch;
              break;
            }
            m_ParenStack.pop_back();

            // Right brace will pop a template if their is one
            if (kind == tok::r_brace && m_ParenStack.size() == 1 ) {
              if (m_ParenStack.back() == tok::greater)
                m_ParenStack.pop_back();
            }
          }
          else
            m_ParenStack.push_back(kind);
        }
        else if (kind == tok::hash) {
          const char* begin[] = { "if", 0 };
          if (!endIdentifier(Tok, curPos, "endif", begin, m_ParenStack)) {
            Res = kMismatch;
            break;
          }
        }
        else if (objC && kind == tok::at) {
          const char* begin[] = { "interface", "implementation", "protocol", 0 };
          if (!endIdentifier(Tok, curPos, "end", begin, m_ParenStack)) {
            Res = kMismatch;
            break;
          }
        }
        else if (kind == tok::semicolon) {
          // Template forward declatation
          if (m_ParenStack.size() == 1 && m_ParenStack.back()==tok::greater)
            m_ParenStack.pop_back();
        }
        else if (Tok.isOneOf(tok::stringlit|tok::charlit))
          MetaLexer::LexQuotedStringAndAdvance(curPos, Tok);
      }
    } while (Tok.isNot(tok::eof));

    if (!m_ParenStack.empty() && Res != kMismatch)
      Res = kIncomplete;

    if (!m_Input.empty()) {
      if (!m_ParenStack.empty() && (m_ParenStack.back() == tok::stringlit
                                    || m_ParenStack.back() == tok::charlit))
        m_Input.append("\\n");
      else
        m_Input.append("\n");
    }
    else
      m_Input = "";

    m_Input.append(line);

    return Res;
  }

  void InputValidator::reset(std::string* input) {
    if (input) {
      assert(input->empty() && "InputValidator::reset got non empty argument");
      input->swap(m_Input);
    } else
      std::string().swap(m_Input);

    std::deque<int>().swap(m_ParenStack);
  }
} // end namespace cling
