//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -Xclang -verify -fno-rtti 2>&1 | FileCheck --match-full-lines %s
// Test command argument parsing

#include "cling/MetaProcessor/Commands.h"
#include "cling/Interpreter/Interpreter.h"
#include "llvm/Support/raw_ostream.h"

using namespace cling;
using namespace cling::meta;

llvm::raw_ostream* Outs;

static void DumpArgs(llvm::StringRef Str, llvm::raw_ostream& Out,
                     unsigned F = CommandHandler::kPopFirstArgument |
                     CommandHandler::kSplitWithGrouping) {
  llvm::SmallVector<CommandHandler::SplitArgument, 8> Args;
  Out << "<" << CommandHandler::Split(Str, Args, F) << ">\n";
  for (auto&& A : Args) {
    Out << "  ";
    A.dump(&Out);
	Out << "\n";
  }
}

class TestImpl : public CommandHandler {
public:
  TestImpl() {}
  ~TestImpl() {}

  virtual CommandResult Execute(const Invocation& I) {
    Outs = &I.Out;
    I.Out << "'" << I.Cmd << "'\n";

	DumpArgs(I.Cmd, I.Out);

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
// CHECK-NEXT:   {"Arg0"}

#pragma cling custom2 Arg0   Arg1
// CHECK-NEXT: 'custom2 Arg0   Arg1'
// CHECK-NEXT: <custom2>
// CHECK-NEXT:   {"Arg0"}
// CHECK-NEXT:   {"Arg1"}

// Intentional trailing space on the next line
#pragma cling   custom3   Arg0   Arg1        Arg2        
// CHECK-NEXT: 'custom3   Arg0   Arg1        Arg2'
// CHECK-NEXT: <custom3>
// CHECK-NEXT:   {"Arg0"}
// CHECK-NEXT:   {"Arg1"}
// CHECK-NEXT:   {"Arg2"}

#pragma cling   groups   <0 1 2 3>   { 4 "" 5 6} [ 7 () 8 9 ]
// CHECK-NEXT: 'groups   <0 1 2 3>   { 4 "" 5 6} [ 7 () 8 9 ]'
// CHECK-NEXT: <groups>
// CHECK-NEXT:   {"0 1 2 3", Group: '<'}
// CHECK-NEXT:   {" 4 "" 5 6", Group: '{'}
// CHECK-NEXT:   {" 7 () 8 9 ", Group: '['}

// Unclosed groups
DumpArgs("nocmd [ < \" {", *Outs, CommandHandler::kSplitWithGrouping);
// CHECK-NEXT: <>
// CHECK-NEXT:   {"nocmd"}
// CHECK-NEXT:   {"[ < " {"}
	  
DumpArgs("cmd [ < \" {", *Outs);
// CHECK-NEXT: <cmd>
// CHECK-NEXT:   {"[ < " {"}

DumpArgs("escape   \"\\\"\" \"b\\' \\\"e\"  \" <{\\\\} \"", *Outs);
// CHECK-NEXT: <escape>
// CHECK-NEXT:   {"\"", Escaped, Group: '"'}
// CHECK-NEXT:   {"b\' \"e", Escaped, Group: '"'}
// CHECK-NEXT:   {" <{\\} ", Escaped, Group: '"'}

DumpArgs("escape2 \"\\\\\narg1\" arg2", *Outs, CommandHandler::kPopFirstArgument);
// CHECK-NEXT: <escape2>
// CHECK-NEXT:   {""\\", Escaped}
// CHECK-NEXT:   {"arg1""}
// CHECK-NEXT:   {"arg2"}

DumpArgs("escape3  arg1 \"\\\narg2\"", *Outs, CommandHandler::kPopFirstArgument);
// CHECK-NEXT: <escape3>
// CHECK-NEXT:   {"arg1"}
// CHECK-NEXT:   {""\
// CHECK-NEXT: arg2"", Escaped}

DumpArgs("escape4 \"\\\\\narg1\" arg2", *Outs);
// CHECK-NEXT: <escape4>
// CHECK-NEXT:   {"\\
// CHECK-NEXT: arg1", Escaped, Group: '"'}
// CHECK-NEXT:   {"arg2"}

DumpArgs("escape5  arg1 \"\\\narg2\"", *Outs);
// CHECK-NEXT: <escape5>
// CHECK-NEXT:   {"arg1"}
// CHECK-NEXT:   {"\
// CHECK-NEXT: arg2", Escaped, Group: '"'}

#pragma cling newline  arg1 arg2 \
                       arg3   arg4
// CHECK-NEXT: 'newline  arg1 arg2 \
// CHECK-NEXT:                        arg3   arg4'
// CHECK-NEXT: <newline>
// CHECK-NEXT:   {"arg1"}
// CHECK-NEXT:   {"arg2"}
// CHECK-NEXT:   {"arg3"}
// CHECK-NEXT:   {"arg4"}

#pragma cling newline2  arg1 \
arg2
// CHECK-NEXT: 'newline2  arg1 \
// CHECK-NEXT: arg2'
// CHECK-NEXT: <newline2>
// CHECK-NEXT:   {"arg1"}
// CHECK-NEXT:   {"arg2"}

std::string TestSeq("1\\n2\\\"3\\\\4\\\'5\\t")
// CHECK: (std::string &) "1\n2\"3\\4\'5\t"
const std::string UnEsc = cling::meta::CommandHandler::Unescape(TestSeq)
// CHECK-NEXT: (const std::string &) "1
// CHECK-NEXT: 2"3\4'5	"
UnEsc.size() == 10
// CHECK-NEXT: (bool) true
(UnEsc[9] == '\t')
// CHECK-NEXT: (bool) true

cling::meta::CommandHandler::Unescape("\\tA")
// CHECK-NEXT: (std::string) "	A"

// expected-no-diagnostics
.q
