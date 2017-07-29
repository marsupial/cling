//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author: Roman Zulak
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/MetaProcessor/Commands.h"
#include "cling/Utils/Casting.h"
#include "cling/Utils/Output.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <vector>

namespace cling {
namespace meta {

CommandHandler::~CommandHandler() {}

static char IsEscape(char Current, char Next) {
  if (Current == '\\') {
    switch (Next) {
      case 'a':  return '\a';
      case 'b':  return '\b';
      case 'f':  return '\f';
      case 'n':  return '\n';
      case 'r':  return '\r';
      case 't':  return '\t';
      case 'v':  return '\v';
      case '\\': return '\\';
      case '\'': return '\'';
      case '\"': return '"';
      case '\n': return '\n';
      case '\r': return '\r';
      default: break;
    }
  }
  return 0;
}

std::string
CommandHandler::Unescape(llvm::StringRef Str) {
  const size_t N = Str.size();
  if (N == 0)
    return "";

  std::string Out;
  Out.reserve(N);

  const char* Data = Str.data(), *End = Data + N - 1;
  while (Data < End) {
    char E;
    if (LLVM_UNLIKELY(E = IsEscape(Data[0], Data[1]))) {
      Out += E;
      Data += 2;
    } else
      Out += *Data++;
  }
  if (Data == End)
    Out += *End;

  return Out;
}

template <class T> static llvm::Optional<T>
ArgToOptional(const llvm::StringRef& Arg, bool* WasBool) {
  T Val;
  // Returns true on error!
  if (Arg.getAsInteger(10, Val)) {
    if (Arg.equals_lower("true"))
      Val = 1;
    else if (Arg.equals_lower("false"))
      Val = 0;
    else
      return llvm::Optional<T>();

    if (WasBool)
      *WasBool = true;
  } else if (WasBool)
    *WasBool = false;

  return Val;
}

template <> llvm::Optional<unsigned>
CommandHandler::SplitArgument::Optional<unsigned>(bool* WasBool) const {
  return ArgToOptional<unsigned>(RawStr, WasBool);
}

template <> llvm::Optional<int>
CommandHandler::SplitArgument::Optional<int>(bool* WasBool) const {
  return ArgToOptional<int>(RawStr, WasBool);
}

template <> llvm::Optional<bool>
CommandHandler::SplitArgument::Optional<bool>(bool* WasBool) const {
  return ArgToOptional<bool>(RawStr, WasBool);
}

void CommandHandler::SplitArgument::dump(llvm::raw_ostream* OS) {
  if (!OS) OS = &cling::outs();
  *OS << "{\"" << RawStr << "\"";
  if (Escaped) *OS << ", Escaped";
  if (Group) *OS << ", Group: '" << Group << '\'';
  *OS << "}";
}

llvm::StringRef
CommandHandler::Split(llvm::StringRef Str, SplitArguments& Out,  unsigned Flags,
                      llvm::StringRef Separators) {
  size_t Start = 0;
  const size_t Len = Str.size(), LenMinOne = Len -1;
  llvm::StringRef CmdName;

  bool HadEscape = false;
  size_t Group = llvm::StringRef::npos;
  const llvm::StringRef GrpBegin("\"'{[<(/"), GrpEnd("\"'}]>)*");
  const size_t Comment = GrpBegin.size() - true;

  for (size_t Idx = 0; Idx < Len; ++Idx) {
    const char C = Str[Idx];

    // Look ahead
    if (Idx < LenMinOne) {
      // Check for escape sequence, which should always be a part of the current
      // argument/group.
      if (const char E = IsEscape(C, Str[Idx+1])) {
        HadEscape = true;
        // The next character is actually part of this one.
        ++Idx;

        // It cannot start or close a group, i.e ' \"quoted\" '
        if (Group != llvm::StringRef::npos)
          continue;

        // But if an argument is open, it can close it out. ' arg1\\\narg2 '
        if (Start < Idx - 1)
          continue;

        // Make sure the next argument starts after this if the escaped char
        // is also a separator, like a line continuation.
        if (Separators.find(E) != llvm::StringRef::npos) {
          Start = Idx + 1;
          HadEscape = false;
        }
        continue;
      }
    }

    // Groups
    if (Flags & kSplitWithGrouping) {
      if (Group != llvm::StringRef::npos) {
        // See if the character is the end of the current group type.
        if (GrpEnd.find(C) == Group) {
          if (Group != Comment || Idx > LenMinOne || Str[Idx+1] == '/') {
            Out.emplace_back(Str.slice(Start, Idx), HadEscape, GrpBegin[Group]);
            // Advance two characters for block comment
            Idx += Group == Comment;
            Start = Idx + 1;
            Group = llvm::StringRef::npos;
            HadEscape = false;
          }
        }
        // Already in a group, eat all until the end of it.
        continue;
      }
      // Maybe a group beginning
      Group = GrpBegin.find(C);
      if (Group != llvm::StringRef::npos) {
        //
        if (Group == Comment && Idx <= LenMinOne) {
          switch (Str[Idx+1]) {
            // Line comment,
            case '/':
              Out.emplace_back(Str.substr(Start+2), HadEscape, '/');
              return CmdName;
            // Block comment
            case '*': ++Idx; break;
            default: Group = llvm::StringRef::npos; break;
          }
        }
        if (Group != llvm::StringRef::npos) {
          Start = Idx + 1;
          HadEscape = false;
          continue;
        }
      }
    }

    // Splitting
    if (Separators.find(C) == llvm::StringRef::npos)
      continue;

    // Make sure this match shouldn't be considered part of the last one.
    if (Idx > Start) {
      if (CmdName.empty() && (Flags & kPopFirstArgument))
        CmdName = Str.slice(Start, Idx);
      else
        Out.emplace_back(Str.slice(Start, Idx), HadEscape);
      HadEscape = false;
    }
    Start = Idx + 1;
  }

  // Unterminated group keep it as it was
  if (Group != llvm::StringRef::npos)
    --Start;

  // Grab whatever remains
  if (CmdName.empty() && (Flags & kPopFirstArgument))
    CmdName = Str.substr(Start);
  else if (Start != Len)
    Out.emplace_back(Str.substr(Start), HadEscape);

  return CmdName;
}

template <typename List, typename Str, bool C = false> struct CallbackTypes {
  typedef std::function<CommandResult(const Invocation&)> Base;
  typedef std::function<CommandResult(const Invocation&, const List&)> ArgList;
  typedef std::function<CommandResult(const Invocation&, Str)> Single;
  typedef std::function<CommandResult(const Invocation&, Str, Str)> Dual;
};

template <typename List, typename Str> struct CallbackTypes<List, Str, true> {
  typedef CommandResult (*Base)(const Invocation&);
  typedef CommandResult (*ArgList)(const Invocation&, const List&);
  typedef CommandResult (*Single)(const Invocation&, Str);
  typedef CommandResult (*Dual)(const Invocation&, Str, Str);
};

typedef CallbackTypes<CommandHandler::SplitArguments, CommandHandler::Argument> ObjFunction;
typedef CallbackTypes<CommandHandler::SplitArguments, CommandHandler::Argument, 1> FreeFunction;

struct Value { template <class T> struct value_type { typedef T type; }; };
struct Pointer { template <class T> struct value_type { typedef T* type; }; };

template <typename T, typename Storage> struct CallbackData {
  union {
    typename Storage::template value_type<typename T::Base>::type B;
    typename Storage::template value_type<typename T::ArgList>::type A;
    typename Storage::template value_type<typename T::Single>::type S;
    typename Storage::template value_type<typename T::Dual>::type D;
  };
};

namespace {
class Callback {
  // Data members
  union {
    CallbackData<FreeFunction, Value> Free;
    CallbackData<ObjFunction, Pointer> Obj;
    void* FuncPtr;
  };
  unsigned Flags : 5;

