//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vasil.georgiev.vasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_VALUE_EXTRACTION_SYNTHESIZER_H
#define CLING_VALUE_EXTRACTION_SYNTHESIZER_H

#include "ASTTransformer.h"

namespace clang {
  class Decl;
  class Expr;
}

namespace cling {
  class Interpreter;

  class ValueExtractionSynthesizer : public WrapperTransformer {
    ///\brief Owning Interpreter (may be a child of another Intepreter).
    ///
    Interpreter& m_Interp;

    ///\brief cling::runtime::internal::setValueNoAlloc cache.
    ///
    clang::Expr* m_UnresolvedNoAlloc;

    ///\brief cling::runtime::internal::setValueWithAlloc cache.
    ///
    clang::Expr* m_UnresolvedWithAlloc;

    ///\brief cling::runtime::internal::copyArray cache.
    ///
    clang::Expr* m_UnresolvedCopyArray;

public:
    ///\ brief Constructs the return synthesizer.
    ///
    ///\param[in] I - The owning Interpreter instance.
    ///
    ValueExtractionSynthesizer(Interpreter& I);

    virtual ~ValueExtractionSynthesizer();

    Result Transform(clang::Decl* D) override;

  private:

    ///\brief
    /// Here we don't want to depend on the JIT runFunction, because of its
    /// limitations, when it comes to return value handling. There it is
    /// not clear who provides the storage and who cleans it up in a
    /// platform independent way.
    //
    /// Depending on the type we need to synthesize a call to cling:
    /// 0) void : do nothing;
    /// 1) enum, integral, float, double, referece, pointer types :
    ///      call to cling::internal::setValueNoAlloc(...);
    /// 2) object type (alloc on the stack) :
    ///      cling::internal::setValueWithAlloc
    ///   2.1) constant arrays:
    ///          call to cling::runtime::internal::copyArray(...)
    ///
    /// We need to synthesize later:
    /// Wrapper has signature: void w(cling::Value V)
    /// case 1):
    ///   setValueNoAlloc(gCling, &SVR, lastExprTy, lastExpr())
    /// case 2):
    ///   new (setValueWithAlloc(gCling, &SVR, lastExprTy)) (lastExpr)
    /// case 2.1):
    ///   copyArray(src, placement, N)
    ///
    clang::Expr* SynthesizeSVRInit(clang::Expr* E);

    // Find and cache cling::runtime::gCling, setValueNoAlloc,
    // setValueWithAlloc on first request.
    bool FindAndCacheRuntimeDecls();
  };

} // namespace cling

#endif // CLING_VALUE_EXTRACTION_SYNTHESIZER_H
