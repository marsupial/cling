//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// When run in batch mode, failure to load should stop execution
// RUN: %cling %T %s -Xclang -verify 2>&1 | FileCheck %s
//XFAIL: *

#pragma cling load("DoesNotExistPleaseRecover") // expected-error@1{{'DoesNotExistPleaseRecover' file not found}}

extern "C" int printf(const char*,...);

void loadERR() {
  printf("RECOVERED\n");
  // CHECK-NOT: RECOVERED
}
