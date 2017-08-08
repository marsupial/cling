//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: clang -shared %fPIC %RTTI -I%S -DCLING_EXPORT=%dllexport -DCLING_LIBTEST %S/Transaction.cxx -o%T/libTransactionA%shlibext
// RUN: clang -shared %fPIC %RTTI -I%S -DCLING_EXPORT=%dllexport -DCLING_REGB %S/Transaction.cxx -o%T/libTransactionB%shlibext
// RUN: cat %s | %cling -I%S -L%T -Xclang -verify 2>&1 | FileCheck %s


.L libTransactionA
#include "Transaction.h"
RegisterPlugin("A");

.L libTransactionB
RegisterPluginB(RegisterPlugin);

.U libTransactionA

//      CHECK: Reg.1: A
// CHECK-NEXT: Reg.2: B
// CHECK-NEXT: Unreg.0: A
// CHECK-NEXT: Unreg.1: B

#include "Transaction.h"
RegisterPluginB(RegisterPlugin);
.U libTransactionB

// CHECK: IncrementalExecutor::executeFunction: symbol 'RegisterPluginB' unresolved while linking [cling interface function]!
// CHECK: IncrementalExecutor::executeFunction: symbol 'RegisterPlugin' unresolved while linking [cling interface function]!

// expected-no-diagnostics
