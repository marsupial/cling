//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -Xclang -verify -fno-rtti 2>&1 | FileCheck --match-full-lines %s
// Test command argument parsing

// FIXME: <functional> on Windows uses typeid, which presents a problem as this
// test is usually run with -fno-rtti.
#ifdef _WIN32
#define typeid(x) (*(type_info*)nullptr)
#endif

#include "cling/MetaProcessor/Commands.h"
#include "cling/Interpreter/Interpreter.h"
#include "cling/Utils/Output.h"

using namespace cling;
using namespace cling::meta;

CommandHandler Cmds;
gCling->setCommandHandler(&Cmds);


static CommandResult DoOneLArg(const Invocation& I, CommandHandler::Argument Arg) {
  I.Out << "llvm: " << Arg.Str << "\n";
  return kCmdSuccess; 
}

static CommandResult DoOneSArg(const Invocation& I, const std::string& Arg) {
  I.Out << "std: " << Arg << "\n";
  return kCmdSuccess; 
}

static CommandResult DoTwoArgs(const Invocation& I, llvm::StringRef Arg1, llvm::StringRef Arg2) {
  I.Out << "Two: (" << Arg1 << ", " << Arg2 << ")\n";
  return kCmdSuccess; 
}

Cmds.AddCommand("LLVM", &DoOneLArg, "");
Cmds.AddCommand("STD", &DoOneSArg, "");
Cmds.AddCommand("TWO", &DoTwoArgs, "");


#pragma cling LLVM  A0 A1 A2 { A3, A4, A5 }  "ESC\tSEQ"
//      CHECK: llvm: A0
// CHECK-NEXT: llvm: A1
// CHECK-NEXT: llvm: A2
// CHECK-NEXT: llvm: A3, A4, A5
// CHECK-NEXT: llvm: ESC\tSEQ

#pragma cling STD  A0 A1 A2 "ESC\tSEQ"
// CHECK-NEXT: std: A0
// CHECK-NEXT: std: A1
// CHECK-NEXT: std: A2
// CHECK-NEXT: std: ESC	SEQ

#pragma cling TWO  A0 A1 A2 A3 A4
// CHECK-NEXT: Two: (A0, A1)
// CHECK-NEXT: Two: (A2, A3)
// CHECK-NEXT: Two: (A4, )

auto Lamb =  [](const Invocation& I) -> CommandResult {
                  smallstream Capt;
                  I.Execute("TWO Cap0 Cap1", Capt);
                  I.Out << "ALAMBDA CAPT: " << Capt.str();
                  return kCmdSuccess;
                };
Cmds.AddCommand("ALAMBDA", Lamb, "");

#pragma cling ALAMBDA
// CHECK-NEXT: ALAMBDA CAPT: Two: (Cap0, Cap1)

Cmds.AddCommand("LAMBDA",
                [](const Invocation& I) -> CommandResult {
                  I.Out << "DONE\n";
                  return kCmdSuccess;
                }, "");

#pragma cling LAMBDA
// CHECK-NEXT: DONE

// expected-no-diagnostics
.q
