//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: cat %s | %cling -Xclang -verify | FileCheck %s

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Value.h"

.rawInput 1

struct Test {
  struct Counter {
    char m_Test, m_Instance;
    Counter() : m_Test('a'-1), m_Instance(0) {}
    Counter(Counter &Base, char C = 0)
        : m_Test(C ? C : Base.m_Test), m_Instance(Base.m_Instance++) {}
    ~Counter() { m_Test = '!'; m_Instance = -1; }

    void next() {
      m_Test++;
      m_Instance = 0;
    }

    std::string str(std::string Begin = "") const {
      if (!Begin.empty()) Begin.append(1, ' ');
      Begin.append(1, m_Test);
      Begin.append(1, ':');
      Begin += std::to_string(int(m_Instance));
      return Begin;
    }

    void dump(std::string Begin, const Test* T) const {
      printf("%s %p\n", str(std::move(Begin)).c_str(), T);
    }
 };

  static Counter sInstance;
  Counter Instance;

  Test() : Instance(sInstance) { Instance.dump("++TERD", this); }
  Test(char C) : Instance(sInstance, C) { Instance.dump("++TERD", this); }
  Test(const Test&) : Instance(sInstance) { Instance.dump("(*COPIED*)", this); }
  ~Test() { Instance.dump("~~TERD", this); }
};
struct TestDfltArgs : public Test {
  template <class T> static std::string Args(std::string N, T arg0, bool arg1) {
    return N + ": " + std::to_string(arg0) + "," + std::to_string(arg1);
  }
  TestDfltArgs(char Arg = 0, bool Dflt2 = true) : Test(Arg) { Instance.dump(Args("  args0", Arg, Dflt2), this); }
  TestDfltArgs(int NoDeflt, bool Dflt = true) { Instance.dump(Args("  args1", NoDeflt, Dflt), this); }
};
Test::Counter Test::sInstance;

struct Scope {
  const std::string Name;
  Scope(std::string N) : Name(std::move(N)) {
    printf("---%s-->\n", Name.c_str());
    Test::sInstance.next();
  }
  ~Scope() { printf("<--%s---\n", Name.c_str()); }
};

static cling::Value& ScopeIt(std::string Name, const char* Eval, cling::Value& V) {
  Scope S(std::move(Name));
  gCling->evaluate(Eval, V);
  return V;
}

static cling::Value ScopeIt(std::string Name, const char* Eval) {
  cling::Value V;
  ScopeIt(std::move(Name), Eval, V);
  return V;
}

static cling::Value Placement(std::string Name, const char* Eval) {
  cling::Value V;
  ScopeIt(std::string("placement-"+Name), Eval, V);
  return V;
}

namespace cling {
  std::string printValue(const Test* T) {
    return T->Instance.str();
  }
}
.rawInput
// CHECK: Not using raw input

cling::Value Vt0;
ScopeIt("evaluate0", "Test t", Vt0)
// CHECK-NEXT: ---evaluate0-->
// CHECK-NEXT: ++TERD a:0
// CHECK-NEXT: <--evaluate0---
// CHECK-NEXT: (cling::Value &) boxes [(Test) a:0]


cling::Value Vt1;
ScopeIt("evaluate1", "Test t[1]", Vt1)
// CHECK-NEXT: ---evaluate1-->
// CHECK-NEXT: ++TERD b:0
// CHECK-NEXT: <--evaluate1---
// CHECK-NEXT: (cling::Value &) boxes [(Test [1]) { b:0 }]


cling::Value Vt2;
ScopeIt("evaluate2", "Test t[2]", Vt2)
// CHECK-NEXT: ---evaluate2-->
// CHECK-NEXT: ++TERD c:0
// CHECK-NEXT: ++TERD c:1
// CHECK-NEXT: <--evaluate2---
// CHECK-NEXT: (cling::Value &) boxes [(Test [2]) { c:0, c:1 }]


