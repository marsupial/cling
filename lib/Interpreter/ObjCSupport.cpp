//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Jermaine Dupris <support@greyangle.com>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "ObjCSupport.h"
#include "cling/Interpreter/DynamicLibraryManager.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "IncrementalExecutor.h"

#ifdef CLING_OBJC_SUPPORT

#include <dlfcn.h>

using namespace cling;
using namespace cling::objectivec;

namespace {
  template <class T> static T dlsym( void *lib, const char* name ) {
    return (T) ::dlsym(lib, name);
  }

  // Exception safe?
  static void* dlOpen(std::vector<void*> &libs, const std::string &filePath,
                      int flags, DynamicLibraryManager* mgr) {
    const char* name = nullptr;
    if (mgr && (mgr->loadLibrary(filePath, true) >=
                 DynamicLibraryManager::kLoadLibNotFound)) {
      name = nullptr;
    } else if (!filePath.empty())
      name = filePath.c_str();
    // else
    // DynamicLibraryManager now holds a reference to the lib, so we'll
    // grab one to ourself and search for the symbol via that. Then we can
    // release all but one of the references after init.

    libs.push_back(nullptr);
    if (void *lib = libs.back() = ::dlopen(name, flags))
      return lib;

    libs.pop_back();
    return nullptr;
  }

  static void testSymbol(void* lib, const char* name, int flag, int& iLevel)
  {
    if (::dlsym(lib, name) != nullptr)
      iLevel = iLevel & ~(flag);
  }
}

#ifndef __APPLE__
static void* object_getClassGCC(const void *obj) {
  struct objc_obj {
    void* class_pointer;
  };
  return obj ? reinterpret_cast<const objc_obj*>(obj)->class_pointer : nullptr;
}
#endif

bool ObjCSupport::ObjCLink::init(void* lib) {
  msg.objc_msgSend = dlsym<CallWithVarAndReturnAPtr>(lib, "objc_msgSend");
  if (!msg.objc_msgSend) {
    // GCC libobjc doesn't have objc_msgSend, GNUstep does
    haveMsgSend = false;
    msg.objc_msg_lookup = dlsym<CallWith2AndReturnAPtr>(lib, "objc_msg_lookup");
    if (!msg.objc_msg_lookup)
      return false;
  } else
    haveMsgSend = true;

  if (!(sel_getUid = dlsym<CallWith1AndReturnAPtr>(lib, "sel_getUid")))
    return false;
  if (!(object_getClassName =
            dlsym<CallWith1AndReturnAPtr>(lib, "object_getClassName")))
    return false;
  if (!(class_getSuperclass =
            dlsym<CallWith1AndReturnAPtr>(lib, "class_getSuperclass")))
    return false;
   if (!(objc_lookUpClass =
            dlsym<CallWith1AndReturnAPtr>(lib, "objc_lookUpClass")))
    return false;
  if (!(class_getName = dlsym<CallWith1AndReturnAPtr>(lib, "class_getName")))
    return false;
  if (!(class_conformsToProtocol =
            dlsym<CallWith2AndReturnBOOL>(lib, "class_conformsToProtocol")))
    return false;

  // On linux these are depepndent on GNUStep or gcc runtime
  // On OS X they are required.

  if (CallWith1AndReturnAPtr objc_getProtocol =
          dlsym<CallWith1AndReturnAPtr>(lib, "objc_getProtocol")) {
    NSObjectProtocol = objc_getProtocol("NSObject");
  }

  if (!(object_getClass =
            dlsym<CallWith1AndReturnAPtr>(lib, "object_getClass"))) {
#ifdef __APPLE__
    return false;
#else
    object_getClass = object_getClassGCC;
#endif
  }

  return true;
}

