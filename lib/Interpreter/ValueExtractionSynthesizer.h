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
  class ASTContext;
  class Decl;
  class Expr;
  class Sema;
  class VarDecl;
}

namespace cling {
  class Interpreter;

  class ValueExtractionSynthesizer : public WrapperTransformer {
    ///\brief Owning Interpreter (may be a child of another Intepreter).
    ///
    Interpreter* m_Parent;

    ///\brief Cached reference to cling_ValueExtraction function.
    ///
    clang::Expr* m_ValueSynth;

public:
    ///\ brief Constructs the return synthesizer.
    ///
    ///\param[in] S - The semantic analysis object.
    ///\param[in] isChildInterpreter - flag to control if it is called
    /// from a child or parent Interpreter
    ///
    ValueExtractionSynthesizer(clang::Sema* S, Interpreter* I);

    virtual ~ValueExtractionSynthesizer();

    Result Transform(clang::Decl* D) override;

  private:

    ///\brief
    /// Transform the expression to allow for printing and/or transfer of
    /// ownership to the calling code.
    ///
    clang::Expr* SynthesizeSVRInit(clang::Expr* E);

    // Find and cache [cling::runtime::] gCling and cling_ValueExtraction,
    bool FindAndCacheRuntimeDecls(clang::Expr*);
  };

} // namespace cling

#endif // CLING_VALUE_EXTRACTION_SYNTHESIZER_H
