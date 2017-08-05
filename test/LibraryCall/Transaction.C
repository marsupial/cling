//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: clang -shared -fno-rtti -I%S -DCLING_EXPORT=%dllexport -DCLING_LIBTEST %S/Transaction.cxx -o%T/libTransactionA%shlibext
// RUN: clang -shared -fno-rtti -I%S -DCLING_EXPORT=%dllexport -DCLING_SUBCLASS %S/Transaction.cxx -L%T -lTransactionA -o%T/libTransactionB%shlibext
// RUN: cat %s | %cling -I%S -L%T -Xclang -verify 2>&1 | FileCheck %s


.L libTransactionA
#include "Transaction.h"
.L libTransactionB
.U libTransactionA

//      CHECK: BaseClass::construct
// CHECK-NEXT: SubClass::construct
// CHECK-NEXT: SubClass::destruct
// CHECK-NEXT: BaseClass::destruct

#include "Transaction.h"
SubClass SC;
.U libTransactionB

// CHECK: You are probably missing the definition of SubClass::SubClass()
// CHECK: Maybe you need to load the corresponding shared library?
// CHECK: You are probably missing the definition of BaseClass::~BaseClass()
// CHECK: Maybe you need to load the corresponding shared library?

// expected-no-diagnostics
