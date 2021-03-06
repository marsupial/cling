//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Jermaine Dupris <support@greyangle.com>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_OBJECTIVE_C_SUPPORT_H
#define CLING_OBJECTIVE_C_SUPPORT_H

#ifdef CLING_OBJC_SUPPORT

#include "clang/Basic/cling.h"
#include <vector>
#include <string>

namespace clang {
  class CompilerInvocation;
}

namespace cling {
  class Interpreter;
  class DynamicLibraryManager;

  namespace objectivec {

    class ObjCSupport : public IObjCLookup {
      #ifndef __OBJC__
        typedef signed char BOOL;
      #endif
      #ifndef __APPLE__
        static void* objc_msgSendNoArgs(const void* id, const void* sel, ...);
      #endif

      typedef void* (*CFStringCreateWithCStringP)(const void*, const char*, int);
      typedef void* (*CallWith1AndReturnAPtr)(const void*);
      typedef void* (*CallWithVarAndReturnAPtr)(const void*,const void*,...);
      typedef BOOL  (*CallWith2AndReturnBOOL)(const void*, const void*);
      typedef void* (*CallWith2AndReturnAPtr)(const void*, const void*);

      struct ObjCLink {
        union {
          CallWithVarAndReturnAPtr objc_msgSend;
          CallWith2AndReturnAPtr objc_msg_lookup;
        } msg;

        CallWith1AndReturnAPtr sel_getUid, object_getClass, object_getClassName,
                               class_getSuperclass, class_getName,
                               objc_lookUpClass;
        CallWith2AndReturnBOOL class_conformsToProtocol;
        void *NSObjectProtocol;
        bool haveMsgSend;

        bool init(void* lib);
      } m_ObjCLink;

      std::vector<void*>         m_Libs;

      // Only Interpreter can create an instance
      friend class cling::Interpreter;

      ObjCSupport();
      ~ObjCSupport();

      bool init(int level, DynamicLibraryManager *mgr, std::string sysRoot);
      void* perform(const void* obj, const char* sel);

      enum ObjectType {
        ObjC_UnkownType,
        ObjC_NSObjectType,
        ObjC_NSStringType,
        ObjC_NXConstantString,
      };
      ObjectType  objType(const void *obj);
      const char* nsStringBytes(const void* strobj);
      std::string nsStringLiteral(const void* strobj);
      std::string nsObjectDescription(const void* obj);
      std::string nxStringLiteral(const void* strobj);

    public:
      static ObjCSupport* instance();
      static ObjCSupport* create(const clang::CompilerInvocation&,
                                 cling::DynamicLibraryManager* = nullptr,
                                 int initLevel = 2);

      void* getSelector(const char* name) override;
      void* getClass(const char* name) override;
      std::string description(const void* obj);
    };

  } // namespace objectivec
} // namespace cling

#endif // CLING_OBJC_SUPPORT

#endif // CLING_OBJECTIVE_C_SUPPORT_H
