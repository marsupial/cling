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

llvm::StringRef
CommandHandler::Split(llvm::StringRef Str, SplitArgumentsImpl& Out,
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
        // The next character is actually part of this one, and cannot start or
        // close a group or argument.
        ++Idx;

        // Special case an escaped character who is also a separator, like a
        // line continuation
        if (Separators.find(E) != llvm::StringRef::npos)
          Start = Idx + 1;
        continue;
      }
    }

    // Groups
    if (Flags & kSplitWithGrouping) {
      if (InGroup != llvm::StringRef::npos) {
        // See if the character is the end of the current group type.
        if (GrpEnd.find(C) == InGroup) {
          Out.push_back(std::make_pair(Str.slice(Start, Idx), HadEscape));
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
    for (const char S : Separators) {
      if (C != S)
        continue;

      // Make sure this match shouldn't be considered part of the last one.
      if (Idx > Start) {
        if (CmdName.empty() && (Flags & kPopFirstArgument))
          CmdName = Str.slice(Start, Idx);
        else
          Out.push_back(std::make_pair(Str.slice(Start, Idx), HadEscape));
        HadEscape = false;
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
    Out.push_back(std::make_pair(Str.substr(Start), HadEscape));

  return CmdName;
}

}
}
