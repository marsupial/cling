//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -I%S -Xclang -verify 2>&1 | FileCheck %s

#pragma cling load("P0.h", "P1.h","P2.h")

ValueA
// CHECK: (const char *) "ValueA"

ValueB
// CHECK: (const char *) "ValueB"

ValueC
// CHECK: (const char *) "ValueC"

.undo
.undo
.undo
.undo

#pragma cling load "P0.h" "P1.h" "P2.h"
ValueA
// CHECK: (const char *) "ValueA"

ValueB
// CHECK: (const char *) "ValueB"

ValueC
// CHECK: (const char *) "ValueC"

.undo
.undo
.undo
.undo

.stats undo

#pragma cling(load, "P0.h", "P1.h", "P2.h")
ValueA
// CHECK: (const char *) "ValueA"

ValueB
// CHECK: (const char *) "ValueB"

ValueC
// CHECK: (const char *) "ValueC"


#pragma cling load "P0.h P1.h P2.h"
ValueD
// CHECK: (const char *) "ValueD"

// expected-no-diagnostics
.q