bool ObjCSupport::init(int iLevel, DynamicLibraryManager* mgr,
                       std::string sysRoot) {

  // When given a DynamicLibraryManager, we let it hold onto the libraries in
  // case a user loads the same one. In that case dlOpen will call
  // DynamicLibraryManager::loadLibrary, we will then call dlopen(null)
  // and when dlsym is later called the list of loaded libraries are searched
  // for the symbol.  On exit of the function, we will then only keep a ref
  // to the first dlopen(null) call.

  // runtime seems to need CoreFoundation.__CFConstantStringClassReference
  // so if level > 1 we test for the symbol and load the library if not present

  const int openFlags = RTLD_LAZY | RTLD_GLOBAL;

  assert(iLevel >= 1);
  iLevel |= 0x01;


  if (void* lib = dlOpen(m_Libs, "", openFlags, nullptr)) {
    if (m_ObjCLink.init(lib)) {
      iLevel = iLevel & ~0x01;
#ifdef __APPLE__
      if (iLevel & 0x02)
        testSymbol(lib, "__CFConstantStringClassReference", 0x02, iLevel);
      if (iLevel & 0x04)
        testSymbol(lib, "NSLog", 0x04, iLevel);
#endif
    } else {
      ::dlclose(lib);
      m_Libs.pop_back();
    }
  }

#ifdef __APPLE__

  // None of this really matters it seems OS X injects libobjc.dylib into
  // every process, so we will have already succeeded above.

  if (iLevel & 0x01) {
    if (void* libobjc =
            dlOpen(m_Libs, "/usr/lib/libobjc.dylib", openFlags, mgr)) {
      if (m_ObjCLink.init(libobjc))
        iLevel = iLevel & ~0x01;
    }
  }

  // SDK framework stubs load properly, libs do not.
  sysRoot.append("/System/Library/Frameworks/");

  if (iLevel & 0x02) {
    if (dlOpen(m_Libs, sysRoot+"CoreFoundation.framework/CoreFoundation",
               openFlags, mgr))
      iLevel = iLevel & ~0x02;
  }

  if (iLevel & 0x04) {
    if (dlOpen(m_Libs, sysRoot+"Foundation.framework/Foundation", openFlags,
               mgr))
      iLevel = iLevel & ~0x04;
  }

#else // !__APPLE__

  if (iLevel & 0x01) {
    if (mgr) {
      // We know libobjc4 works, so that's the first attempt
      if (mgr->loadLibrary("objc.so.4", false) !=
          DynamicLibraryManager::kLoadLibSuccess) {
        if (mgr->loadLibrary("objc.so", false) !=
            DynamicLibraryManager::kLoadLibSuccess) {
          // ### FIXME This may be Ubuntu specific.
          // But /usr/lib/x86_64-linux-gnu is not reachable on Ubuntu 12
          // DynamicLibrayManger.cpp GetSystemLibraryPaths needs to handle that
          if (mgr->loadLibrary("/usr/lib/x86_64-linux-gnu/libobjc.so.4",
                               false) !=
              DynamicLibraryManager::kLoadLibSuccess) {
            return false;
          }
        }
      }
      if (void* libobjc = dlOpen(m_Libs, "", openFlags, mgr)) {
        if (m_ObjCLink.init(libobjc))
          iLevel = iLevel & ~0x01;
      }
    }
  }
  iLevel = iLevel & ~(0x02|0x04);

#endif

  // If we have a manager, release all of the dlopen(NULL) libraries, except for
  // the first which is where our ObjCLink methods have been loaded from

  if (mgr) {
    for (int i = 1, N = m_Libs.size(); i < N; ++i)
      ::dlclose(m_Libs[i]);
    m_Libs.resize(1);
  }

  return iLevel == 0;
}

void *ObjCSupport::getSelector(const char* name) {
  return m_ObjCLink.sel_getUid(name);
}
void *ObjCSupport::getClass(const char* name) {
  return m_ObjCLink.objc_lookUpClass(name);
}

void *ObjCSupport::perform(const void* obj, const char* name) {
  if (void *sel = getSelector(name)) {
    if (m_ObjCLink.haveMsgSend) {
      return m_ObjCLink.msg.objc_msgSend(obj, sel);
    } else {
      CallWithVarAndReturnAPtr msgProc =
        (CallWithVarAndReturnAPtr) m_ObjCLink.msg.objc_msg_lookup(obj, sel);
      if (msgProc)
        return msgProc(obj, sel);
    }
  }
  return nullptr;
}

