//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: clang -shared %S/Unload.C -DCLING_UNLOAD=A -fPIC -o %T/ATest%shlibext
// RUN: clang -shared %S/Unload.C -DCLING_UNLOAD=B -fPIC -o %T/BTest%shlibext
// RUN: clang -shared %S/Unload.C -DCLING_UNLOAD=D -fPIC -o %T/DTest%shlibext
// RUN: clang -shared %S/Unload.C -DCLING_UNLOAD=E -fPIC -o %T/ETest%shlibext
// RUN: clang -shared %S/Unload.C -DCLING_UNLOAD=0 -fPIC -o %T/0Test%shlibext
// RUN: clang -shared %S/Unload.C -DCLING_UNLOAD=1 -fPIC -o %T/1Test%shlibext
// RUN: clang -shared %S/Unload.C -DCLING_UNLOAD=2 -fPIC -o %T/2Test%shlibext
// RUN: clang -shared %S/Unload.C -DCLING_UNLOAD=3 -fPIC -o %T/3Test%shlibext

// RUN: cat %s | %cling -L%T -DCLTEST_LIBS="\"%T\"" -Dshlibext="\"%shlibext\"" -Xclang -verify 2>&1 | FileCheck %s


#include "cling/Interpreter/Interpreter.h"
extern "C" int printf(const char*,...);

.L ETest
.L DTest
.L ATest
.L BTest

static void calledFromStaticDtor(void* Interp) {
  ((cling::Interpreter*)(Interp))->echo("printf(\"calledFromStaticDtor\\n\");");
}

// FIXME This isn't working with lit only
// #define CLING_UNLOAD A
// #include "Unload.C"
// CLING_JOIN(setInterpreter, CLING_UNLOAD)(calledFromStaticDtor, gCling);
//
extern "C" void setInterpreterA(void*, void*);
setInterpreterA((void*)&calledFromStaticDtor, gCling);

{
  const char* argV[2] = { "cling", "-L" CLTEST_LIBS };
  cling::Interpreter ChildInterp(*gCling, 2, argV);
  ChildInterp.loadFile(CLTEST_LIBS "/3Test" shlibext, true);
  ChildInterp.loadFile(CLTEST_LIBS "/0Test" shlibext, true);
  ChildInterp.loadFile(CLTEST_LIBS "/2Test" shlibext, true);
  ChildInterp.loadFile(CLTEST_LIBS "/1Test" shlibext, true);
  // expected-no-diagnostics
}

// CHECK: Unloaded::~Unloaded 1
// CHECK: Unloaded::~Unloaded 2
// CHECK: Unloaded::~Unloaded 0
// CHECK: Unloaded::~Unloaded 3

printf("Barrier\n");
// CHECK: Barrier

.q
// CHECK: Unloaded::~Unloaded B
// CHECK: Unloaded::~Unloaded A
// CHECK: calledFromStaticDtor
// CHECK: (int) 21
// CHECK: Unloaded::~Unloaded D
// CHECK: Unloaded::~Unloaded E
