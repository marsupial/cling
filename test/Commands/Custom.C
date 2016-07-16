//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -I %S -Xclang -verify 2>&1 | FileCheck %s
// Test customComamnd

#include "cling/MetaProcessor/Commands.h"

using namespace cling::meta;

namespace {
  
CommandResult customCommand(CommandArguments&, llvm::StringRef arg) {
  printf("Custom command '%s'\n", arg.str().c_str());
  return kCmdSuccess;
}

CommandResult customCommand2(CommandArguments& args) {
  std::string captured;
  args.execute("? custom", &captured);
  printf("Captured: %s", captured.c_str());
  llvm::StringRef arg = args.nextString();
  while (!arg.empty()) {
    printf("Custom command2 '%s'\n", arg.str().c_str());
    arg = args.nextString();
  }
  return kCmdSuccess;
}

}

// Test inabilty to overwrite builtin commands
Commands::get().add("?", customCommand);
// CHECK-STDERR: Custom command '?' is being replaced

Commands::get().add("custom", customCommand);
.custom "arg1" "arg2" "arg3" "arg4"
// CHECK: Custom command 'arg1'
// CHECK: Custom command 'arg2'
// CHECK: Custom command 'arg3'
// CHECK: Custom command 'arg4'

#pragma cling custom A B C
// CHECK: Custom command 'A'
// CHECK: Custom command 'B'
// CHECK: Custom command 'C'

Commands::get().add("?", customCommand);
// CHECK-STDERR: Cannot replace builtin command '?'

const char* argC = "argC";
#define argD "argD"

Commands::get().add("custom", customCommand2, "CC2Syntax", "CC2Help");
// CHECK-STDERR: Replaced command: 'custom' 

.custom "arg5" "arg6" argC argD
// CHECK: Captured: custom CC2Syntax         CC2Help
// CHECK: Custom command2 'arg5'
// CHECK: Custom command2 'arg6'
// CHECK: Custom command2 'argC'
// CHECK: Custom command2 'argD'

// expected-no-diagnostics
.q
