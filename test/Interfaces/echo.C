//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling 2>&1 | FileCheck %s

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Value.h"

thisCling.echo("1;");
// CHECK: (int) 1
cling::Value V;
thisCling.echo("2;", &V);
V
// CHECK-NEXT: (int) 2
// CHECK-NEXT: (cling::Value &) boxes [(int) 2]
