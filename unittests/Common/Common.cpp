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

// Tests the common functionality in UnitTest.cxx

TEST(Common, local) {
  const Args A = Args::Get();
  cling::Interpreter interp(A.argc, A.argv, LLVMDIR);
}

TEST(Common, heap) {
  auto UniquePtr = CreateInterpreter();
}

static bool CallbackA(cling::Interpreter& I) {
  return true;
}

static bool CallbackB(cling::Interpreter& I) {
  return false;
}

TEST(Common, callbacks) {
  EXPECT_TRUE( CreateInterpreter(CallbackA) );
  EXPECT_FALSE( CreateInterpreter(CallbackB) );
}
