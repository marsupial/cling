//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -nostdinc++ -I%S/BadNew/A 2>&1 | FileCheck -check-prefix=CHECK -check-prefix=CHECKA %s
// RUN: cat %s | %cling -nostdinc++ -I%S/BadNew/B 2>&1 | FileCheck -check-prefix=CHECK -check-prefix=CHECKB %s
// testBadNewInclude

// CHECK: Warning in cling::IncrementalParser::CheckABICompatibility():
// CHECK:  Possible C++ standard library mismatch, compiled with {{.*$}}

struct a {} TEST
// CHECKA: RuntimePrintValue.h could not be loaded.
// CHECKB: {{.*}} 'string' file not found

.q
