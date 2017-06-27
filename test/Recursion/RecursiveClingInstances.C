//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -I%p 2>&1 | FileCheck %s

// Tests the ability of cling to host itself. We can have cling instances in
// cling's runtime. This is important for people, who use cling embedded in
// their frameworks.

#include "cling/Interpreter/Interpreter.h"

thisCling.process("const char * const argV = \"cling\";");
thisCling.process("cling::Interpreter *DefaultInterp;");

thisCling.process("DefaultInterp = new cling::Interpreter(1, &argV);");
thisCling.process("DefaultInterp->process(\"#include \\\"cling/Interpreter/Interpreter.h\\\"\");");
thisCling.process("DefaultInterp->process(\"std::string s; thisCling.createUniqueName(s); s.c_str()\");");
// CHECK: ({{[^)]+}}) "__cling_Un1Qu31"
.q
