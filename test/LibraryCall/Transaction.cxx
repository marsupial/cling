//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "Transaction.h"

extern "C" int printf(const char*, ...);

#ifndef CLING_SUBCLASS

void BaseClass::dump(const char* What) const { printf("%s::%s\n", Name, What); }
BaseClass::BaseClass(const char* N) : Name(N) { dump("construct"); }
BaseClass::~BaseClass() { dump("destruct"); }
void BaseClass::DoSomething() const { dump("virtual"); }

static BaseClass B;

#else

SubClass::SubClass() : BaseClass("SubClass") {}
void SubClass::DoSomething() const {
  printf("<");
  BaseClass::DoSomething();
  printf(">\n");
}

static SubClass SC;

#endif



