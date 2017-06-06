//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vasil.georgiev.vasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_CINTERACE_VALUEPRINTER_H
#define CLING_CINTERACE_VALUEPRINTER_H

#include "cling-c/config.h"

CLING_EXTERN_C_

///\brief C language printing function.
///
void cling_PrintValue(void* /*cling::Value**/ V);

_CLING_EXTERN_C

#endif // CLING_CINTERACE_VALUEPRINTER_H
