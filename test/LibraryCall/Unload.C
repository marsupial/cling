/*------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//----------------------------------------------------------------------------*/

// RUN: true
// Used as library source by LibUnload.c

typedef void (*DtorCallback)(void*);
#define CLING_JOIN(Name, Tok)  CLING_JOIN_(Name, Tok)
#define CLING_JOIN_(Name, Tok) Name##Tok

extern "C" void CLING_JOIN(setInterpreter, CLING_UNLOAD) (DtorCallback C, void* I);

#if !defined(CLTEST_LIBS)
  extern "C" int printf(const char*,...);
  #define ClingStringifyx(s) #s
  #define ClingStringify(s) ClingStringifyx(s)

  struct CLING_JOIN(Unloaded, CLING_UNLOAD) {
    DtorCallback m_Callback;
    void* m_Data;
    CLING_JOIN(Unloaded,CLING_UNLOAD) () : m_Callback(0), m_Data(0) {}
    CLING_JOIN(~Unloaded,CLING_UNLOAD) () {
      printf("Unloaded::~Unloaded %s\n", ClingStringify(CLING_UNLOAD));
      if (m_Callback)
        m_Callback(m_Data);
    }
  };

  static CLING_JOIN(Unloaded, CLING_UNLOAD) CLING_JOIN(sUnloaded, CLING_UNLOAD);

  void CLING_JOIN(setInterpreter, CLING_UNLOAD) (DtorCallback C, void* I) {
    CLING_JOIN(sUnloaded, CLING_UNLOAD).m_Callback = C;
    CLING_JOIN(sUnloaded, CLING_UNLOAD).m_Data = I;
  }
#endif
