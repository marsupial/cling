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

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <vector>

namespace llvm {
raw_ostream& operator<<(raw_ostream& OS,
                        cling::meta::CommandHandler::Argument Arg) {
  OS << "{\"" << Arg.Str << "\"";
  if (Arg.Escaped) OS << ", Escaped";
  if (Arg.Group) OS << ", Group: '" << Arg.Group << '\'';
  OS << "}";
  return OS;
}
}
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
  return ArgToOptional<unsigned>(Str, WasBool);
}

template <> llvm::Optional<int>
CommandHandler::SplitArgument::Optional<int>(bool* WasBool) const {
  return ArgToOptional<int>(Str, WasBool);
}

template <> llvm::Optional<bool>
CommandHandler::SplitArgument::Optional<bool>(bool* WasBool) const {
  return ArgToOptional<bool>(Str, WasBool);
}

llvm::StringRef
CommandHandler::Split(llvm::StringRef Str, SplitArgumentsList& Out,
                      unsigned Flags, llvm::StringRef Separators) {
  size_t Start = 0;
  const size_t Len = Str.size(), LenMinOne = Len -1;
  llvm::StringRef CmdName;

  bool HadEscape = false;
  size_t InGroup = llvm::StringRef::npos;
  const llvm::StringRef GrpBegin("\"'{[<("), GrpEnd("\"'}]>)");

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
        if (InGroup != llvm::StringRef::npos)
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
      if (InGroup != llvm::StringRef::npos) {
        // See if the character is the end of the current group type.
        if (GrpEnd.find(C) == InGroup) {
          Out.emplace_back(Str.slice(Start, Idx), HadEscape, GrpBegin[InGroup]);
          InGroup = llvm::StringRef::npos;
          HadEscape = false;
          Start = Idx + 1;
        }
        // Already in a group, eat all until the end of it.
        continue;
      }
      // Maybe a group beginning
      InGroup = GrpBegin.find(C);
      if (InGroup != llvm::StringRef::npos) {
        Start = Idx + 1;
        HadEscape = false;
        continue;
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
  if (InGroup != llvm::StringRef::npos)
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

typedef CallbackTypes<CommandHandler::SplitArgumentsList, CommandHandler::Argument> ObjFunction;
typedef CallbackTypes<CommandHandler::SplitArgumentsList, CommandHandler::Argument, 1> FreeFunction;
typedef CallbackTypes<CommandHandler::EscapedArgumentsList, CommandHandler::EscArgument> ObjEscapedFunction;
typedef CallbackTypes<CommandHandler::EscapedArgumentsList, CommandHandler::EscArgument, 1> FreeEscapedFunction;

template <typename T, bool C = 0> struct CallbackData {
  union {
    typename T::Base *B;
    typename T::ArgList *A;
    typename T::Single *S;
    typename T::Dual *D;
  };
};

template <typename T> struct CallbackData<T, true> {
  union {
    typename T::Base B;
    typename T::ArgList A;
    typename T::Single S;
    typename T::Dual D;
  };
};

namespace {
class Callback {
  // Data members
  union {
    CallbackData<FreeFunction, true> Free;
    CallbackData<FreeEscapedFunction, true> FreeEscaped;
    CallbackData<ObjFunction> Obj;
    CallbackData<ObjEscapedFunction> ObjEscaped;
    void* FuncPtr;
  };
  unsigned Flags : 5;

  enum {
    kObjFunction = 1,
    kArgList = 2,
    kSingleStr = 4,
    kDualStr = 8,
    kEscapedStr = 16,
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
  static constexpr unsigned GetArgFlags(const FreeEscapedFunction::ArgList&) { return kEscapedStr | kArgList; }
  static constexpr unsigned GetArgFlags(const FreeEscapedFunction::Single&) { return kEscapedStr | kSingleStr; }
  static constexpr unsigned GetArgFlags(const FreeEscapedFunction::Dual&) { return kEscapedStr | kDualStr; }

  static constexpr unsigned GetArgFlags(const ObjFunction::ArgList&) { return kArgList; }
  static constexpr unsigned GetArgFlags(const ObjFunction::Single&) { return kSingleStr; }
  static constexpr unsigned GetArgFlags(const ObjFunction::Dual&) { return kDualStr; }
  static constexpr unsigned GetArgFlags(const ObjEscapedFunction::ArgList&) { return kEscapedStr | kArgList; }
  static constexpr unsigned GetArgFlags(const ObjEscapedFunction::Single&) { return kEscapedStr | kSingleStr; }
  static constexpr unsigned GetArgFlags(const ObjEscapedFunction::Dual&) { return kEscapedStr | kDualStr; }


  typedef CommandHandler::SplitArgumentsList::value_type Unescaped;
  typedef CommandHandler::Argument Argument;
  typedef CommandHandler::EscArgument EscArgument;

  static Argument Convert(const Unescaped& A) { return A; }
  static EscArgument Convert(EscArgument A) { return A; }

  template <typename CFunc, typename ObjCall, typename... Ts>
  CommandResult Call(CFunc Func, const ObjCall& Obj, Ts&&... Args) {
    if (Flags & kObjFunction)
      return (*Obj)(std::forward<Ts>(Args)...);
    return Func(std::forward<Ts>(Args)...);
  }

  template <typename CFunc, typename ObjCall, typename List>
  CommandResult CallWithOne(const CFunc Func, const ObjCall& Obj,
                            const Invocation& I, const List& Args) {
    for (const auto& A : Args) {
      const CommandResult Result = Call(Func, Obj, I, Convert(A));
      if (Result != kCmdSuccess)
        return Result;
    }
    return kCmdSuccess;
  }

  template <typename CFunc, typename ObjCall, typename List>
  CommandResult CallWithTwo(const CFunc Func, const ObjCall& Obj,
                            const Invocation& I, const List& Args) {
    typename List::value_type Empty;
    for (auto Itr = Args.begin(), End = Args.end(); Itr < End; ++Itr) {
      const auto& A = *Itr;
      ++Itr;
      const auto& B = Itr < End ? *Itr : Empty;
      const CommandResult Result = Call(Func, Obj, I, Convert(A), Convert(B));
      if (Result != kCmdSuccess)
        return Result;
    }
    return kCmdSuccess;
  }

  template <class T0, class T1> inline void DoDelete(T0* StdStr, T1* StrRef) {
    // Delete the proper pointer type.
    Flags & kEscapedStr ? (delete StdStr) : (delete StrRef);
  }

public:
  template <class FrFunc> Callback(FrFunc F) : Flags(GetArgFlags(F)) {
    // Make sure the data is layed out as required.
    static_assert(sizeof(FuncPtr) == sizeof(F), "Invalid assignment");
    static_assert(sizeof(Free) == sizeof(Obj), "Invalid assignment");
    static_assert(sizeof(Free) == sizeof(FreeEscaped), "Invalid assignment");
    static_assert(sizeof(Obj) == sizeof(ObjEscaped), "Invalid assignment");
    assert(&FuncPtr == static_cast<void*>(&Free.B) && "Bad overlap");
    assert(&FuncPtr == static_cast<void*>(&Obj.S) && "Bad overlap");
    assert(&FuncPtr == static_cast<void*>(&ObjEscaped.D) && "Bad overlap");

    FuncPtr = cling::utils::FunctionToVoidPtr(F);
  }

  template <class StdFunc>
  Callback(std::function<StdFunc> F) : Flags(kObjFunction | GetArgFlags(F)) {
    FuncPtr = reinterpret_cast<void*>(new std::function<StdFunc>(std::move(F)));
  }

  ~Callback() {
    if (Flags & kObjFunction) {
      switch (Flags & ~(kObjFunction|kEscapedStr)) {
        case kNoStrArgs: DoDelete(ObjEscaped.B, Obj.B); break;
        case kArgList:   DoDelete(ObjEscaped.A, Obj.A); break;
        case kSingleStr: DoDelete(ObjEscaped.S, Obj.S); break;
        case kDualStr:   DoDelete(ObjEscaped.D, Obj.D); break;
        default: llvm_unreachable("Unkown function type not deallocated");
      }
    }
  }

  CommandResult Dispatch(const Invocation& I,
                         CommandHandler::SplitArguments& Args,
                         std::vector<std::string>& ArgV) {
    // Simplest callback (Invocation& I);
    if (!Flags || Flags == kObjFunction)
      return Call(Free.B, Obj.B, I);

    // Do escaping now if neccessary.
    if (Flags & kEscapedStr && ArgV.empty()) {
      ArgV.reserve(Args.size());
      for (auto&& A : Args)
        ArgV.emplace_back(std::move(A));
    }

    // callback (Invocation& I, ArgList);
    if (Flags & kArgList) {
      if (Flags & kEscapedStr)
        return Call(FreeEscaped.A, ObjEscaped.A, I, ArgV);
      return Call(Free.A, Obj.A, I, Args);
    }

    // callback (Invocation& I, SingleStr);
    if (Flags & kSingleStr) {
      if (Flags & kEscapedStr)
        return CallWithOne(FreeEscaped.S, ObjEscaped.S, I, ArgV);
      return CallWithOne(Free.S, Obj.S, I, Args);
    }

    // callback (Invocation& I, SingleStr, SingleStr);
    if (Flags & kDualStr) {
      if (Flags & kEscapedStr)
        return CallWithTwo(FreeEscaped.D, ObjEscaped.D, I, ArgV);
      return CallWithTwo(Free.D, Obj.D, I, Args);
    }

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
                                       T Func, std::string Help) {
  Key K(L.emplace(std::move(Name), llvm::make_unique<Callback>(std::move(Func))));
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
      std::string Name, Type F, std::string Help) {                            \
    return AddIt(m_Callbacks, std::move(Name), std::move(F), std::move(Help)); \
  }

CH_ADD_COMMAND_SPEC(FreeFunction::Base)

CH_ADD_COMMAND_SPEC(FreeFunction::ArgList)
CH_ADD_COMMAND_SPEC(FreeFunction::Single)
CH_ADD_COMMAND_SPEC(FreeFunction::Dual)
CH_ADD_COMMAND_SPEC(FreeEscapedFunction::ArgList)
CH_ADD_COMMAND_SPEC(FreeEscapedFunction::Single)
CH_ADD_COMMAND_SPEC(FreeEscapedFunction::Dual)

CH_ADD_COMMAND_SPEC(ObjFunction::Base)

CH_ADD_COMMAND_SPEC(ObjFunction::ArgList)
CH_ADD_COMMAND_SPEC(ObjFunction::Single)
CH_ADD_COMMAND_SPEC(ObjFunction::Dual)
CH_ADD_COMMAND_SPEC(ObjEscapedFunction::ArgList)
CH_ADD_COMMAND_SPEC(ObjEscapedFunction::Single)
CH_ADD_COMMAND_SPEC(ObjEscapedFunction::Dual)

bool CommandHandler::Alias(std::string Name, CommandID ID) {
  return false;
}

CommandResult CommandHandler::Execute(const Invocation& I) {
  SplitArguments Args;
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

  // Hold the escaped arguemnts here, so it only needs to be done once.
  std::vector<std::string> ArgV;

  for (auto Cmd = Cmds.first; Cmd != Cmds.second; ++Cmd) {
    const CommandResult Result = Cmd->second->Dispatch(Adjusted, Args, ArgV);
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
