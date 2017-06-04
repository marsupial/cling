//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.

//------------------------------------------------------------------------------

// RUN: cat %s | %cling -Xclang -verify 2>&1 | FileCheck %s

#include <string>

struct StdString { operator std::string() const { return "StdConvertor"; } } ss
// CHECK: (struct StdString &) "StdConvertor"

struct WString { operator std::wstring() const { return L"WString"; } } ws
// CHECK: (struct WString &) L"WString"

struct U16String { operator std::u16string() const { return u"U16String"; } } u1
// CHECK: (struct U16String &) u"U16String"

struct U32String { operator std::u32string() const { return U"U32String"; } } u2
// CHECK: (struct U32String &) U"U32String"

// expected-no-diagnostics
