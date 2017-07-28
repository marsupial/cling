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

#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

#include <utility>
#include <vector>

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
      ///\brief The name of the command as it was invoked.
      StringRef Cmd;

      ///\brief Interpreter to operate on
      Interpreter& Interp;

      ///\brief Stream the command should write to
      llvm::raw_ostream& Out;
      
      ///\brief Reference to the CommandHandler that invoked this command.
      CommandHandler& CmdHandler;

      ///\brief Value the command may create
      Value* Val;

      ///\brief Command arguments.
      StringRef Args;

      ///\brief Executes antother command
      ///
      ///\param[in] Capture - Capture output into this instead of Out.
      ///
      ///\returns CommandResult of the command
      ///
      CommandResult Execute(StringRef Cmd, llvm::raw_ostream* O = 0) const;
    };


    class CommandHandler {
    public:
      ///\brief The argument string and whether it contained an escape sequence.
      /// The assumption is more often than not escape sequnces are not
      /// neccessary and can speed things up by not allocating a new string
      /// for every argument.
      class SplitArgument {
        mutable llvm::PointerIntPair<std::string*, 1> UnescapedStr;

      public:
        const llvm::StringRef Str;
        const char Group;

        SplitArgument(llvm::StringRef S = {}, bool E = false, char G = 0)
            : UnescapedStr(nullptr, E), Str(S), Group(G) {}
        SplitArgument(SplitArgument&& O)
            : UnescapedStr(O.UnescapedStr), Str(O.Str), Group(O.Group) {
          O.UnescapedStr.setPointer(nullptr);
        }
        SplitArgument(const SplitArgument&) = delete;

        ~SplitArgument() {
          if (std::string* E = UnescapedStr.getPointer()) delete E;
        }

        ///\brief Convert the string argument to an llvm::Optional.
        ///
        ///\param[out] WasBool - Whether the string was converted as a boolean.
        ///
        template <class T>
        llvm::Optional<T> Optional(bool* WasBool = nullptr) const;

        bool empty() const { return Str.empty(); }
        bool operator==(const char* RHS) const { return Str == RHS; }
        bool operator!=(const char* RHS) const { return Str != RHS; }
        operator llvm::StringRef() const { return Str; }

        bool escaped() const { return UnescapedStr.getInt(); }
        operator const std::string&() const {
          std::string* E = UnescapedStr.getPointer();
          if (!E) {
            E = new std::string(escaped() ? CommandHandler::Unescape(Str)
                                          : Str.str());
            UnescapedStr.setPointer(E);
          }
          return *E;
        }
      };

      ///\brief Convenience type for when 8 or less arguemnts expected.
      typedef llvm::SmallVector<SplitArgument, 8> SplitArguments;

      typedef llvm::SmallVectorImpl<SplitArgument> SplitArgumentsList;
      typedef const SplitArgument& Argument;
      typedef const std::string& EscArgument;
      typedef std::vector<std::string> EscapedArgumentsList;
      typedef void* CommandID;

    private:
      template <typename T>
      struct Supported {
        template <typename... Types> struct Convertible {
          typedef void type;
          enum { value = false, same = false, convert = false };
        };

        template <typename Current, typename... Types>
        struct Convertible<Current, Types...> {
          typedef typename std::is_same<T, Current> Same;
          typedef typename std::is_convertible<T, Current> Convert;
          typedef Convertible<Types...> Next;

          // If the type matches, select it. Otherwise check the remaining types
          // for an exact match and return it if found. If still nothing found
          // check the remaining types for a conversion operator and return it
          // if found.  If still nothing found, return if this can convert.
          //
          // Should be fairly obvious as to why any exact match is prefered.
          // Why conversion chooses later entries is so that lambdas will
          // be converted into std::functions rather than a C function pointer
          // so that lambda -> std::function -> function pointer won't occur.
          typedef typename std::conditional<Same::value, Current,
            typename std::conditional<Next::same, typename Next::type,
              typename std::conditional<Next::convert, typename Next::type,
                typename std::conditional<Convert::value, Current,
                                          typename Next::type
                >::type
              >::type
            >::type
          >::type type;

          enum { same = Same::value ? 1 : Next::same,
                 convert = Convert::value ? 1 : Next::convert,
                 value = same || convert };
        };

        typedef Convertible<
          CommandResult (*)(const Invocation& I),
          CommandResult (*)(const Invocation& I, const SplitArgumentsList&),
          CommandResult (*)(const Invocation& I, Argument),
          CommandResult (*)(const Invocation& I, Argument, Argument),
          CommandResult (*)(const Invocation& I, const EscapedArgumentsList&),
          CommandResult (*)(const Invocation& I, EscArgument),
          CommandResult (*)(const Invocation& I, EscArgument, EscArgument),
          std::function<CommandResult(const Invocation&)>,
          std::function<CommandResult(const Invocation&, const SplitArgumentsList&)>,
          std::function<CommandResult(const Invocation&, Argument)>,
          std::function<CommandResult(const Invocation&, Argument, Argument)>,
          std::function<CommandResult(const Invocation&, const EscapedArgumentsList&)>,
          std::function<CommandResult(const Invocation&, EscArgument)>,
          std::function<CommandResult(const Invocation&, EscArgument, EscArgument)>
        > Converted;

        typedef typename Converted::type type;
        enum { same = Converted::same,
               convert = Converted::convert,
               value = Converted::value };
      };

      // Work-horse for AddCommand to avoid name mangling errors on gcc.
      template <class T> CommandID
      DoAddCommand(std::string Name, T Obj, std::string Help);

    public:
      virtual ~CommandHandler();

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
      static llvm::StringRef Split(llvm::StringRef Str, SplitArgumentsList& Out,
                                   unsigned Flags = 0,
                                   llvm::StringRef Separators = " \t\n\v\f\r");

      ///\brief Un-escape the given string
      ///
      static std::string Unescape(llvm::StringRef Str);

      ///\brief Add a given command
      ///
      ///\param[in] Name - The command name.
      ///\param[in] Function - The callback to invoke.
      ///\param[in] Help - The help / syntax to be displayed.
      ///
      ///\returns A CommandID that can be used to remove the command on success,
      /// or nullptr on failure.
      ///
      template <class T>
      typename std::enable_if<Supported<T>::same, CommandID>::type
      AddCommand(std::string Name, T Obj, std::string Help) {
        return DoAddCommand(std::move(Name), std::move(Obj), std::move(Help));
      }

      ///\brief Lambda variant of above.
      ///
      template <class T>
      typename std::enable_if<!Supported<T>::same && Supported<T>::convert,
                              CommandID>::type
      AddCommand(std::string Name, T Obj, std::string Help) {
        typename Supported<T>::type F(Obj);
        return DoAddCommand(std::move(Name), std::move(F), std::move(Help));
      }

      ///\brief Alias a new command name to an existing command.
      ///
      ///\param[in] Name - The name to invoke with.
      ///\param[in] ID - The original command name.
      ///
      ///\returns True if success, otherwise false.
      ///
      bool
      Alias(std::string Name, CommandID ID);

      ///\brief Remove a previously registered command, and sets it to an
      //// invalid value.
      ///
      void RemoveCommand(CommandID& Key);

      ///\brief Remove all commands with the given name.
      ///
      ///\returns The number of commands removed
      ///
      size_t RemoveCommand(const std::string& Name);

      ///\brief Remove all registered named commands.
      ///
      virtual void Clear();

      ///\brief Execute the given command
      ///
      ///\param[in] I - Invocation data for this command.
      ///
      ///\returns CommandResult of the execution
      ///
      virtual CommandResult Execute(const Invocation& I);
    };
  }
}

// Helper function to dump a CommandHandler::Argument into an llvm::raw_ostream
namespace llvm {
raw_ostream& operator<<(raw_ostream&, cling::meta::CommandHandler::Argument&);
}

#endif // CLING_META_COMMANDS_H
