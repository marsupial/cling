//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -Xclang -verify -fno-rtti 2>&1 | FileCheck %s

#include "cling/MetaProcessor/Commands.h"
#include "cling/Interpreter/Interpreter.h"
#include "llvm/Support/raw_ostream.h"

using namespace cling;
using namespace cling::meta;

class TestImpl : public CommandHandler {
public:
  TestImpl() {}
  ~TestImpl() {}

  CommandResult Execute(ParmBlock& PB) final {
    PB.Out << PB.CmdStr << "\n";
    return kCmdSuccess;
  }
};
TestImpl T;
gCling->setCommandHandler(&T);

#pragma cling custom0
//      CHECK: custom0

#pragma cling custom1
// CHECK-NEXT: custom1

#pragma cling custom2
// CHECK-NEXT: custom2

#pragma cling custom3
// CHECK-NEXT: custom3

// expected-no-diagnostics
.q
