//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Roman Zulak
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/Interpreter/InterceptBuilder.h"
#include "cling/Utils/Casting.h"

#include "IncrementalExecutor.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace cling;

InterceptBuilder::InterceptBuilder(llvm::Module *Module,
                                   DispatchProc Dispatch,
                                   llvm::AttributeList *Attrs)
    : m_Ctx(Module->getContext()), I32(llvm::Type::getInt32Ty(m_Ctx)),
      PtrT(llvm::Type::getInt8PtrTy(m_Ctx)), Void(llvm::Type::getVoidTy(m_Ctx)),
      m_Module(Module), m_Attrs(Attrs), m_Dispatch(Dispatch) {}

bool InterceptBuilder::Build(llvm::Function *F, void *Ptr, unsigned Cmd) {
  if (!F)
    return false;

  llvm::BasicBlock* Block = llvm::BasicBlock::Create(m_Ctx, "", F);
  llvm::IRBuilder<> Builder(Block);

  // Forward first 2 args passed to the function, casting to PtrT
  llvm::SmallVector<llvm::Value*, 4> Args;
  llvm::Function::arg_iterator FArg = F->arg_begin();
  switch (F->arg_size()) {
    default:
    case 2:
      Args.push_back(Builder.CreateBitOrPointerCast(&(*FArg), PtrT));
      ++FArg;
    case 1:
      Args.push_back(Builder.CreateBitOrPointerCast(&(*FArg), PtrT));
      ++FArg;
    case 0:
      break;
  }

  // Add remaining arguments
  switch (Args.size()) {
    case 0: Args.push_back(llvm::Constant::getNullValue(PtrT));
    case 1: Args.push_back(llvm::Constant::getNullValue(PtrT));
    case 2: Args.push_back(Builder.getInt32(Cmd));
    default: break;
  }
  assert(Args.size() == 3 && "Wrong number of arguments");

  // Add the final void* argument
  Args.push_back(Builder.CreateIntToPtr(Builder.getInt64(uintptr_t(Ptr)),
                                        PtrT));

  // typedef int (*) (void*, void*, unsigned, void*) FuncPtr;
  // FuncPtr FuncAddr = (FuncPtr) 0xDoCommand;
  llvm::SmallVector<llvm::Type*, 4> ArgTys = { PtrT, PtrT, I32, PtrT };
  llvm::Value* FuncAddr = Builder.CreateIntToPtr(
      Builder.getInt64(uintptr_t(utils::FunctionToVoidPtr(m_Dispatch))),
      llvm::PointerType::get(llvm::FunctionType::get(I32, ArgTys, false),
                             0),
      "FuncCast");

  // int rval = FuncAddr(%0, %1, Cmd, Ptr);
  llvm::Value* Result = Builder.CreateCall(FuncAddr, Args, "rval");

  // return rval;
  Builder.CreateRet(Result);
  return true;
}

// FIXME: Alias all functions with first that matches Ptr, Cmd and NArgs >=
llvm::Function *InterceptBuilder::Build(llvm::StringRef Name, bool Ret,
                                        unsigned NArgs, void *Ptr,
                                        unsigned Cmd) {
  // Declare the function [void|int] Name (void* [,void*])
  llvm::SmallVector<llvm::Type *, 8> ArgTy(NArgs, PtrT);
  llvm::Type *RTy = Ret ? I32 : Void;
  llvm::Function *F = llvm::cast_or_null<llvm::Function>(
      m_Attrs
          ? m_Module->getOrInsertFunction(
                Name, llvm::FunctionType::get(RTy, ArgTy, false), *m_Attrs)
          : m_Module->getOrInsertFunction(
                Name, llvm::FunctionType::get(RTy, ArgTy, false)));

  if (F && Build(F, Ptr, Cmd)) {
    m_Functions.push_back(F);
    return F;
  }
  return nullptr;
}

void InterceptBuilder::Emit(IncrementalExecutor *Exec, bool Explicit) {
  for (auto &&F : m_Functions) {
    if (void *Addr = Exec->getPointerToGlobalFromJIT(*F)) {
      if (Explicit) {
        // Add to injected symbols explicitly, as some formats (COFF)
        // don't tag individual symbols as exported and the JIT needs this.
        // https://reviews.llvm.org/rL258665
        Exec->addSymbol(F->getName(), Addr, true);
      }
    }
    else
      llvm::errs() << "Function '" << F->getName() << "' was not overloaded\n";
  }
}
