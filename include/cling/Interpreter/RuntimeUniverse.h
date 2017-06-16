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

#ifdef __cplusplus
extern "C" {
#endif

///\brief Set the type of a void expression evaluated at the prompt.
///\param [out] vpSVR - The Value that is created.
///\param [in] vpQT - The opaque ptr for the clang::QualType of value.
///\param [in] vpI - The cling::Interpreter for Value.
///\param [in] ID - Operation, assign, dump, or return value.
///
void* cling_ValueExtraction(void* vpSVR, void* vpQT, void *vpI, int ID, ...);

///\brief a function that throws InvalidDerefException. This allows to 'hide'
/// the definition of the exceptions from the RuntimeUniverse and allows us to
/// run cling in -no-rtti mode.
///
void* cling_ThrowIfInvalidPointer(void* Sema, void* Expr, const void* Arg);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CLING_RUNTIME_UNIVERSE_H
