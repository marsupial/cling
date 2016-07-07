//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// Test that runtime-printing still works when not C++11

// RUN: cat %s | %cling -std=c++98 -Xclang -verify 2>&1 | FileCheck %s
// testNotCxx-11

class NoMatch {};
NoMatch n;
n
// CHECK: (NoMatch &) @0x{{[a-z0-9]+}}

#include <string>
#include <vector>
#include <set>
#include <map>
   
std::map<int, std::string> m;
m[0] = "PORK";
m[1] = "FACE";
m[2] = "GOOD";
m
// CHECK: (std::map<{{.*}},{{.*}}> &) { 0 => "PORK", 1 => "FACE", 2 => "GOOD" }

#include <vector>
std::vector<int> v;
v.push_back(10);
v.push_back(11);
v.push_back(12);
v.push_back(13);
v.push_back(14);
v
// CHECK: (std::vector<{{.*}}> &) { 10, 11, 12, 13, 14 }

std::set<std::string> s;
s.insert("A");
s.insert("B");
s.insert("C");
s.insert("D");
s.insert("E");
s
// CHECK: (std::set<{{.*}}> &) { "A", "B", "C", "D", "E" }

std::make_pair(std::string("DEF"), std::string("BORB"))
// CHECK: (std::pair<{{.*}},{{.*}}>) { "DEF", "BORB" }

// expected-no-diagnostics
.q
