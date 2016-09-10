//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -I %S -Xclang -verify 2>&1 | FileCheck -allow-empty %s
// Test friendUnload

void bestFriend();

.storeState "NF"
#include "Friend.h"
.undo
.compareState "NF"
//CHECK-NOT: Differences

#include "FriendNested.h"
.undo
.compareState "NF"
//CHECK-NOT: Differences

// TODO Fix why this actually errors over expected error & note testing!
#include "FriendRecursive.h" // expected-error@FriendRecursive.h:6 {{expected success trying to remove CXXRecord 'TestB' from TranslationUnit}}
// expected-note@FriendRecursive.h:6 {{Please run .source and send output to cling-dev@cern.ch}}
.undo
.compareState "NF"
//CHECK-NOT: Differences


// STL has many of these in memory & stdexcept
#include <memory>
.undo
#include <memory>

.q
