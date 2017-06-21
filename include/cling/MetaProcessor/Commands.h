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

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <utility>

namespace llvm {
  class raw_ostream;
  class StringRef;
}

namespace cling {
  class Interpreter;
  class Value;

  namespace meta {
    class CommandHandler;

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


    struct Invocation {
      typedef llvm::StringRef StringRef;
      ///\brief Command arguments.
      StringRef Args;

      ///\brief Interpreter to operate on
      Interpreter& Interp;

      ///\brief Stream the command should write to
      llvm::raw_ostream& Out;
      
      ///\brief Reference to the CommandHandler that invoked this command.
      CommandHandler& CmdHandler;

      ///\brief Value the command may create
      Value* Val;

      ///\brief Executes antother command
      ///
      ///\param[in] Capture - Capture output into this instead of Out.
      ///
      ///\returns CommandResult of the command
      ///
      CommandResult Execute(StringRef Cmd, llvm::raw_ostream* O = nullptr);
    };


    class CommandHandler {
      typedef llvm::SmallVectorImpl<llvm::StringRef> SplitArgumentsImpl;
    public:
      virtual ~CommandHandler();

      ///\brief The argument string type.
      typedef llvm::StringRef SplitArgument;

      ///\brief Convenience type for when 8 or less arguemnts expected.
      typedef llvm::SmallVector<SplitArgument, 8> SplitArguments;

      ///\brief Split the given string into a command-name and list of arguments
      ///
      ///\param[in] Str - String to operate on
      ///\param[out] Out - Where to store the arguments
      ///\param[in] Separators - Separators to split on
      ///
      ///\returns The first string of Str before any Separators
      ///
      static llvm::StringRef Split(llvm::StringRef Str, SplitArgumentsImpl& Out,
                                   llvm::StringRef Separators = " \t\n\v\f\r");

      ///\brief Execute the given command
      ///
      ///\param[in] I - Invocation data for this command.
      ///
      ///\returns CommandResult of the execution
      ///
      virtual CommandResult Execute(const Invocation& I) = 0;
    };
  }
}

#endif // CLING_META_COMMANDS_H
