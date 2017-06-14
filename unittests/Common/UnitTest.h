//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Roman Zulak
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CING_UNITTEST_COMMON_H
#define CING_UNITTEST_COMMON_H

#include <gtest/gtest.h>

#include <string>
#include <vector>
#include <memory>

namespace cling {

class Interpreter;
  
namespace unittest {

class Args {
  typedef std::vector<const char*> storage_type;
  storage_type m_Args;

  Args(storage_type& Other, size_t N);

public:
  const char* const* const argv;
  const size_t argc;

  static Args Get();
};

std::unique_ptr<cling::Interpreter>
CreateInterpreter(size_t Argc, const char* const* Argv,
                  const char* LLVMDir = LLVMDIR);

std::unique_ptr<cling::Interpreter>
CreateInterpreter(const char* LLVMDir = LLVMDIR);

bool CreateInterpreter(bool (*Proc)(Interpreter&), size_t Argc,
                       const char* const* Argv,
                       const char* LLVMDir = LLVMDIR);

bool CreateInterpreter(bool (*Proc)(Interpreter&),
                       const char* LLVMDir = LLVMDIR);

}
}

using namespace cling::unittest;

#endif // CING_UNITTEST_COMMON_H
