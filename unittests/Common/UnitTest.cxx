//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Roman Zulak
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "UnitTest.h"
#include "cling/Interpreter/Interpreter.h"

Args::Args(storage_type& Other, size_t N)
    : m_Args(std::move(Other)), argv(&m_Args[0]), argc(N) {}

Args Args::Get() {
  const auto& In = testing::internal::GetArgvs();
  storage_type A;
  if (!In.empty()) {
    for (const auto& Arg : In)
      A.push_back(Arg.c_str());
  } else
    A.push_back("");
  return Args(A, In.size());
}

namespace cling {
namespace unittest {

std::unique_ptr<Interpreter>
CreateInterpreter(size_t Argc, const char* const* Argv, const char* LLVMDir) {
  if (Argv)
    return std::unique_ptr<Interpreter>(new Interpreter(Argc, Argv, LLVMDir));

  const Args In = Args::Get();
  return std::unique_ptr<Interpreter>(
      new Interpreter(In.argc, In.argv, LLVMDir));
}

bool CreateInterpreter(bool (*Proc)(Interpreter&), size_t Argc,
                       const char* const* Argv, const char* LLVMDir) {
  if (Argv) {
    Interpreter Interp(Argc, Argv, LLVMDir);
    return Proc(Interp);
  }

  const Args In = Args::Get();
  Interpreter Interp(In.argc, In.argv, LLVMDir);
  return Proc(Interp);
}

std::unique_ptr<Interpreter> CreateInterpreter(const char* LLVMDir) {
  return CreateInterpreter(0, nullptr, LLVMDir);
}

bool CreateInterpreter(bool (*Proc)(Interpreter&), const char* LLVMDir) {
  return CreateInterpreter(Proc, 0, nullptr, LLVMDir);
}

}
}