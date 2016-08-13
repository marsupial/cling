//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: mkdir -p %T/subdir && clang -shared %S/call_lib.c -o %T/subdir/libtest%shlibext
// RUN: export ENVVAR_LIB="%T/subdir" ; export ENVVAR_INC="%S/subdir" ; export ENVVAR_DLMA="A:B:C:D" 
// RUN: cat %s | %cling -I %S -Xclang -verify 2>&1 | FileCheck %s

#pragma cling add_include_path("$ENVVAR_INC")
#include "Include_header.h"
include_test()
// CHECK: OK(int) 0

#pragma cling add_library_path("$ENVVAR_LIB")
#pragma cling load("libtest")

#pragma cling add_library_path("$NONEXISTINGVARNAME")

#pragma cling add_include_path "$ENVVAR_DLMA" : "WIN;STYLE" ;
#pragma cling add_include_path("4,5,6,7","PORK","SANDWICH")
#pragma cling add_include_path("4;5;6;7",;)
#pragma cling add_include_path("A,B,C,D",  ERR) // expected-error {{expected string literal or single character}}

.I
// CHECK: A
// CHECK: B
// CHECK: C
// CHECK: D
// CHECK: WIN
// CHECK: STYLE
// CHECK: 4,5,6,7
// CHECK: PORK
// CHECK: SANDWICH
// CHECK: 4
// CHECK: 5
// CHECK: 6
// CHECK: 7
