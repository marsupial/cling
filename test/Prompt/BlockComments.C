//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -Xclang -verify 2>&1 | FileCheck %s
// Test blockComments

(/*
   1
   2
   3 */
  8
  // single line 1
  /* single line 2*/
)
// CHECK: (int) 8

// Check nested indentation works
/*
 {
  (
   [
   ]
  )
 }
*/

// Check nested indentation doesn't error on mismatched closures
/*
  {
    [
      (
      }
    )
  ]
*/

( 5
  /*
   + 1
   + 2
   + 3 */
  + 4
  // single line 1
  /* single line 2*/
)
// CHECK: (int) 9

/*
  This should work
  // As should this // */

/*
  This will warn
  // /* */ // expected-warning {{within block comment}}

.rawInput
*/ // expected-error {{expected unqualified-id}}
.rawInput


// This is a side effect of wrapping, expression is compiled as */; so 2 errors
*/ // expected-error@2 {{expected expression}} expected-error@3 {{expected expression}}

// Check preprocessor blocked out
/*
#if 1

#else er
#we not gonna terminate this
  #include "stop messing around.h"
#finished

*/

// Check meta-commands are blocked out
/*
  .L SomeStuff
  .x some more
  .q
*/

( 5
  /*
   + 10
   + 20 */
  /*
    + 30
  */
  + 4
  // single line 1
  + 10
  /* single line 2*/
  /* ) */
)
// CHECK: (int) 19

/* 8 + */ 9 /* = 20 */
// CHECK: (int) 9

.q
