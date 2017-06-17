//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -Xclang -verify 2>&1 | FileCheck %s

// Test to check the importation of classes/structures that have been declared
// in a parent, but first instantiated in a child.

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Value.h"
#include <stdio.h>

const int Argc = 2;
const char* const Args[] = {"cling", "-D__STRICT_ANSI__", "-Xclang", "-verify"};

struct InParent {};
struct InParent2 {};
template <class T> class TInParent {};
template <class T> T FuncIt(T Val) { return Val; }


// This declaration causes failure in Child.echo("InParent2 P2");
InParent2 IP;
{
  cling::Interpreter Child(*gCling, Argc, Args);

  Child.echo("InParent2 P2");
  // CHECK: (InParent2 &) @0x{{.*}}
}

cling::Value Vals[4];

{
  cling::Interpreter Child(*gCling, Argc, Args);
  Child.echo("InParent P");
  // CHECK: (InParent &) @0x{{.*}}

  Child.echo("TInParent<int> T");
  // CHECK-NEXT: (TInParent<int> &)  @0x{{.*}}

#if BROKEN
  Child.echo("FuncIt<int>(3)");
  // BROKEN-CHECK-NEXT: (int) 3

  Child.evaluate("FuncIt(\"TEST\")", Vals[0]);
  Vals[0].dump();
  // BROKEN-CHECK-NEXT: (const char *) "TEST"
#endif
}

{
  cling::Interpreter Child(*gCling, Argc, Args);

  Child.evaluate("InParent P", Vals[0]);
  Vals[0].dump();
  // CHECK: (InParent) @0x{{.*}}

  Child.evaluate("new InParent", Vals[1]);
  Vals[1].dump();
  // CHECK-NEXT: (InParent *) 0x{{.*}}
}

{
  cling::Interpreter Child(*gCling, Argc+2, Args);

  Child.echo("IP");
  // CHECK: (InParent2 &) @0x{{.*}}

  Child.evaluate("IP", Vals[2]);
  printf("(bool) %s\n", Vals[2].getPtr() == &IP ? "true" : "false");
  // CHECK-NEXT: (bool) true

  Child.echo("struct T {} t");
  // CHECK-NEXT: (struct T &) @0x{{.*}}
  Child.echo("t // expected-error {{use of undeclared identifier 't'}}");
  // CHECK-NOT: (struct T &) @0x{{.*}}

#if BROKEN
  struct OverLoad {};
  //Child.declare("namespace cling { std::string printValue(const OverLoad*) { return \"OOO\"; } }");
  Child.echo("OverLoad O");
#endif

  Child.evaluate("struct B {} b", Vals[3]);
  Vals[3].dump();
  // CHECK-NEXT: (struct B) @0x{{.*}}
}

// SIGSEGV
//Vals[3]

// expected-no-diagnostics

.q
