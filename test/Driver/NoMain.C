//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

//RUN: cat %s | %cling 2>&1 | FileCheck %s

extern "C" int main(int, const char**);

main(0,0);
// CHECK: IncrementalExecutor::executeFunction: symbol 'main' unresolved while linking [cling interface function]!

.q
