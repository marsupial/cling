//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -x objective-c++ -Wno-objc-root-class -Xclang -verify 2>&1 | FileCheck -allow-empty %s
// Test testSimple

#define NS_ENUM(_type, _name) enum _name : _type _name; enum _name : _type
#define NS_OPTIONS(_type, _name) enum _name : _type _name; enum _name : _type

.storeState "A"

@interface Base
@end
@implementation Base
@end

@interface Test : Base {
  union {
    struct {
      unsigned a;
    } _singleRange;
    struct {
      unsigned b;
    } _multipleRanges;
  } _internal;
}
  - (void)testMeth;
  typedef NS_ENUM(unsigned, TestEnum) { A = 0, B = 1, C = 2 };
  struct Local {
  };
@end

@implementation Test
- (void) testMeth {
    return;
}
@end

.undo // @implementation
.undo // @interface Test
.undo // @implementation Base
.undo // @interface Base

.compareState "A"
// CHECK-NOT: Differences

// expected-no-diagnostics

.q
