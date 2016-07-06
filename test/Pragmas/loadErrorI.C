//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// If in interactive mode with -Werror, failure to load is reported and
// handled by clang's diagnostic machinery, so exit code is 1
// RUN: cat %s | %cling %s -Werror -Xclang -verify 2>&1 | FileCheck %s
//XFAIL: *

#pragma cling load("DoesNotExistPleaseRecover") // expected-error@1{{'DoesNotExistPleaseRecover' file not found}}

extern "C" int printf(const char*,...);

printf("RECOVERED\n");
// CHECK: RECOVERED
