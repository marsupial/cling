//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: %cling %s -Xclang -verify 2>&1 | FileCheck %s
// Test testGNUObjC

extern "C" id class_createInstance(Class class_, unsigned extraBytes);
extern "C" int printf(const char*, ...);
extern "C" void* object_setInstanceVariable(id object, const char *name, void *newValue);

@interface TestInst {
  id isa; // Because we have no root class, we must define this implicit member
  int m_Value;
}
  + (id) alloc;
  + (int) test;
  - (int) testMeth : (id) me;
@end
@implementation TestInst
  + (id) alloc {
    return (id) class_createInstance(self, sizeof(int));
  }
  + (int) test {
    return 506;
  }
  - (id) init: (int) value {
    m_Value = value;
    return self;
  }
  - (int) testMeth: (id) me {
    return self == me;
  }
  - (int) value {
    return m_Value;
  }
@end
// expected-warning@16 {{class 'TestInst' defined without specifying a base class}}
// expected-note@16 {{add a super class to fix this problem}}

extern "C" void Invoke() {
  
  printf("static: %d\n", [TestInst test]);
  // CHECK: static: 506

  TestInst* t = [[TestInst alloc] init: 768];
  printf("instance: %p\n", t);
  // CHECK: instance: 0x{{.*}}

  printf("same: %d\n", [t testMeth: t]);
  // CHECK: same: 1

  printf("value: %d\n", [t value]);
  // CHECK: value: 768
}
