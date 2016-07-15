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

struct Argument {
  const char* value;
  Argument(const char* v) : value(v) {}
};
Argument  A("A1");
Argument* B = new Argument("B");
const char* named = "NAMED";
#define MACROARG "macroarg"

extern "C" const char* lFunc() {
  return "lFunc called";
}

.X BashStyle.h  4 named B->value 5 MACROARG 6  undefined 12
// CHECK: 4|NAMED|B|5|macroarg|6|undefined|12
// CHECK: (int) 10

.X BashStyle.h (8, "lit", named, 6, "more \"\'\t  spaces", 12, "test", 2)
// CHECK: 8|lit|NAMED|6|more "'	  spaces|12|test|2
// CHECK: (int) 10

.x CallMain.h named "arg1" 45 unnamed 50-25 A.value B->value File->test File.h lFunc()
// CHECK: main[0]: 'CallMain.h'
// CHECK: main[1]: 'NAMED'
// CHECK: main[2]: 'arg1'
// CHECK: main[3]: '45'
// CHECK: main[4]: 'unnamed'
// CHECK: main[5]: '50-25'
// CHECK: main[6]: 'A1'
// CHECK: main[7]: 'B'
// CHECK: main[8]: 'File->test'
// CHECK: main[9]: 'File.h'
// CHECK: main[10]: 'lFunc called'
// CHECK: (int) 0

delete B;

//expected-no-diagnostics
.q