cling::Value Vt3;
ScopeIt("evaluate3", "Test t[3] = { Test('o'), Test('v'), Test('l') }", Vt3)
Vt3 = cling::Value();
printf("~~~evaluate3~~~\n");
// CHECK-NEXT: ---evaluate3-->
// CHECK-NEXT: ++TERD o:0
// CHECK-NEXT: ++TERD v:1
// CHECK-NEXT: ++TERD l:2
// CHECK-NEXT: <--evaluate3---
// CHECK-NEXT: (cling::Value &) boxes [(Test [3]) { o:0, v:1, l:2 }]
// CHECK-NEXT: ~~TERD l:2
// CHECK-NEXT: ~~TERD v:1
// CHECK-NEXT: ~~TERD o:0
// CHECK-NEXT: ~~~evaluate3~~~


if (1) {
 Scope S("scope");
 Test t[3];
 printf("%s\n", cling::printValue(&t).c_str());
}
// CHECK-NEXT: ---scope-->
// CHECK-NEXT: ++TERD e:0
// CHECK-NEXT: ++TERD e:1
// CHECK-NEXT: ++TERD e:2
// CHECK-NEXT: { e:0, e:1, e:2 }
// CHECK-NEXT: ~~TERD e:2
// CHECK-NEXT: ~~TERD e:1
// CHECK-NEXT: ~~TERD e:0
// CHECK-NEXT: <--scope---


Test::sInstance.next();
printf("---print-->\n");
Test t4[4]
printf("<--print---\n");
// CHECK-NEXT: ---print-->
// CHECK-NEXT: ++TERD f:0
// CHECK-NEXT: ++TERD f:1
// CHECK-NEXT: ++TERD f:2
// CHECK-NEXT: ++TERD f:3
// CHECK-NEXT: (Test [4]) { f:0, f:1, f:2, f:3 }
// CHECK-NEXT: <--print---


ScopeIt("reference", "t4", Vt3)
// CHECK-NEXT: ---reference-->
// NO 'g'
// CHECK-NEXT: <--reference---
// CHECK-NEXT: (cling::Value &) boxes [(Test [4]) { f:0, f:1, f:2, f:3 }]


cling::Value Vplc0;
char Buf0[sizeof(Test)];
ScopeIt("placement0", "Test *Tp = new(Buf0) Test()", Vplc0)
// ScopeIt("placement0", "Test *Tp = new(Buf0) Test()", Vplc0)
// CHECK-NEXT: ---placement0-->
// CHECK-NEXT: ++TERD h:0
// CHECK-NEXT: <--placement0---
// CHECK-NEXT: (cling::Value &) boxes [(Test *) 0x{{.*}}]


cling::Value VMd;
ScopeIt("multidim", "int multiDimArray[2][3][4]{{{1,2,3,4},{11,12,13,14},{21,22,23,24}},{{101,102,103,104},{111,112,113,114},{121,122,123,124}}}", VMd)
// CHECK-NEXT: ---multidim-->
// CHECK-NEXT: <--multidim---
// CHECK-NEXT: (cling::Value &) boxes [(int [2][3][4]) { { { 1, 2, 3, 4 }, { 11, 12, 13, 14 }, { 21, 22, 23, 24 } }, { { 101, 102, 103, 104 }, { 111, 112, 113, 114 }, { 121, 122, 123, 124 } } }]


cling::Value Vplc1;
int Buf1[4*3];
ScopeIt("placement1", "new(Buf1) int[4][3] {{0,1,2},{3,4,5},{6,7,8},{9,10,11}}", Vplc1)
typedef int Caster[4][3];
*(Caster*)Vplc1.getPtr()
(Vplc1.getPtr() == (void*)&Buf1)
// CHECK-NEXT: ---placement1-->
// CHECK-NEXT: <--placement1---
// CHECK-NEXT: (cling::Value &) boxes [(int (*)[3]) 0x{{.*}}]
// CHECK-NEXT: (Caster) { { 0, 1, 2 }, { 3, 4, 5 }, { 6, 7, 8 }, { 9, 10, 11 } }
// CHECK-NEXT: (bool) true


