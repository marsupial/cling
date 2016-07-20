//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -x objective-c++ -Xclang -verify 2>&1 | FileCheck %s
// Test testGNUObjC

extern "C" id class_createInstance(Class class_, unsigned extraBytes);

@interface TestInst
  + (id) alloc;
  + (int) test;
  - (int) testMeth : (id) me;
@end
@implementation TestInst
  + (id) alloc {
    return (id) class_createInstance(self, 0);
  }
  + (int) test {
    return 506;
  }
  - (int) testMeth: (id) me {
    return self == me;
  }
@end
// expected-warning {{class 'TestInst' defined without specifying a base class}}
// expected-note {{add a super class to fix this problem}}

[TestInst test]
// CHECK: (int) 506

TestInst* t = [TestInst alloc];
t
// CHECK: (TestInst *) 0x{{.*}}

[t testMeth: t]
// CHECK: (int) 1

.q
