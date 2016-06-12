//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -L %S -I %S -Xclang -verify | FileCheck %s
// Test doXrunParen

extern "C" int printf(const char*,...);

.x XrunParen.h (4) (5)
// CHECK: XrunParen(4)
// CHECK: XrunReturned(5)

.x "XrunParen.h(4)" ("5")
// CHECK: XrunParen(5)

.x "XrunParen.h(4)(1)" ()
// CHECK: XrunParen()

.L ; ;file;

weirdfile
// CHECK: (const char *) ";file;"

semicolon
// CHECK: (const char *) "semicolonoscopy"

//expected-no-diagnostics
.q