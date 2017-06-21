//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author: Roman Zulak
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_META_COMMANDS_H
#define CLING_META_COMMANDS_H

namespace llvm {
  class raw_ostream;
  class StringRef;
}

namespace cling {
  class Interpreter;
  class Value;

  namespace meta {
    ///\brief Result of invoking a command via Commands::execute
    ///
    enum CommandResult {
      ///\brief The command was run successfully.
      kCmdSuccess       = 0,
      ///\brief The command failed.
      kCmdFailure       = 1,
      ///\brief The command was not found.
      kCmdNotFound      = 2,
      ///\brief Command exists, but did nothing and parsed no arguments
      kCmdUnimplemented = 3,
      ///\brief Command parsed arguments, but they were not understood
      kCmdInvalidSyntax = 4,
    };

    class CommandHandler {
    public:
      virtual ~CommandHandler();

      struct ParmBlock {
        ///\brief Command and arguments to invoke.
        const llvm::StringRef& CmdStr;

        ///\brief Interpreter to operate on
        Interpreter& Interp;

        ///\brief Stream the command should write to
        llvm::raw_ostream& Out;

        ///\brief Value the command may create
        Value* Val;
      };

      ///\brief Execute the given command
      ///
      ///\returns CommandResult of the execution
      ///
      virtual CommandResult Execute(ParmBlock& Params) = 0;
    };
  }
}

#endif // CLING_META_COMMANDS_H
