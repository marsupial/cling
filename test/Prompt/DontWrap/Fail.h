
static int staticFunc(int a) {
  return 1 + a;
}

// expected-error {{function definition is not allowed here}}

namespace test {
  static int staticFunc(int a) {
    return 1 + a;
  }
}
test::staticFunc(15)
// CHECK: (int) 16

.q
