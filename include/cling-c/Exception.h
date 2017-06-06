//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Roman Zulak
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_CINTERACE_EXCEPTION_H
#define CLING_CINTERACE_EXCEPTION_H

#include "cling-c/config.h"

CLING_EXTERN_C_

///\brief a function that throws InvalidDerefException.
///
void* cling_ThrowIfInvalidPointer(void* Interp, void* Expr, const void* Arg);

_CLING_EXTERN_C

#endif // CLING_CINTERACE_EXCEPTION_H
