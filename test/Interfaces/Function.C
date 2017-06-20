//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -Xclang -verify | FileCheck %s

#include "cling/Interpreter/Interpreter.h"
#include "cling/Utils/Casting.h"

extern "C" int printf(const char*, ...);

typedef void (*Proc) ();

cling::utils::VoidToFunctionPtr<Proc>(
    gCling->compileFunction("C", "extern \"C\" void C() { printf(\"C\\n\"); }"))();
// CHECK: C

cling::utils::VoidToFunctionPtr<Proc>(
    gCling->compileFunction(
        "A", "namespace A { namespace B { void A() { printf(\"ABA\\n\"); } } }",
    false, true, false))()
// CHECK-NEXT: ABA

// expected-no-diagnostics
