//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -Xclang -verify 2>&1 | FileCheck %s
// XFAIL: *

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Value.h"
#include <string>
#include <stdexcept>
#include <stdio.h>

class Thrower {
  int m_Private = 0;
public:
  Thrower(int I = 1) : m_Private(I) {
    if (I) throw std::runtime_error("Thrower");
  }
  ~Thrower() { printf("~Thrower-%d\n", m_Private); }

  void* operator new(size_t Size) {
    throw std::runtime_error("Thrower.new");
    return malloc(Size);
  }
};

void barrier() {
  static int N = 0;
  printf("%d -------------\n", N++);
}

namespace cling {
  std::string printValue(const Thrower* T) {
    throw std::runtime_error("cling::printValue");
    return "";
  }
}

// FIXME: Intercept std::terminate call from this: Thrower Tt(1);

// Check throwing from cling::printValue doesn't crash.
Thrower Ts(0);
barrier();
// CHECK: 0 -------------

Ts
// CHECK-NEXT: >>> Caught a std::exception: 'cling::printValue'.

ERR // expected-error {{use of undeclared identifier 'ERR'}}

barrier();
// CHECK-NEXT: 1 -------------

// Un-named, so it's not a module static which would trigger std::terminate.
Thrower()
// CHECK-NEXT: >>> Caught a std::exception: 'Thrower'.
//  CHECK-NOT: ~Thrower-1


barrier();
// CHECK-NEXT: 2 -------------

Thrower& flocal() {
  Thrower T;
  return T; // expected-warning {{reference to stack memory associated with local variable 'T' returned}}
}
flocal()
// CHECK-NEXT: >>> Caught a std::exception: 'Thrower'.
//  CHECK-NOT: ~Thrower-1


barrier();
// CHECK-NEXT: 3 -------------

// Must be -new-, throwing from a constructor of a static calls std::terminate!
new Thrower
// CHECK-NEXT: >>> Caught a std::exception: 'Thrower.new'.
//  CHECK-NOT: ~Thrower-1


barrier();
// CHECK-NEXT: 4 -------------

// Ts is a valid object and destruction should occur when out of scope.
//  CHECK-NOT: ~Thrower-1
// CHECK-NEXT: ~Thrower-0
//  CHECK-NOT: ~Thrower-1

// expected-no-diagnostics
.q
