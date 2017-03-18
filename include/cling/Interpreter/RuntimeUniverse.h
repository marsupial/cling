//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vasil.georgiev.vasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------
#ifndef CLING_RUNTIME_UNIVERSE_H
#define CLING_RUNTIME_UNIVERSE_H

#if !defined(__CLING__)
#error "This file must not be included by compiled programs."
#endif

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS // needed by System/DataTypes.h
#endif

#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS // needed by System/DataTypes.h
#endif

#ifndef __cplusplus

extern void* gCling;

#else

#include <new>

namespace cling {

  class Interpreter;

  /// \brief Used to stores the declarations, which are going to be
  /// available only at runtime. These are cling runtime builtins
  namespace runtime {

    /// \brief The interpreter provides itself as a builtin, i.e. it
    /// interprets itself. This is particularly important for implementing
    /// the dynamic scopes and the runtime bindings
    extern Interpreter* const gCling;

    namespace internal {
      ///\brief Set the type of a void expression evaluated at the prompt.
      ///\param [in] vpI - The cling::Interpreter for Value.
      ///\param [in] vpQT - The opaque ptr for the clang::QualType of value.
      ///\param [in] vpT - The opaque ptr for the cling::Transaction.
      ///\param [out] vpSVR - The Value that is created.
      ///
      void setValueNoAlloc(void* vpI, void* vpSVR, void* vpQT, char vpOn);

      ///\brief Set the value of the GenericValue for the expression
      /// evaluated at the prompt.
      ///\param [in] vpI - The cling::Interpreter for Value.
      ///\param [in] vpQT - The opaque ptr for the clang::QualType of value.
      ///\param [in] value - The float value of the assignment to be stored
      ///                    in GenericValue.
      ///\param [in] vpT - The opaque ptr for the cling::Transaction.
      ///\param [out] vpSVR - The Value that is created.
      ///
      void setValueNoAlloc(void* vpI, void* vpV, void* vpQT, char vpOn,
                           float value);

      ///\brief Set the value of the GenericValue for the expression
      /// evaluated at the prompt.
      ///\param [in] vpI - The cling::Interpreter for Value.
      ///\param [in] vpQT - The opaque ptr for the clang::QualType of value.
      ///\param [in] value - The double value of the assignment to be stored
      ///                    in GenericValue.
      ///\param [in] vpT - The opaque ptr for the cling::Transaction.
      ///\param [out] vpSVR - The Value that is created.
      ///
      void setValueNoAlloc(void* vpI, void* vpV, void* vpQT, char vpOn,
                           double value);

      ///\brief Set the value of the GenericValue for the expression
      ///   evaluated at the prompt. Extract through
      ///   APFloat(ASTContext::getFloatTypeSemantics(QT), const APInt &)
      ///\param [in] vpI - The cling::Interpreter for Value.
      ///\param [in] vpQT - The opaque ptr for the clang::QualType of value.
      ///\param [in] value - The value of the assignment to be stored
      ///                    in GenericValue.
      ///\param [in] vpT - The opaque ptr for the cling::Transaction.
      ///\param [out] vpSVR - The Value that is created.
      ///
      void setValueNoAlloc(void* vpI, void* vpV, void* vpQT, char vpOn,
                           long double value);

      ///\brief Set the value of the GenericValue for the expression
      /// evaluated at the prompt.
      /// We are using unsigned long long instead of uint64, because we don't
      /// want to #include the header.
      ///\param [in] vpI - The cling::Interpreter for Value.
      ///\param [in] vpQT - The opaque ptr for the clang::QualType of value.
      ///\param [in] value - The uint64_t value of the assignment to be stored
      ///                    in GenericValue.
      ///\param [in] vpT - The opaque ptr for the cling::Transaction.
      ///\param [out] vpSVR - The Value that is created.
      ///
      void setValueNoAlloc(void* vpI, void* vpV, void* vpQT, char vpOn,
                           unsigned long long value);

      ///\brief Set the value of the GenericValue for the expression
      /// evaluated at the prompt.
      ///\param [in] vpI - The cling::Interpreter for Value.
      ///\param [in] vpQT - The opaque ptr for the clang::QualType of value.
      ///\param [in] value - The void* value of the assignment to be stored
      ///                    in GenericValue.
      ///\param [in] vpT - The opaque ptr for the cling::Transaction.
      ///\param [out] vpV - The Value that is created.
      ///
      void setValueNoAlloc(void* vpI, void* vpV, void* vpQT, char vpOn,
                           const void* value);

      ///\brief Set the value of the Generic value and return the address
      /// for the allocated storage space.
      ///\param [in] vpI - The cling::Interpreter for Value.
      ///\param [in] vpQT - The opaque ptr for the clang::QualType of value.
      ///\param [in] vpT - The opaque ptr for the cling::Transaction.
      ///\param [out] vpV - The Value that is created.
      ///
      ///\returns the address where the value should be put.
      ///
      void* setValueWithAlloc(void* vpI, void* vpV, void* vpQT, char vpOn);
    } // end namespace internal
  } // end namespace runtime
} // end namespace cling

using namespace cling::runtime;

extern "C" {

#endif // __cplusplus

// From "cling-c/Exception.h"

///\brief a function that throws InvalidDerefException. This allows to 'hide'
/// the definition of the exceptions from the RuntimeUniverse and allows us to
/// run cling in -no-rtti mode.
///

void* cling_ThrowIfInvalidPointer(void* Sema, void* Expr, const void* Arg);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // CLING_RUNTIME_UNIVERSE_H
