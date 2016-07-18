//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -Xclang -verify 2>&1 | FileCheck %s
// Test undoPrinter

.stats undo
// CHECK: <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>
// CHECK: `   <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>

struct Test {};

Test message
// CHECK: (Test &) @0x{{[0-9a-f]+}}

.undo // undo declaration & print
.stats undo
// CHECK: <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>
// CHECK: `   <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>
// CHECK: <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>

Test message;

// Make sure we can still print
message
// CHECK: (Test &) @0x{{[0-9a-f]+}}

.undo // undo print
.stats undo
// CHECK: <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>
// CHECK: `   <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>
// CHECK: <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>
// CHECK: <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>

message
// CHECK: (Test &) @0x{{[0-9a-f]+}}

.undo // print message
.undo // declare message
.undo // declare Test
.stats undo
// CHECK: <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>
// CHECK: `   <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>

#include "cling/Interpreter/Interpreter.h"

gCling->echo("std::string(\"TEST\")");
// CHECK: (std::string) "TEST"

.stats undo
// CHECK: <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>
// CHECK: `   <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>
// CHECK: <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>
// CHECK: <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>
// CHECK: `   <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>
// CHECK: `      <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>
// CHECK: `      <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>
// CHECK: `      <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>

.undo
.stats undo
// CHECK: <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>
// CHECK: `   <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>
// CHECK: <cling::Transaction* 0x{{[0-9a-f]+}} isEmpty=0 isCommitted=1>

gCling->echo("std::string(\"TEST2\")");
// CHECK: (std::string) "TEST2"

// expected-no-diagnostics
.q
