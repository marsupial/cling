//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling 2>&1 | FileCheck -allow-empty %s


namespace A { void foo(); }
namespace B { using A::foo; }
namespace B { using A::foo; }
.storeState "AB2"

namespace B { using A::foo; }
namespace B { using A::foo; }
.undo
.undo

.compareState "AB2"
//CHECK-NOT: Differences

.q
