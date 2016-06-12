//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// Various inconsistencies in commands parsing file names

// RUN: cat %s | %cling -L %S -I %S -Xclang -verify | FileCheck %s
// XFAIL: *
// Test dotFileCommands

extern "C" int printf(const char*,...);

// Normal bash syntax
#undef  LOADED_FUNC
#define LOADED_FUNC quotedL
.L "Lload This File.h"
LOADED_FUNC();
// CHECK: quotedL

#undef  LOADED_FUNC
#define LOADED_FUNC quotedX
.x "Lload This File.h"
// CHECK: LloadThisFile
// Error trying to call illegal function name
LOADED_FUNC();
// CHECK: quotedX
// Error Transaction rolled back to #define LOADED_FUNC quotedL

#undef  LOADED_FUNC
#define LOADED_FUNC spacesL
.L Lload This File.h
LOADED_FUNC();
// CHECK: Lload: spacesL
This();
// CHECK: This
Fileh();
// CHECK: Fileh.h

#undef  LOADED_FUNC
#define LOADED_FUNC spacesX
.x Lload This File.h
// CHECK-NOT: LloadThisFile
// CHECK: Lload: 'This', 'File.h'
LOADED_FUNC();
// CHECK: spacesX
// Error Transaction rolled back to #define LOADED_FUNC spacesL

// expected-no-diagnostics
.q
