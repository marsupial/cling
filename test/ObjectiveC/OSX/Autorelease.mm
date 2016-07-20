//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -x objective-c++ -Xclang -verify 2>&1 | FileCheck %s
// Test testAutoReleasePool

#import <Foundation/NSAutoreleasePool.h>

.L /System/Library/Frameworks/Foundation.Framework/Foundation

@interface ARTest : NSObject
@end

@implementation ARTest

-(void)dealloc {
  NSLog(@"called ARTest dealloc\n");
  [super dealloc];
}
@end

{
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  ARTest *ar = [[[ARTest alloc] init] autorelease];
  [pool release];
}
// CHECK: called ARTest dealloc

// expected-no-diagnostics
.q