  enum {
    kObjFunction = CommandHandler::kCommandFlagsEnd,
    kArgList = CommandHandler::kCommandFlagsEnd * 2,
    kSingleStr = CommandHandler::kCommandFlagsEnd * 4,
    kDualStr = CommandHandler::kCommandFlagsEnd * 8,
    kNoStrArgs = 0,
  };

  // Overloads to -easily- get get the proper flags for the type of arguments
  // the callback takes. kObjFunction must be set explicitly as both
  // free-standing and std::function callbacks with no strings are ambiguous.
  static constexpr unsigned GetArgFlags(const ObjFunction::Base& B) { return kNoStrArgs; }
  static constexpr unsigned GetArgFlags(const FreeFunction::Base&) { return kNoStrArgs; }

  // Specializing for free-standing shouln't really be neccesssary, but clang is
  // complaining about ambiguity otherwise.
  static constexpr unsigned GetArgFlags(const FreeFunction::ArgList&) { return kArgList; }
  static constexpr unsigned GetArgFlags(const FreeFunction::Single&) { return kSingleStr; }
  static constexpr unsigned GetArgFlags(const FreeFunction::Dual&) { return kDualStr; }

  static constexpr unsigned GetArgFlags(const ObjFunction::ArgList&) { return kArgList; }
  static constexpr unsigned GetArgFlags(const ObjFunction::Single&) { return kSingleStr; }
  static constexpr unsigned GetArgFlags(const ObjFunction::Dual&) { return kDualStr; }

