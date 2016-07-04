//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -I%S -include-pch DontExist.pch -Xclang -verify 2>&1 | FileCheck %s
// testMissingPCH

// CHECK: error: no such file or directory: 'DontExist.pch'

9
// CHECK: (int) 9
// expected-no-diagnostics
.q
