//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -Xclang -verify 2>&1 | FileCheck %s

#include <string>

class Thrower {
  int m_Private = 0;
};

namespace cling {
  std::string printValue(const Thrower* T) {
    throw std::bad_alloc();
    return "";
  }
}
Thrower Ts;
Ts
// CHECK: >>> Caught a std::exception: 'std::bad_alloc'.

ERR // expected-error {{use of undeclared identifier 'ERR'}}

Ts.m_Private = 5 // expected-error {{'m_Private' is a private member of 'Thrower'}}
// expected-note@input_line_13:2 {{implicitly declared private here}}

// CHECK-NOT: (int) 5

.q