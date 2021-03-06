//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -I%S -Xclang -verify 2>&1 | FileCheck %S/Shebang.C

#define CLING_WRAP_FUNC_ static void WrappedFunc() {
#define _CLING_WRAP_FUNC }

#include "Shebang.C"

shebang
WrappedFunc()

// expected-no-diagnostics
