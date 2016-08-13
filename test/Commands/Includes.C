//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: export ENV_INCLUDE="ABC:DEF:G"
// RUN: cat %s | %cling -Xclang -verify 2>&1 | FileCheck %s
// Test testICommandExpansion

.I $ENV_INCLUDE :
.I $ENV_INCLUDE
.I "HAT;SHOE;LACE" ;
.I "ABS/PATH/ROOT" /
.I "SEP/PATH/LIT" '/'
.I

// CHECK: ABC
// CHECK: DEF
// CHECK: G
// CHECK: ABC:DEF:G

// CHECK: HAT
// CHECK: SHOE
// CHECK: LACE

// CHECK: ABS/PATH/ROOT
// CHECK: /

// CHECK: SEP
// CHECK: PATH
// CHECK: LIT

//expected-no-diagnostics
.q
