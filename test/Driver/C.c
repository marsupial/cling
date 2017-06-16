//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -x c -Xclang -verify 2>&1 | FileCheck %s
// RUN: cat %s | %cling -x c -fsyntax-only -Xclang -verify 2>&1

// Validate cling C mode.

#include <strings.h>
#include <stdio.h>
#include <stdint.h>
#include <wchar.h>

printf("CHECK 123 %p\n", gCling); // CHECK: CHECK 123

"1234567"
// CHECK-NEXT: (char *) "1234567"

1
// CHECK-NEXT: (int) 1

2
// CHECK-NEXT: (int) 2

3
// CHECK-NEXT: (int) 3

45.678f
// CHECK-NEXT: (float) 45.67800f

910.111213
// CHECK-NEXT: (double) 910.111213

typedef unsigned long long ULL;
ULL funcIt() {
    return 65261;
}

funcIt()
// CHECK-NEXT: (unsigned long long) 65261

void FNoArgs() {
}
FNoArgs
//      CHECK: (void (*)()) Function @0x{{.*}}
// CHECK-NEXT:   at {{.*}}
// CHECK-NEXT: void FNoArgs() {
// CHECK-NEXT: }

void FWithArgs(int A, const char* B) {
}
FWithArgs
//      CHECK: (void (*)(int, const char *)) Function @0x{{.*}}
// CHECK-NEXT:   at {{.*}}
// CHECK-NEXT: void FWithArgs(int A, const char* B) {
// CHECK-NEXT: }


typedef struct Test {
} Test;

Test structTest() {
  Test t;
  return t;
}

structTest()
// CHECK: (Test) 0x{{.*}}

Test T0 = {}
// CHECK-NEXT: (Test &) 0x{{.*}}
T0
// CHECK-NEXT: (Test &) 0x{{.*}}
&T0
// CHECK-NEXT: (Test *) 0x{{.*}}

typedef struct TestCustom {
    int A;
    const char* Name;
    struct TestCustom* BackPtr;
} TestCustom;

typedef struct TestACII {
} TestACII;

typedef struct TestU16 {
} TestU16;

typedef struct TestU32 {
} TestU32;

typedef struct TestWChar {
} TestWChar;

typedef struct TestNULL {
} TestNULL;

typedef struct TestEmpty {
} TestEmpty;


typedef struct TestSizeArg0 {} TestSizeArg0;
typedef struct TestSizeArg1 {} TestSizeArg1;

const char* clingPrint_TestCustom(const TestCustom* TC) {
  printf("{%d, %s, %p:%p}\n", TC->A, TC->Name, TC->BackPtr, TC);
  return strdup("<-TestCustom->");
}

const char* clingPrint_TestACII(const struct TestACII* Ptr, size_t Sz, char* Buf) {
  snprintf(Buf, Sz, "TestACII - %p %lu %p", Ptr, Sz, Buf);
  return Buf;
}

uint16_t* clingPrint_TestU16(const struct TestU16* Ptr, size_t* Sz, uint16_t* Buf) {
  for (unsigned I = 0; I < 12; ++I)
    Buf[I] = 'a' + I;
  *Sz = 6;
  return Buf;
}

uint32_t* clingPrint_TestU32(const TestU32* Ptr, size_t* Sz, uint32_t* Buf) {
  for (unsigned I = 0; I < 12; ++I)
    Buf[I] = 'A' + I;
  *Sz = 3;
  return Buf;
}

wchar_t* clingPrint_TestWChar(const TestWChar* Ptr, size_t* Sz, int* Buf) {
  for (unsigned I = 0; I < 12; ++I)
    Buf[I] = '0' + I;
  *Sz = 5;
  return Buf;
}

uint16_t* clingPrint_TestSizeArg0(const TestSizeArg0* Ptr, size_t Sz, uint16_t* Buf) {
  return NULL;
}

uint32_t* clingPrint_TestSizeArg1(const TestSizeArg1* Ptr) {
  return NULL;
}


const char* clingPrint_TestNULL(const TestNULL* Ptr, size_t* Sz, char* Buf) {
  return NULL;
}

const char* clingPrint_TestEmpty() {
  return strdup("** TestEmpty **");
}

TestCustom Tc = { 10, "First" };
Tc.BackPtr = &Tc;
Tc
// CHECK-NEXT: {10, First, 0x{{.*}}:0x{{.*}}}
// CHECK-NEXT: (TestCustom &) <-TestCustom->


TestACII Ta = {}
// CHECK-NEXT: (TestACII &) TestACII - 0x{{.*}} {{[0-9]+}} 0x{{.*}}

TestU16 Tu16 = {};
Tu16
// CHECK-NEXT: (TestU16 &) u"abcdef"

TestU32 Tu32 = {}
// CHECK-NEXT: (TestU32 &) U"ABC"

TestWChar TWC = {}  ; // FIXME: wchar not matching builtin...
// CHECK-NOT: (TestWChar &) L"01234"

TestNULL TN = {};
TN
// CHECK-NEXT: (TestNULL &) nullptr


TestSizeArg0 TSa0 = {}
// CHECK-NEXT: (TestSizeArg0 &) <unicode string>

TestSizeArg1 TSa1 = {}
// CHECK-NEXT: (TestSizeArg1 &) <unicode string>

TestEmpty TE = {}
// CHECK-NEXT: ** TestEmpty **

typedef struct TestExternal {
} TestExternal;
extern const char* clingPrint_TestExternal();

TestExternal Tx = {}
// CHECK-NEXT: IncrementalExecutor::executeFunction: symbol 'clingPrint_TestExternal' unresolved while linking [cling interface function]!
// CHECK-NEXT: (TestExternal &) 0x{{.*}}

// expected-no-diagnostics
.q
