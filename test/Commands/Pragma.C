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

  virtual CommandResult Execute(const Invocation& I) {
    I.Out << "'" << I.Args << "'\n";
    llvm::SmallVector<llvm::StringRef, 8> Args;
    I.Out << "<" << CommandHandler::Split(I.Args, Args) << ">\n";
    for (auto&& A : Args)
      I.Out << "  '" << A << "'\n";
    return kCmdSuccess;
  }
};
TestImpl T;
gCling->setCommandHandler(&T);

#pragma cling custom0
//      CHECK: 'custom0'
// CHECK-NEXT: <custom0>

#pragma cling custom1 Arg0
// CHECK-NEXT: 'custom1 Arg0'
// CHECK-NEXT: <custom1>
// CHECK-NEXT:   'Arg0'

#pragma cling custom2 Arg0   Arg1
// CHECK-NEXT: 'custom2 Arg0   Arg1'
// CHECK-NEXT: <custom2>
// CHECK-NEXT:   'Arg0'
// CHECK-NEXT:   'Arg1'

// Intentional trailing space on the next line
#pragma cling   custom3   Arg0   Arg1        Arg2        
// CHECK-NEXT: 'custom3   Arg0   Arg1        Arg2'
// CHECK-NEXT: <custom3>
// CHECK-NEXT:   'Arg0'
// CHECK-NEXT:   'Arg1'
// CHECK-NEXT:   'Arg2'

// expected-no-diagnostics
.q
