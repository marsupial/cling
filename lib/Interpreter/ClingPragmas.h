//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Axel Naumann <axel@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_PRAGMAS_H
#define CLING_PRAGMAS_H

#include "clang/Lex/Pragma.h"
#include <string>

namespace clang {
  class Lexer;
}

namespace cling {
  class Interpreter;
  namespace meta {
    class Commands;
  }
  
  class ClingPragmaHandler: public clang::PragmaHandler {
    Interpreter& m_Interp;
    meta::Commands* m_Commands;

    void ReportCommandErr(clang::Preprocessor&, const clang::Token&);
    void LoadCommand(clang::Preprocessor&, clang::Token&, std::string);
    int GetCommand(const llvm::StringRef CommandStr) const;
    bool RunCommand(clang::Lexer*, const llvm::StringRef&) const;
    bool GetNextLiteral(clang::Preprocessor&, clang::Token&,
                        std::string&, const char* = nullptr) const;
  public:
    ClingPragmaHandler(Interpreter& interp);

    void HandlePragma(clang::Preprocessor&,
                      clang::PragmaIntroducerKind,
                      clang::Token&) override;

    static ClingPragmaHandler* install(Interpreter&);

    void setCommands(meta::Commands* Cmds) { m_Commands = Cmds; }
  };
} // namespace cling

#endif // CLING_PRAGMAS_H
