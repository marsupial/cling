//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -I %S -Xclang -verify | FileCheck %s
// Test doXrunBash

extern "C" int printf(const char*,...);

const char* named = "NAMED";

.X BashStyle.h  4 named "named space" 5 "name \"spa\"ce literal" 6  "another vers" 12
// CHECK: 4|NAMED|named space|5|name "spa"ce literal|6|another vers|12
// CHECK: (int) 10

.X BashStyle.h (8, "lit", named, 6, "more \"\'\t  spaces", 12, "test", 2)
// CHECK: 8|lit|NAMED|6|more "'	  spaces|12|test|2
// CHECK: (int) 10

.x CallMain.h named "arg1" 45 unnamed 50
// CHECK: main[0]: 'CallMain.h'
// CHECK: main[1]: 'NAMED'
// CHECK: main[2]: 'arg1'
// CHECK: main[3]: '45'
// CHECK: main[4]: 'unnamed'
// CHECK: main[5]: '50'
// CHECK: (int) 0

//expected-no-diagnostics
.q
