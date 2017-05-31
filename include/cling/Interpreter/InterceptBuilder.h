//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author: Roman Zulak
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_INTERPRETER_INTERCEPT_BUILDER_H
#define CLING_INTERPRETER_INTERCEPT_BUILDER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
  class AttributeList;
  class Function;
  class LLVMContext;
  class Module;
  class StringRef;
  class Type;
}

namespace cling {
  class IncrementalExecutor;

// Build & emit LLVM function overrides that will call into a DispatchProc.
//
// Arg0 and Arg1 are forwarded from the call site, but nothing else.
//   InterceptBuilder Build(Module, DispatchProc);
//   Build("__cxa_atexit", this /*Usr Ptr*/, 3 /*Number of Args*/, 111 /*Cmd*/);
// Translates a call from the JIT to __cxa_atexit as follows:
//   __cxa_atexit(Func, Arg, __dso_handle) -> DispatchProc(Func, Arg, 111, this)
//
class InterceptBuilder {
  typedef int (*DispatchProc)(void* A0, void* A1, unsigned Cmd, void* T);
  typedef llvm::SmallVectorImpl<llvm::Function*> iterator;

  llvm::SmallVector<llvm::Function*, 8> m_Functions;
  llvm::LLVMContext& m_Ctx;
  llvm::Type* I32;
  llvm::Type* PtrT;
  llvm::Type* Void;
  llvm::Module *m_Module;
  llvm::AttributeList* m_Attrs;
  DispatchProc m_Dispatch;

  bool Build(llvm::Function *F, void *Ptr, unsigned Cmd);

  // Build a function declaration :
  // void | int  Name (void*, void*, void*, ... NArgs)
  llvm::Function* Build(llvm::StringRef Name, bool Ret, unsigned NArgs,
                        void* Ptr, unsigned Cmd);
public:
  InterceptBuilder(llvm::Module* Module, DispatchProc Dispatch,
                   llvm::AttributeList* Attrs = nullptr);

  llvm::Function* operator () (llvm::StringRef Name, bool Ret, unsigned NArgs,
                               void* Ptr, unsigned Cmd) {
    return Build(Name, Ret, NArgs, Ptr, Cmd);
  }

  llvm::Function* operator () (llvm::StringRef Name, void* Ptr,
                               unsigned NArgs = 1, unsigned Cmd = 0) {
    return Build(Name, true, NArgs, Ptr, Cmd);
  }
  
  // Force built function to be emitted to the JIT
  void Emit(IncrementalExecutor* Exec, bool Explicit = false);
};

}

#endif // CLING_INTERPRETER_INTERCEPT_BUILDER_H
