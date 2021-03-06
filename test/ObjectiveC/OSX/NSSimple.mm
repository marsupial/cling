//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -x objective-c++ -Xclang -verify 2>&1 | FileCheck %s
// Test testNSObject

#import <Foundation/NSString.h>
#import <stdio.h>

.L /System/Library/Frameworks/Foundation.Framework/Foundation

@interface Test : NSObject {
  @public
    id Public;
  @private
    id Private;
    @package
    id Package;
  }
  - (void)testMeth;
@end

@implementation Test

- (void) write : (NSString*) str {
  fflush(stdout);
  [str writeToFile:@"/dev/stdout" atomically: NO
                                  encoding: NSASCIIStringEncoding
                                  error: nil];
}

- (void) testMeth {
  [ self write: @"called testMeth\n" ];
}
-(void)dealloc {
  [ self write: @"called dealloced\n" ];
  [super dealloc];
}
@end

Test *t = [[Test alloc] init]
// CHECK: (Test *) <Test: 0x{{[0-9a-f]+}}>
[t testMeth]
// CHECK: called testMeth
[t release];
// CHECK: called dealloced

#import <Foundation/NSDate.h>

[[NSDate alloc] init]
// CHECK: (NSDate *) {{[0-9]+}}-{{[0-9]+}}-{{[0-9]+}} {{[0-9]+}}:{{[0-9]+}}:{{[0-9]+}} +{{[0-9]+}}

// expected-no-diagnostics
.q
