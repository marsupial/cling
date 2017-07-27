//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: clang -shared %S/Unload.cxx -DCLING_UNLOAD=A %fPIC -DCLING_EXPORT=%dllexport -o %T/ATest%shlibext
// RUN: clang -shared %S/Unload.cxx -DCLING_UNLOAD=B %fPIC -DCLING_EXPORT=%dllexport -o %T/BTest%shlibext
// RUN: clang -shared %S/Unload.cxx -DCLING_UNLOAD=D %fPIC -DCLING_EXPORT=%dllexport -o %T/DTest%shlibext
// RUN: clang -shared %S/Unload.cxx -DCLING_UNLOAD=E %fPIC -DCLING_EXPORT=%dllexport -o %T/ETest%shlibext
// RUN: clang -shared %S/Unload.cxx -DCLING_UNLOAD=0 %fPIC -DCLING_EXPORT=%dllexport -o %T/0Test%shlibext
// RUN: clang -shared %S/Unload.cxx -DCLING_UNLOAD=1 %fPIC -DCLING_EXPORT=%dllexport -o %T/1Test%shlibext
// RUN: clang -shared %S/Unload.cxx -DCLING_UNLOAD=2 %fPIC -DCLING_EXPORT=%dllexport -o %T/2Test%shlibext
// RUN: clang -shared %S/Unload.cxx -DCLING_UNLOAD=3 %fPIC -DCLING_EXPORT=%dllexport -o %T/3Test%shlibext

// RUN: cat %s | %cling -L%/T -DCLTEST_LIBS="\"%/T\"" -Dshlibext="\"%shlibext\"" -Xclang -verify 2>&1 | FileCheck %s


#include "cling/Interpreter/Interpreter.h"
extern "C" int printf(const char*,...);

.L ETest
.L DTest
.L ATest
.L BTest

// FIXME: std::cout.flush() below should not be necessary, but is for Windows.
#include <iostream>

static void calledFromStaticDtor(void* Interp) {
  ((cling::Interpreter*)(Interp))->echo("printf(\"calledFromStaticDtor\\n\");");
  std::cout.flush();
}

// FIXME This isn't working with lit only
// #define CLING_UNLOAD A
// #include "Unload.cxx"
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
}

//      CHECK: Unloaded::~Unloaded 1
// CHECK-NEXT: Unloaded::~Unloaded 2
// CHECK-NEXT: Unloaded::~Unloaded 0
// CHECK-NEXT: Unloaded::~Unloaded 3

printf("Barrier\n");
// CHECK-NEXT: Barrier

// expected-no-diagnostics
.q
// CHECK-NEXT: Unloaded::~Unloaded B
// CHECK-NEXT: Unloaded::~Unloaded A
// CHECK-NEXT: calledFromStaticDtor
// CHECK-NEXT: (int) 21
// CHECK-NEXT: Unloaded::~Unloaded D
// CHECK-NEXT: Unloaded::~Unloaded E
