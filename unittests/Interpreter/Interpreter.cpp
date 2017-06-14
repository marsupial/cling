//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Roman Zulak
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "UnitTest.h"
#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Value.h"

TEST(Interpreter, macro) {
  auto Interp = CreateInterpreter();
  EXPECT_TRUE( Interp->getMacro("__CLING__") != nullptr );
}

static bool TestOne(cling::Interpreter& Interp) {
  cling::Value Val;
  Interp.echo("\"12345\"", &Val);
  EXPECT_TRUE( Val.hasValue() );
  EXPECT_TRUE( strcmp((const char*)Val.getPtr(), "12345") == 0 );
  return true;
}

/* FIXME: Failing

TEST(Interpreter, multiple) {
  CreateInterpreter(TestOne);
  CreateInterpreter(TestOne);
}

TEST(Interpreter, dual) {
  auto InterpA = CreateInterpreter();
  auto InterpB = CreateInterpreter();
  TestOne(*InterpA);
  TestOne(*InterpB);
}

*/