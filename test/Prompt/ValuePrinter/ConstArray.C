//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// Test imposibility of local array type
// RUN: cat %s | %cling -Xclang -verify 2>&1 | FileCheck %s

extern "C" int printf(const char*, ...);

struct TEST { TEST() { printf("TEST\n"); } ~TEST() { printf("test\n"); } };
typedef TEST Test4[4]; 

TEST[2] // expected-error {{expected unqualified-id}}

Test4() // expected-error {{array types cannot be value-initialized}}

static TEST[3] returner() { return; } // expected-error {{brackets are not allowed here; to declare an array, place the brackets after the name}} // expected-error {{function definition is not allowed here}}

static Test4 returner() { return Test4; } // expected-error {{function cannot return array type 'Test4' (aka 'TEST [4]')}} // expected-error {{unexpected type name 'Test4': expected expression}}

TEST t[4]
//      CHECK: TEST
// CHECK-NEXT: TEST
// CHECK-NEXT: TEST
// CHECK-NEXT: TEST
// CHECK-NEXT: (TEST [4]) { @0x{{[0-9a-f]+}}, @0x{{[0-9a-f]+}}, @0x{{[0-9a-f]+}}, @0x{{[0-9a-f]+}} }

int A0[2][3][4]{{{1,2,3,4},{11,12,13,14},{21,22,23,24}},
                {{101,102,103,104},{111,112,113,114},{121,122,123,124}}}
// CHECK-NEXT: (int [2][3][4]) { { { 1, 2, 3, 4 }, { 11, 12, 13, 14 }, { 21, 22, 23, 24 } }, { { 101, 102, 103, 104 }, { 111, 112, 113, 114 }, { 121, 122, 123, 124 } } }

// destruction of TEST t[4]
// CHECK-NEXT: test
// CHECK-NEXT: test
// CHECK-NEXT: test
// CHECK-NEXT: test

.q