  template <typename CFunc, typename ObjCall, typename... Ts>
  CommandResult Call(CFunc Func, const ObjCall& Obj, Ts&&... Args) {
    if (Flags & kObjFunction)
      return (*Obj)(std::forward<Ts>(Args)...);
    return Func(std::forward<Ts>(Args)...);
  }

  bool Skip(CommandHandler::Argument Arg) const {
    return Flags & CommandHandler::kPassComments ? false : Arg.Group == '/';
  }

  template <typename CFunc, typename ObjCall> CommandResult
  CallWithOne(const CFunc Func, const ObjCall& Obj, const Invocation& I,
              const CommandHandler::SplitArguments& Args) {
    for (const auto& Arg : Args) {
      if (Skip(Arg))
        continue;
      if (const CommandResult Result = Call(Func, Obj, I, Arg))
        return Result;
    }
    return kCmdSuccess;
  }

  template <typename CFunc, typename ObjCall> CommandResult
  CallWithTwo(const CFunc Func, const ObjCall& Obj, const Invocation& I,
              const CommandHandler::SplitArguments& Args) {
    const CommandHandler::SplitArgument Empty;
    for (auto Itr = Args.begin(), End = Args.end(); Itr < End; ++Itr) {
      const auto& A = *Itr;
      if (Skip(A))
        continue;
      const CommandHandler::SplitArgument* B = nullptr;
      while (!B && ++Itr < End) {
        const CommandHandler::SplitArgument& Next = *Itr;
        B = Skip(Next) ? nullptr : &Next;
      }

      if (const CommandResult Result = Call(Func, Obj, I, A, B ? *B : Empty))
        return Result;
    }
    return kCmdSuccess;
  }

public:
  template <class FrFunc>
  Callback(FrFunc Func, unsigned F) : Flags(F | GetArgFlags(Func)) {
    // Make sure the data is layed out as required.
    static_assert(sizeof(FuncPtr) == sizeof(Func), "Invalid assignment");
    static_assert(sizeof(Free) == sizeof(Obj), "Invalid assignment");
    assert(&FuncPtr == static_cast<void*>(&Free.B) && "Bad overlap");
    assert(&FuncPtr == static_cast<void*>(&Obj.S) && "Bad overlap");

    FuncPtr = cling::utils::FunctionToVoidPtr(Func);
  }

  template <class StdFunc>
  Callback(std::function<StdFunc> Func, unsigned F)
      : Flags(F | kObjFunction | GetArgFlags(Func)) {
    FuncPtr = new std::function<StdFunc>(std::move(Func));
  }

  ~Callback() {
    if (Flags & kObjFunction) {
      switch (Flags & (kArgList|kSingleStr|kDualStr)) {
        case kNoStrArgs: delete Obj.B; break;
        case kArgList:   delete Obj.A; break;
        case kSingleStr: delete Obj.S; break;
        case kDualStr:   delete Obj.D; break;
        default: llvm_unreachable("Unkown function type not deallocated");
      }
    }
  }

  CommandResult Dispatch(const Invocation& I,
                         CommandHandler::SplitArguments& Args) {
    // Simplest callback (Invocation& I);
    if (!Flags || Flags == kObjFunction)
      return Call(Free.B, Obj.B, I);

    // callback (Invocation& I, ArgList);
    if (Flags & kArgList)
      return Call(Free.A, Obj.A, I, Args);

    // callback (Invocation& I, Arg);
    if (Flags & kSingleStr)
      return CallWithOne(Free.S, Obj.S, I, Args);

    // callback (Invocation& I, Arg0, Arg1);
    if (Flags & kDualStr)
      return CallWithTwo(Free.D, Obj.D, I, Args);

    return kCmdUnimplemented;
  }
};
typedef std::multimap<std::string, std::unique_ptr<Callback>> CallbackList;

CallbackList m_Callbacks;

union Key {
  CommandHandler::CommandID ID;
  CallbackList::iterator Itr;

