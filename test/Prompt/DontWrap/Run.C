//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -DTEST_PATH=\"%S\" -DTEST_OUT=\"%t\" -Xclang -verify
// RUN: cat %t | %cling -Xclang -verify 2>&1 | FileCheck --allow-empty %t
// Test dontWrapDefinitions


#include <fstream>
#include <iterator>
#include <algorithm>

#if defined(__GLIBCXX__) && (__GLIBCXX__ < 20141104)
  #define TEST_NAME "Fail.h"
#else
  #define TEST_NAME "Pass.h"
#endif

using namespace std;
ifstream source(TEST_PATH "/" TEST_NAME, ios::binary);
ofstream dest(TEST_OUT, ios::binary);

copy(istreambuf_iterator<char>(source), istreambuf_iterator<char>(),
     ostreambuf_iterator<char>(dest));

source.close();
dest.close();

// expected-no-diagnostics
.q
