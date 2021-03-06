#!/bin/cling
//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: %cling %s -Xclang -verify 2>&1 | FileCheck %s
// RUN: cat %s | %cling -Xclang -verify 2>&1 | FileCheck %s

float shebang = 1.0;
extern "C" int printf(const char* fmt, ...);

#ifndef CLING_WRAP_FUNC_
 #define CLING_WRAP_FUNC_
 #define _CLING_WRAP_FUNC
 shebang
#endif
// CHECK: (float) 1

CLING_WRAP_FUNC_

if(shebang == 1.0) {
  printf("I am executed\n");
  // CHECK-NEXT: I am executed
}

_CLING_WRAP_FUNC

// expected-no-diagnostics
