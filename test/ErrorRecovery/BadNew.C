//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: clang -E -C %s | sed -e '/^#/d' > %t
// RUN: cat %t | %cling -nostdinc++ -I%S/BadNew/A 2>&1 | FileCheck -check-prefix=CHECK -check-prefix=CHECKA %t
// RUN: cat %s | %cling -nostdinc++ -I%S/BadNew/B 2>&1 | FileCheck -check-prefix=CHECK -check-prefix=CHECKB %s
// testBadNewInclude

// CHECK: Warning in cling::IncrementalParser::CheckABICompatibility():
// CHECK:  Possible C++ standard library mismatch, compiled with {{.*$}}

struct a {} TEST
#if defined(__GLIBCXX__) && defined(__APPLE__)
  // CHECKA: error: call to global function cling::executePrintValue() not configured
  // CHECKA: (struct a &) <unknown value>
#else
  // CHECKA: error: 'string' file not found
  // CHECKA: error: RuntimePrintValue.h could not be loaded
#endif

// CHECKB: error: RuntimePrintValue.h could not be loaded

.q
