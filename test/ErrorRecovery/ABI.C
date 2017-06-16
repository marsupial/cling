//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: %cling -C -E -P %s > %t
// RUN: cat %t | %cling -nostdinc++ -Xclang -verify 2>&1 | FileCheck %t

// RUN: %cling -C -E -P -DCLING_VALEXTRACT_ERR %s > %t
// RUN: cat %t | %cling -nostdinc++ -nobuiltininc -Xclang -verify 2>&1 | FileCheck %t

// RUN: %cling -C -E -P -DCLING_RTIME_PRNT_ERR %s > %t
// RUN: cat %t | %cling -I%S/ABI -Xclang -verify 2>&1 | FileCheck %t

#ifndef CLING_RTIME_PRNT_ERR
// expected-error@input_line_1:1 {{'new' file not found}}

//      CHECK: Warning in cling::IncrementalParser::CheckABICompatibility():
// CHECK-NEXT:  Possible C++ standard library mismatch, compiled with {{.*$}}
#endif

struct Trigger {} Tr;

// Try to print twice too make sure not to crash on subsequent attempts

#ifdef CLING_VALEXTRACT_ERR
Tr // expected-error@2 {{ValueExtractionSynthesizer could not find: 'cling_ValueExtraction'.}}
Tr // expected-error@2 {{ValueExtractionSynthesizer could not find: 'cling_ValueExtraction'.}}
#endif

#ifdef CLING_RTIME_PRNT_ERR
Tr // expected-error@cling/Interpreter/RuntimePrintValue.h:2 {{C++ requires a type specifier for all declarations}} expected-error@2 {{RuntimePrintValue.h could not be loaded}}
// CHECK: (struct Trigger &) <<<undefined>>>
#endif

.q
