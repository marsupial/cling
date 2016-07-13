//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author: Jermaine Dupris <support@greyangle.com>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_META_COMMAND_TABLE_H
#define CLING_META_COMMAND_TABLE_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringMap.h"

namespace llvm {
  class raw_ostream;
}

namespace cling {
  class Value;
  class MetaProcessor;
  class Interpreter;

  namespace meta {
    class CommandArguments;

    class CommandTable
    {
      struct CommandObj {
        typedef bool (*CommandCallback0)(CommandArguments&);
        typedef bool (*CommandCallback1)(CommandArguments&, llvm::StringRef);

        union CommandCallback {
          CommandCallback0 Callback0;
          CommandCallback1 Callback1;
          CommandCallback() : Callback0(nullptr) {}
        } Callback;
        const char* Syntax;
        const char* Help;
        unsigned Flags : 6;
        
        // llvm::StringMap needs this
        CommandObj() : Syntax(nullptr), Help(nullptr), Flags(0) {}
      };
      llvm::StringMap<CommandObj*> m_Commands;

      static void showHelp(const llvm::StringMap<CommandObj*>::iterator&,
                           llvm::raw_ostream& Out);
      static bool doHelpCommand(CommandArguments& Params, llvm::StringRef Cmd);
      static bool sort(const llvm::StringMap<CommandObj*>::iterator&,
                       const llvm::StringMap<CommandObj*>::iterator&);

      // For adding a command with a different name
      CommandObj* add(const char* Name, CommandObj*);

    public:
      static CommandTable* create(bool InstanceOnly = false);
      virtual ~CommandTable();

      enum CommandFlags {
        kCmdCallback0        = 0,
        kCmdCallback1        = 2,
        kCmdCustomSyntax     = 4,
        kCmdRequireProcessor = 8,
        kCmdDebug            = 16,
        kCmdExperimental     = 32,
      };

      template <class T> CommandObj*
      add(const char* Name, T Callback,
               const char* Syntax = nullptr, const char* Help = nullptr,
               unsigned = kCmdCallback0);
  
      // FIXME: only virtual to avoid linking / circular dependencies
      virtual int execute(llvm::StringRef, Interpreter&, llvm::raw_ostream&,
                  MetaProcessor* = nullptr, Value* = nullptr);
    };
  }
}

#endif // CLING_META_COMMAND_TABLE_H
