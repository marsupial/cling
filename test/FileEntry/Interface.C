//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

// RUN: clang -shared %S/lib.c -o%p/libTest%shlibext
// RUN: echo "const char *kShlibExt = \"" %shlibext "\";" > %p/shlibext.h
// RUN: cat %s | %cling -I%p -I%clanggeninc -IDirA -IDirB -I DirC -L %p -L DirB -Xclang -verify 2>&1 | FileCheck %s

// Tests the ability of cling to host itself. We can have cling instances in
// cling's runtime. This is important for people, who use cling embedded in
// their frameworks.

#include "cling/Interpreter/Interpreter.h"
#include "cling/Utils/FileEntry.h"
#include <cstdio>


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
feH.exists()
// CHECK: (bool) true
feH.isLibrary()
// CHECK: (bool) false

// Move file libTest to libTest.h
const std::string renamed = feH.filePath()+".h";
std::rename(feH.filePath().c_str(), renamed.c_str());

gCling->loadFile(feH) // expected-error-re@1 {{'{{.*}}/DirB/libTest' file not found}}
// CHECK: (cling::Interpreter::CompilationResult) (cling::Interpreter::CompilationResult::kFailure) : (unsigned int) 1

gCling->loadFile("libTest")
// CHECK: (cling::Interpreter::CompilationResult) (cling::Interpreter::CompilationResult::kSuccess) : (unsigned int) 0

// Move file libTest.h back to libTest
std::rename(renamed.c_str(), feH.filePath().c_str());
gCling->loadFile(feH)
// CHECK: (cling::Interpreter::CompilationResult) (cling::Interpreter::CompilationResult::kSuccess) : (unsigned int) 0

LoadedFrom
// CHECK: (const char *) "DirB"

funcFace()
// CHECK: (int) 462

std::rename(feH.filePath().c_str(), renamed.c_str());
gCling->loadFile("DirB/libTest")
LoadedFrom2
// CHECK: (const char *) "DirB/DirB"
std::rename(renamed.c_str(), feH.filePath().c_str());

.q