  Key(CallbackList::iterator I) : Itr(I) {}
  Key(CommandHandler::CommandID I) : ID(I) {}
  ~Key() {}

  CommandHandler::CommandID Check(CallbackList::iterator End) {
    static_assert(sizeof(ID) <= sizeof(Itr), "CommandID cannot hold value");
    if (Itr == End)
      return nullptr;
    assert(ID != nullptr && "CommandID is supposed to be invalid");
    return ID;
  }
};

template <class T>
static CommandHandler::CommandID AddIt(CallbackList& L, std::string Name,
                                       T Func, std::string Help, unsigned F) {
  Key K(L.emplace(std::move(Name),
                  llvm::make_unique<Callback>(std::move(Func), F)));
  return K.Check(L.end());
}

}

void CommandHandler::RemoveCommand(CommandID& ID) {
  if (ID != nullptr) {
    Key K(ID);
    m_Callbacks.erase(K.Itr);
    ID = nullptr;
  }
}

size_t CommandHandler::RemoveCommand(const std::string& Name) {
  return m_Callbacks.erase(Name);
}

void CommandHandler::Clear() {
  m_Callbacks.clear();
}

#define CH_ADD_COMMAND_SPEC(Type)                                              \
  template <> CommandHandler::CommandID CommandHandler::DoAddCommand<Type>(    \
      std::string N, Type F, std::string H, unsigned Flgs) {                   \
    return AddIt(m_Callbacks, std::move(N), std::move(F), std::move(H), Flgs); \
  }

CH_ADD_COMMAND_SPEC(FreeFunction::Base)
CH_ADD_COMMAND_SPEC(FreeFunction::ArgList)
CH_ADD_COMMAND_SPEC(FreeFunction::Single)
CH_ADD_COMMAND_SPEC(FreeFunction::Dual)

CH_ADD_COMMAND_SPEC(ObjFunction::Base)
CH_ADD_COMMAND_SPEC(ObjFunction::ArgList)
CH_ADD_COMMAND_SPEC(ObjFunction::Single)
CH_ADD_COMMAND_SPEC(ObjFunction::Dual)

bool CommandHandler::Alias(std::string Name, CommandID ID) {
  return false;
}

CommandResult CommandHandler::Execute(const Invocation& I) {
  llvm::SmallVector<SplitArgument, 8> Args;
  llvm::StringRef CmdName;
  if (!I.Args.empty()) {
    CmdName = I.Cmd;
    Split(I.Args, Args, kSplitWithGrouping);
  } else
    CmdName = Split(I.Cmd, Args, kPopFirstArgument | kSplitWithGrouping);

#if 0
  llvm::raw_ostream& OS = cling::outs();
  OS << "CMD: " << CmdName << "\n";
  for (auto& Arg : Args) { OS << " arg: "; Arg.dump(&OS); OS << "\n"; }
#endif

  const auto Cmds = m_Callbacks.equal_range(CmdName);
  if (Cmds.first == Cmds.second)
    return kCmdNotFound;

  // Build the new Invocation with CmdName and Args properly split and make
  // sure CmdHandler points to this.
  const Invocation Adjusted = {
      CmdName, I.Interp, I.Out, *this, I.Val,
      !I.Args.empty()
          ? I.Args
          : I.Cmd.substr((CmdName.data() + CmdName.size()) - I.Cmd.data()),
  };

  for (auto Cmd = Cmds.first; Cmd != Cmds.second; ++Cmd) {
    const CommandResult Result = Cmd->second->Dispatch(Adjusted, Args);
    if (Result != kCmdSuccess)
      return Result;
  }
  return kCmdSuccess;
}

CommandResult Invocation::Execute(StringRef Cmd, llvm::raw_ostream* OS) const {
  const Invocation I = { Cmd, Interp, OS ? *OS : Out, CmdHandler, Val, "" };
  return CmdHandler.Execute(I);
}

}
}
