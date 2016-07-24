//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Jermaine Dupris <support@greyangle.com>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_UTILS_UTILS_H
#define CLING_UTILS_UTILS_H

#include "llvm/ADT/SmallVector.h"
#include <string>

namespace llvm {
  class raw_ostream;
}

namespace clang {
  class HeaderSearchOptions;
  class CompilerInstance;
}

namespace cling {
  namespace utils {

  void CopyIncludePaths(const clang::HeaderSearchOptions& Opts,
                        llvm::SmallVectorImpl<std::string>& Paths,
                        bool WithSystem, bool WithFlags);

  void DumpIncludePaths(const clang::HeaderSearchOptions& Opts,
                        llvm::raw_ostream& Out,
                        bool WithSystem, bool WithFlags);
  }
}

#endif // CLING_UTILS_UTILS_H
