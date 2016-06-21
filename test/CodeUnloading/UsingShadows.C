//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -I %S 2>&1 | FileCheck %s


namespace A { void foo(); }
namespace B { using A::foo; }
namespace B { using A::foo; }
.storeState "TEST1"

namespace B { using A::foo; }
namespace B { using A::foo; }
.undo
.undo

.compareState "TEST1"
//CHECK-NOT: Differences

typedef long long_t;

.storeState "TEST2"

#include "UsingShadows.h"
#include "UsingShadows.h"
#include "UsingShadows.h"
.undo
.undo
.undo

//Make sure long_t is still valid
.compareState "TEST2"
//CHECK-NOT: Differences

long_t val = 9;
val
//CHECK: 9

// Unloading <string> used to fail as well (which as annoying).
#include <string>
.undo

// expected-no-diagnostics
.q