const char *ObjCSupport::nsStringBytes(const void* strobj) {
  return static_cast<const char *>(perform(strobj, "UTF8String"));
}

std::string ObjCSupport::nsStringLiteral(const void* strobj) {
  const char *str = nsStringBytes(strobj);
  return std::string("@\"") + (str ? str : "") + std::string("\"");
}

std::string ObjCSupport::nxStringLiteral(const void* obj) {
  const char *cStr = static_cast<const char*>(perform(obj, "cString"));
  return std::string("@\"") + (cStr ? cStr : "") + std::string("\"");
}


ObjCSupport::ObjectType ObjCSupport::objType(const void* obj) {
  if (!obj || *((void*const*)obj) == IncrementalExecutor::getUnresolvedSymbol())
    return ObjC_UnkownType;

  if (void *oClass = m_ObjCLink.object_getClass(obj)) {

    // Quick test for NSObject protocol
    const char* cName = (const char *)m_ObjCLink.object_getClassName(obj);
    if (m_ObjCLink.NSObjectProtocol && m_ObjCLink.class_conformsToProtocol(
                                         oClass, m_ObjCLink.NSObjectProtocol)) {
      if (cName && strncmp(cName, "NSString", 8) == 0)
        return ObjC_NSStringType;

      return ObjC_NSObjectType;
    }
    // Don't know if this is constant across libobjc.dylib versions
    // But might be a quicker out for constant strings
    else if (cName && strncmp(cName, "__NSCFConstantString", 20) == 0)
      return ObjC_NSStringType;
#ifndef __APPLE__
    else if (cName && strncmp(cName, "NXConstantString", 16) == 0)
      return ObjC_NXConstantString;
#endif

    // Search the hierarchy
    while ((oClass = m_ObjCLink.class_getSuperclass(oClass))) {
      if ((cName = (const char *)m_ObjCLink.class_getName(oClass))) {
        if (strncmp(cName, "NSString", 8) == 0)
          return ObjC_NSStringType;
        if (strncmp(cName, "NSObject", 8) == 0)
          return ObjC_NSObjectType;
      }
    }
  }
  return ObjC_UnkownType;
}

std::string ObjCSupport::nsObjectDescription(const void* obj) {
  assert(objType(obj) >= ObjC_NSObjectType &&
         "Object must be of type NSObject");
  const void *const desc = perform(obj, "description");
  const char *dbytes = nsStringBytes(desc);
  return dbytes ? std::string(dbytes) : std::string();
  // is there a need to release returned string ?
  // rval.assign(dbytes);
  // perform(desc, "release");
  // return rval;
}

std::string ObjCSupport::description(const void* obj) {
  switch (objType(obj)) {
    case ObjCSupport::ObjC_NSObjectType:
      return nsObjectDescription(obj);
    case ObjCSupport::ObjC_NSStringType:
      return nsStringLiteral(obj);
    case ObjC_NXConstantString:
      return nxStringLiteral(obj);
    default:
      break;
  }
  return "";
}

ObjCSupport::ObjCSupport() {
  ::memset(&m_ObjCLink, 0, sizeof(m_ObjCLink));
}

ObjCSupport::~ObjCSupport() {
  for (void* lib : m_Libs)
    ::dlclose(lib);
}

ObjCSupport* ObjCSupport::create(const clang::CompilerInvocation& invocation,
                                 DynamicLibraryManager* mgr, int iLevel) {
  const clang::LangOptions *opts = invocation.getLangOpts();
  if (opts->ObjC2 || opts->ObjC1) {
    // We need support one instance for the entire process
    // Use static destruction on exit to clean up.
    static ObjCSupport sSupport;
    if (cling::objectivec::gInstance == &sSupport)
      return &sSupport;

    if (sSupport.init(iLevel, mgr, invocation.getHeaderSearchOpts().Sysroot)) {
      cling::objectivec::gInstance = &sSupport;
      return &sSupport;
    }
  }
  return nullptr;
}

ObjCSupport* ObjCSupport::instance() {
  return static_cast<ObjCSupport*>(cling::objectivec::gInstance);
}

#endif // CLING_OBJC_SUPPORT
