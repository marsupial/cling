//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -nostdinc++ -I%S/BadNew/A 2>&1 | FileCheck %s
// testBadNewInclude

// CHECK: Warning in cling::IncrementalParser::CheckABICompatibility():
// CHECK:  Possible C++ standard library mismatch, compiled with {{.*$}}

struct a {} TEST
// CHECK: RuntimePrintValue.h could not be loaded.

.q