{
  Scope SC("defaultargs");
  cling::Value Vda;
  gCling->evaluate("TestDfltArgs Na", Vda);
  gCling->evaluate("TestDfltArgs D0a('L')", Vda);
  gCling->evaluate("TestDfltArgs D0b('M', false)", Vda);
  gCling->evaluate("TestDfltArgs D1b(1)", Vda);
  gCling->evaluate("TestDfltArgs D1a(10, false)", Vda);
  printf("** %s **\n", cling::printValue(&Vda).c_str());
}
// CHECK-NEXT: ---defaultargs-->
// CHECK-NEXT: ++TERD k:0
// CHECK-NEXT:   args0: 0,1 k:0
// CHECK-NEXT: ~~TERD k:0
// CHECK-NEXT: ++TERD L:1
// CHECK-NEXT:   args0: 76,1 L:1
// CHECK-NEXT: ~~TERD L:1
// CHECK-NEXT: ++TERD M:2
// CHECK-NEXT:   args0: 77,0 M:2
// CHECK-NEXT: ~~TERD M:2
// CHECK-NEXT: ++TERD k:3
// CHECK-NEXT:   args1: 1,1 k:3
// CHECK-NEXT: ~~TERD k:3
// CHECK-NEXT: ++TERD k:4
// CHECK-NEXT:   args1: 10,0 k:4
// CHECK-NEXT: ** boxes [(TestDfltArgs) k:4] **
// CHECK-NEXT: ~~TERD k:4
// CHECK-NEXT: <--defaultargs---


cling::Value Vtn;
ScopeIt("new0", "Test *t = new Test", Vtn)
// CHECK-NEXT: ---new0-->
// CHECK-NEXT: ++TERD l:0
// CHECK-NEXT: <--new0---
// CHECK-NEXT: (cling::Value &) boxes [(Test *) 0x{{.*}}]

// Try to force a crash if bad ownership (double free)
delete static_cast<Test*>(Vtn.getPtr());
// CHECK-NEXT: ~~TERD l:0

cling::Value VScp;
ScopeIt("scopedeval", "{Test A[2];} Test B; return Test('S');", VScp)
// CHECK-NEXT: ---scopedeval-->
// CHECK-NEXT: ++TERD m:0
// CHECK-NEXT: ++TERD m:1
// CHECK-NEXT: ~~TERD m:1
// CHECK-NEXT: ~~TERD m:0
// CHECK-NEXT: ++TERD m:2
// CHECK-NEXT: ++TERD S:3
// CHECK-NEXT: ~~TERD m:2
// CHECK-NEXT: <--scopedeval---
// CHECK-NEXT: (cling::Value &) boxes [(Test) S:3]



Test::sInstance.next();
cling::Value Vlcl;
gCling->evaluate("Test();", Vlcl);
// CHECK-NEXT: ++TERD n:0
Vlcl
// CHECK-NEXT: (cling::Value &) boxes [(Test) n:0]

gCling->evaluate("return Test();", Vlcl);
// CHECK-NEXT: ~~TERD n:0
// CHECK-NEXT: ++TERD n:1
Vlcl
// CHECK-NEXT: (cling::Value &) boxes [(Test) n:1]

gCling->evaluate("return TestDfltArgs('R');", Vlcl);
// CHECK-NEXT: ~~TERD n:1
// CHECK-NEXT: ++TERD R:2
// CHECK-NEXT:   args0: 82,1 R:2
Vlcl
// CHECK-NEXT: (cling::Value &) boxes [(TestDfltArgs) R:2]



// *** Static destruction ***

// Leaking Tp (User allocated)
//  CHECK-NOT: ~~TERD h:0

// reference constructed nothing
//  CHECK-NOT: ~~TERD g:0

// Force Vt1 to the head of the line
Vt1 = Vt2;
// CHECK-NEXT: ~~TERD b:0

// expected-no-diagnostics
.q
