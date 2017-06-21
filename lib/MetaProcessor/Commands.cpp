//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author: Roman Zulak
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/MetaProcessor/Commands.h"
#include "llvm/Support/raw_ostream.h"

namespace cling {
namespace meta {

CommandHandler::~CommandHandler() {}

llvm::StringRef
CommandHandler::Split(llvm::StringRef Str, SplitArgumentsImpl& Out,
                      unsigned Flags, llvm::StringRef Separators) {
  size_t Start = 0;
  const size_t Len = Str.size();
  llvm::StringRef CmdName;

  size_t InGroup = llvm::StringRef::npos;
  const llvm::StringRef GrpBegin("\"'{[<("), GrpEnd("\"'}]>)");

  for (size_t Idx = 0; Idx < Len; ++Idx) {
    const char C = Str[Idx];

    // Groups
    if (Flags & kSplitWithGrouping) {
      if (InGroup != llvm::StringRef::npos) {
        // See if the character is the end of the current group type.
        if (GrpEnd.find(C) == InGroup) {
          Out.push_back(Str.slice(Start, Idx));
          InGroup = llvm::StringRef::npos;
          Start = Idx + 1;
        }
        // Already in a group, eat all until the end of it.
        continue;
      }
      // Maybe a group beginning
      InGroup = GrpBegin.find(C);
      if (InGroup != llvm::StringRef::npos) {
        Start = Idx + 1;
        continue;
      }
    }

    // Splitting
    for (const char S : Separators) {
      if (C != S)
        continue;

      // Make sure this match shouldn't be considered part of the last one.
      if (Idx > Start) {
        if (CmdName.empty() && (Flags & kPopFirstArgument))
          CmdName = Str.slice(Start, Idx);
        else
          Out.push_back(Str.slice(Start, Idx));
      }
      Start = Idx + 1;
      break;
    }
  }

  // Unterminated group keep it as it was
  if (InGroup != llvm::StringRef::npos)
    --Start;

  // Grab whatever remains
  if (CmdName.empty() && (Flags & kPopFirstArgument))
    CmdName = Str.substr(Start);
  else if (Start != Len)
    Out.push_back(Str.substr(Start));

  return CmdName;
}

}
}
