//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: %cling -E -C -P %s > %t
// RUN: cat %t | %cling -nostdinc++ -I%S/BadNew/A -Xclang -verify 2>&1 | FileCheck %t
// RUN: %cling -E -C -P -DCLING_TESTB %s > %t
// RUN: cat %t | %cling -nostdinc++ -I%S/BadNew/B -Xclang -verify 2>&1 | FileCheck %t
// testBadNewInclude

// CHECK: Warning in cling::IncrementalParser::CheckABICompatibility():
// CHECK:  Possible C++ standard library mismatch, compiled with {{.*$}}

struct a {} TEST
#ifdef CLING_TESTB
// expected-error@new:3 {{C++ requires a type specifier for all declarations}}
#endif

// expected-error@cling/Interpreter/RuntimePrintValue.h:* {{'string' file not found}}
// expected-error {{RuntimePrintValue.h could not be loaded}}

.q
