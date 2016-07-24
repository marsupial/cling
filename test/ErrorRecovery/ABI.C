//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: %cling -C -E -P  %s | %cling -nostdinc++ -Xclang -verify 2>&1 | FileCheck %s
// RUN: %cling -C -E -P -DCLING_VALEXTRACT_ERR %s | %cling -nostdinc++ -nobuiltininc -Xclang -verify 2>&1 | FileCheck %s
// RUN: %cling -C -E -P -DCLING_RTIME_PRNT_ERR %s | %cling -nostdinc++ -nobuiltininc -Xclang -verify 2>&1 | FileCheck %s

// expected-error@cling_Interpreter_initialization_1:1 {{'new' file not found}}

//      CHECK: Warning in cling::IncrementalParser::CheckABICompatibility():
// CHECK-NEXT:  Possible C++ standard library mismatch, compiled with {{.*$}}

#ifdef CLING_VALEXTRACT_ERR
struct Trigger {} Tr // expected-error {{ValueExtractionSynthesizer could not find: 'cling_ValueExtraction'.}}
// FIX-WITH-TMP-FILE-CHECK-NOT: (struct Trigger &) <undefined>
#endif

#ifdef DCLING_RTIME_PRNT_ERR
extern "C" void* cling_ValueExtraction(void*, void*, void*, int, ...) { return 0; }
struct Trigger {} Tr // expected-error {{RuntimePrintValue.h could not be loaded}}
// expected-error@input_line_25:1 {{'cling/Interpreter/RuntimePrintValue.h' file not found}}
// FIX-WITH-TMP-FILE-CHECK-NEXT: (struct Trigger &) <undefined>
#endif
.q
