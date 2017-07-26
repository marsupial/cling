//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: rm -rf %T/Tree
// RUN: mkdir -p %T/Tree/DirA
// RUN: mkdir -p %T/Tree/DirB/DirB
// RUN: mkdir -p %T/Tree/DirC
// RUN: echo "const char* LoadedFrom = \"DirB\";  extern \"C\" int funcFace();" > %T/Tree/DirB/libTest
// RUN: echo "const char* LoadedFrom2 = \"DirB/DirB\";" > %T/Tree/DirB/DirB/libTest
// RUN: touch %T/Tree/DirA/a.h
// RUN: touch %T/Tree/DirB/b.h
// RUN: touch %T/Tree/DirC/c.h
// RUN: clang -shared %S/lib.c -o%T/Tree/libTest%shlibext
// RUN: cat %s | %built_cling -I%T/Tree -I%T/Tree/DirA -I%T/Tree/DirB -I%T/Tree/DirC -L%T/Tree -L%T/Tree/DirB -Xclang -verify 2>&1 | FileCheck %s
// RUN: cd %T/Tree && cat %s | %built_cling -DTEST_RELATIVE -I./ -IDirA -IDirB -IDirC -L./ -LDirB -Xclang -verify 2>&1 | FileCheck %s

#include "cling/Interpreter/Interpreter.h"
#include "cling/Utils/FileEntry.h"
#include <string>
#include <cstdio>

// sleep a thread
//#if defined(__CLING__GNUC__) && !defined(_GLIBCXX_USE_NANOSLEEP)
//  #define _GLIBCXX_USE_NANOSLEEP
//#endif
//#include <chrono>
//#include <thread>

//#include <unistd.h>

// Ugh, no functions here!?
 class Rename {
  std::string m_Name0;
  std::string m_Name1;
  bool m_Restore;
  public:
  
  Rename(const std::string &a) : m_Name0(a), m_Name1(a+".h"), m_Restore(false) {
   operator()();
  }
  ~Rename() {
   if (m_Restore)
     operator()();
  }
  void operator () () {
   //llvm::errs() << "Move '" << m_Name0 << "' -> '" << m_Name1 << "'\n";
   std::rename(m_Name0.c_str(), m_Name1.c_str());
   m_Name0.swap(m_Name1);
   m_Restore = !m_Restore;
   // Try to make sure fs has sunch changes before continuing
   //std::this_thread::sleep_for(std::chrono::milliseconds(500));
   //::sync();
  }
};

gCling->lookupFileOrLibrary("DirA/a.h").exists()
// CHECK: (bool) true
gCling->lookupFileOrLibrary("DirB/b.h").exists()
// CHECK: (bool) true
gCling->lookupFileOrLibrary("DirC/c.h").exists()
// CHECK: (bool) true

gCling->lookupFileOrLibrary("DirA/c.h").exists()
// CHECK: (bool) false
gCling->lookupFileOrLibrary("DirB/a.h").exists()
// CHECK: (bool) false
gCling->lookupFileOrLibrary("DirC/b.h").exists()
// CHECK: (bool) false

using namespace cling::utils;
FileEntry feH = gCling->lookupFileOrLibrary("libTest");
feH.filePath()
// CHECK: (const std::string &) "{{.*Tree[/\]DirB[/\]}}libTest"
feH.exists()
// CHECK: (bool) true
feH.isLibrary()
// CHECK: (bool) false

// Move file libTest to libTest.h
Rename renamer(feH.filePath());

gCling->loadFile(feH)
#ifdef TEST_RELATIVE
  // expected-error-re@input_line_59:1 {{'{{.*[/\]DirB[/\]}}libTest' file not found}}
#else
  // expected-error-re@input_line_59:1 {{cannot open file '{{.*[/\]DirB[/\]}}libTest': {{[Nn]}}o such file or directory}}
#endif
// CHECK: (cling::Interpreter::CompilationResult) (cling::Interpreter::CompilationResult::kFailure) : ({{(unsigned )?}}int) 1

gCling->loadFile("libTest")
// CHECK: (cling::Interpreter::CompilationResult) (cling::Interpreter::CompilationResult::kSuccess) : ({{(unsigned )?}}int) 0

// Move file libTest.h back to libTest
renamer();
// ### clang's caching is interfering with how this should work!
//gCling->loadFile(feH)
// NOCHECK: (cling::Interpreter::CompilationResult) (cling::Interpreter::CompilationResult::kSuccess) : ({{(unsigned )?}}int) 0

//LoadedFrom
// NOCHECK: (const char *) "DirB"

//funcFace()
// NOCHECK: (int) 462

renamer();
gCling->loadFile("DirB/libTest")
LoadedFrom2
// CHECK: (const char *) "DirB/DirB"

.q
