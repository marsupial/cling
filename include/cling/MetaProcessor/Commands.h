//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author: Jermaine Dupris <support@greyangle.com>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_META_COMMANDS_H
#define CLING_META_COMMANDS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Optional.h"

namespace llvm {
  class raw_ostream;
}

namespace cling {
  class Value;
  class Interpreter;

  namespace meta {
    class Processor;
    class Parser;
    class Actions;
    class Token;
    class Commands;

    // So CommandArguments isn't constructable by anyone else
    namespace {
      class CommandTable;
    }
    
    ///\brief Result of invoking a command via Commands::execute
    ///
    enum CommandResult {
      ///\brief The command was run successfully.
      kCmdSuccess       = 0,
      ///\brief The command failed.
      kCmdFailure       = 1,
      ///\brief The command was not found.
      kCmdNotFound      = 2,
      ///\brief Command parsed arguments, but they were not understood
      kCmdInvalidSyntax = 3, //
      ///\brief Command did nothing and parsed no arguments
      kCmdUnimplemented = 4,
    };

    ///\brief Class that encapsulates argument parsing and state a command
    /// can operate on.
    ///
    class CommandArguments {
      Parser& m_Parser;

      const Token& skipToNextToken();

      CommandArguments(Parser&, Interpreter&, llvm::raw_ostream&,
                       Processor*, Value*);

      // Only to be constructed by the Commands implementation
      friend class meta::CommandTable;

    public:

      typedef Token Argument;

      ///\brief The Interpreter instance the command will act on.
      ///
      cling::Interpreter& Interpreter;

      ///\brief The stream the command should ouput to
      ///
      llvm::raw_ostream& Output;

      ///\brief The name of the command i.e. argv[0]
      ///
      const llvm::StringRef CommandName;

      ///\brief The MetaProcessor who invoked the command, or nullptr if invoked
      /// via #pragma cling. kCmdRequireProcessor should be set if this needs
      // to be valid for the command to function.
      ///
      meta::Processor *Processor;

      ///\brief Storage for any cling::Value the command will create. Can be 0.
      ///
      Value* OutValue;

      ///\brief Get a reference to the next argument delimited by tok
      const Argument& nextArg(unsigned tok);

      ///\brief Get a string for the next argument delimited by tok
      llvm::StringRef nextString(unsigned tok);

      ///\brief Get a reference to the current argument
      const Argument& curArg();

      ///\brief Interpret the next argument as optional integer value.
      ///
      ///\param[in] ParseBool - Interpret 'true' as 1 and 'false' 0.
      ///\param[out] WasBool - The argument was 'true' or 'false' string.
      ///
      llvm::Optional<int> optionalInt(bool ParseBool = true, bool* WasBool = 0);

      ///\brief Returns the reaminaing arguments as one string
      llvm::StringRef remaining();

      ///\brief Returns the next argument
      const Argument& nextArg();

      ///\brief Returns the next argument as string
      llvm::StringRef nextString();

      ///\brief Return the Actions of the Processor (use kCmdRequireProcessor).
      Actions& actions() const;

      ///\brief Execute another command, capturing it's output or value.
      ///
      ///\param[in] Cmd - Command name and arguments
      ///\param[in] Out - Capture the commands output
      ///\param[out] Value - Capture the Value the command has created. If null,
      ///       pass any Value created on to the invoker of the current command.
      ///
      ///
      CommandResult execute(llvm::StringRef Cmd,
                            std::string* Out = nullptr,
                            Value* Value = nullptr);
    };


    class Commands {
    protected:
      ///\brief Basic callback function that can be registered.
      ///
      typedef CommandResult (*CmdCallback0)(CommandArguments&);

      ///\brief Simplified interface for a command that only needs to operate
      /// on one argumenta at a time.
      ///
      /// If there were no arguemnts it is called exactly once with empty string
      /// Otherwise it is invoked for each argument provided.
      ///
      typedef CommandResult (*CmdCallback1)(CommandArguments&, llvm::StringRef);

      struct CommandObj;
      virtual ~Commands() {} // To keep compilers from complaining

    public:

      ///\brief Flags providing information about the command
      ///
      enum CommandFlags {
        ///\brief The command cannot be matched by name alone, i.e. .2>, .1>
        kCmdCustomSyntax     = 1,
        ///\brief The command requires a meta::Processor object to execute
        kCmdRequireProcessor = 2,
        ///\brief The command is for debug puposes, not shown unless '.? all'
        kCmdDebug            = 4,
      };

      ///\brief Get a reference to the Commands object, creating it if it
      /// doesn't already exist. You should avoid populating the object until
      /// a command has been requested.
      ///
      ///\param[in] Populate - Compete initilaization and build a table of the
      ///       builtin commands, or just get a reference.
      ///
      ///\returns A reference to the Command object.
      ///
      static Commands& get(bool Populate = false);


      ///\brief Add a new command
      ///
      ///\param[in] Name - Alternate name for the command
      ///\param[in] Callback - CmdCallback0 or CmdCallback1 type function
      ///\param[in] Syntax - Syntax of the command: <required> [optional]
      ///\param[in] Help - Description of what the command does
      ///\param[in] Flags - CommandFlags describing the command
      ///
      ///\returns a new CommandObj
      ///
      template <class T> CommandObj*
      add(const char* Name, T Callback,
               const char* Syntax = nullptr, const char* Help = nullptr,
               unsigned Flags = 0);


      ///\brief Alias an existing command to a new name.
      ///
      ///\param[in] Name - Alternate name for the command
      ///\param[in] Cmd - CommandObj* from previous call to Commands::add
      ///
      ///\returns CommandObj passed in Cmd
      ///
      CommandObj* alias(const char* Name, CommandObj* Cmd);


      ///\brief Execute the given command
      ///
      ///\param[in] CmdStr - Command and arguments to invoke
      ///\param[in] Interpreter - Interpreter to operate on
      ///\param[in] Out - Stream the command should write to
      ///\param[in] Processor - meta::Processor object
      ///\param[out] Value - Value the command may create
      ///
      ///\returns CommandResult of the exection
      ///
      virtual CommandResult
      execute(llvm::StringRef CmdStr, Interpreter&, llvm::raw_ostream& Out,
              Processor* = nullptr, Value* = nullptr) = 0;
    };
  }
}

#endif // CLING_META_COMMANDS_H
