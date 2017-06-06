//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %built_cling 2>&1 | FileCheck %s

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Value.h"
#include "llvm/Support/raw_ostream.h"
#include <cmath>

cling::Value V;
int cling_Test_A0[2]{ 0, 1 };

gCling->evaluate("cling_Test_A0", V);
// clang::QualType QT = V.getType();
// QT.getTypePtr()->dump();
.trace ast cling_Test_A0
// CHECK: VarDecl {{0x[0-9a-f]+}} <input_line_17:2:2

gCling->evaluate("int cling_Test_A1[2]{0, 1};", V);
// QT = V.getType();
// QT.getTypePtr()->dump();
.trace ast cling_Test_A1
// CHECK: VarDecl {{0x[0-9a-f]+}} <cling::Interpreter::evaluate_25:2:2

gCling->evaluate("int cling_Test_A2[2]{0, 1};\nint cling_Test_A3[2]{0, 1}\n;", V);
.trace ast cling_Test_A2
// CHECK: VarDecl {{0x[0-9a-f]+}} <cling::Interpreter::evaluate_31:2:2

.trace ast cling_Test_A3
// CHECK: VarDecl {{0x[0-9a-f]+}} <cling::Interpreter::evaluate_31:3:1



int cling_Test_A4[2]{ 0, 1 };
gCling->evaluate("cling_Test_A4", V);
.trace ast cling_Test_A4
// CHECK: VarDecl {{0x[0-9a-f]+}} <input_line_40:2:2





llvm::outs().flush(); // FIXME: Shouldn't be necessary

DERFS
// CHECK: input_line_51:2:2: error: use of undeclared identifier 'DERFS'
