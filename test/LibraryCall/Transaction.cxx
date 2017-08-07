//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "Transaction.h"

#ifndef CLING_REGB

#include <stddef.h>

#ifdef _WIN32
#define strdup _strdup
#endif

extern "C" int printf(const char*, ...);
extern "C" char* strdup(const char*);
extern "C" void* realloc(void*, size_t);
extern "C" void free(void*);

static struct Registry {
  char** Names;
  unsigned Num;
  Registry() : Names(0), Num(0) {}
  ~Registry() {
    if (!Names) return;
    for (unsigned I = 0; I < Num; ++I) {
      printf("Unreg.%d: %s\n", I, Names[I]);
      free(Names[I]);
    }
    free(Names);
  }
  void operator() (const char* N) {
    printf("Reg.%d: %s\n", ++Num, N);
    Names = (char**) realloc(Names, Num * sizeof(const char*));
    Names[Num-1] = strdup(N);
  }
} sRegistry;

extern "C" void CLING_EXPORT RegisterPlugin(const char* Name) {
  sRegistry(Name);
}

#else

extern "C" void CLING_EXPORT RegisterPluginB(void (*RegisterPlugin)(const char*)) {
  RegisterPlugin("B");
}

#endif




