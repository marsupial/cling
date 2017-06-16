//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling 2>&1 -Xclang -verify | FileCheck %s

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Value.h"

gCling->echo("1;");
// CHECK: 1
cling::Value V;
gCling->echo("2;", &V);
V

// CHECK-NEXT: 2
// CHECK-NEXT: (cling::Value &) boxes [(int) 2]

struct T {
  unsigned Val;
  void print(const char* N) const { printf("T::%s[%u] = %p\n", N, Val, this); }
  T(unsigned V) : Val(V) { print("T"); }
  ~T() { print("~T"); }
};

gCling->echo("T(0)");
printf("---- barrier ----\n");

// Above should have destroy the object immediately after print
// (no way to reach it otherwise)
// CHECK-NEXT: T::T[0] = 0x{{.*}}
// CHECK-NEXT: (T) @0x{{.*}}
// CHECK-NEXT: T::~T[0] = 0x{{.*}}
// CHECK-NEXT: ---- barrier ----

gCling->echo("T(1);", &V);
printf("---- barrier ----\n");

// This variant should have printed and stored the object into V.
// CHECK-NEXT: T::T[1] = 0x{{.*}}
// CHECK-NEXT: (T) @0x{{.*}}
// CHECK-NEXT: ---- barrier ----
// CHECK-NEXT: T::~T[1] = 0x{{.*}}

// expected-no-diagnostics
