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
                      llvm::StringRef Separators) {
  size_t Start = 0;
  const size_t Len = Str.size();
  llvm::StringRef CmdName;

  for (size_t Idx = 0; Idx < Len; ++Idx) {
    const char C = Str[Idx];
    for (const char S : Separators) {
      if (C != S)
        continue;

      // Make sure this match shouldn't be considered part of the last one.
      if (Idx > Start) {
        if (!CmdName.empty())
          Out.push_back(Str.slice(Start, Idx));
        else
          CmdName = Str.slice(Start, Idx);
      }
      Start = Idx + 1;
      break;
    }
  }

  // Grab whatever remains
  if (CmdName.empty())
    CmdName = Str.substr(Start);
  else if (Start != Len)
    Out.push_back(Str.substr(Start));

  return CmdName;
}

}
}
