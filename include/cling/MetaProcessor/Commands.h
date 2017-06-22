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
      typedef llvm::SmallVectorImpl<std::pair<llvm::StringRef, bool>>
          SplitArgumentsImpl;
    public:
      virtual ~CommandHandler();

      ///\brief The argument string and whether it contained an escape sequence.
      /// The assumtion is more often than not escape sequnces are not
      /// neccessary and can speed things up by not allocating a new string
      /// for every argument. 
      typedef std::pair<llvm::StringRef, bool> SplitArgument;

      ///\brief Convenience type for when 8 or less arguemnts expected.
      typedef llvm::SmallVector<SplitArgument, 8> SplitArguments;

      ///\brief Control how the string is split
      enum SplitFlags {
        ///\brief Don't include the first 'argument' in the output, return it
        kPopFirstArgument = 1,
        ///\brief Group items by <>, {}, [], (), '', and "" as well
        kSplitWithGrouping = 2,
      };

      ///\brief Split the given string into a command-name and list of arguments
      ///
      ///\param[in] Str - String to operate on
      ///\param[out] Out - Where to store the arguments
      ///\param[in] Flags - The SplitFlags to use while building the output
      ///\param[in] Separators - Separators to split on
      ///
      ///\returns An empty string when kPopFirstArgument is not set, otherwise
      /// the first sequence in Str before any Separators occured.
      ///
      static llvm::StringRef Split(llvm::StringRef Str, SplitArgumentsImpl& Out,
                                   unsigned Flags = 0,
                                   llvm::StringRef Separators = " \t\n\v\f\r");

      ///\brief Un-escape the given string
      ///
      static std::string Unescape(llvm::StringRef Str);

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
