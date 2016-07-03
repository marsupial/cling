//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Axel Naumann <axel@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/Interpreter/CIFactory.h"
#include "ClingUtils.h"
#include "ObjCSupport.h"

#include "DeclCollector.h"
#include "cling-compiledata.h"

#include "clang/AST/ASTContext.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/Version.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Job.h"
#include "clang/Driver/Tool.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/VerifyDiagnosticConsumer.h"
#include "clang/Serialization/SerializationDiagnostic.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/FrontendTool/Utils.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "clang/Serialization/ASTReader.h"

#include "llvm/Config/llvm-config.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Process.h"

#ifdef CLING_CLANG_RUNTIME_PATCH
  #include "clang/Basic/cling.h"
#endif

#include <ctime>
#include <cstdio>

#include <memory>

#ifndef _MSC_VER
# include <unistd.h>
# define getcwd_func getcwd
#endif

// FIXME: This code has been taken (copied from) llvm/tools/clang/lib/Driver/WindowsToolChain.cpp
// and should probably go to some platform utils place.
// the code for VS 11.0 and 12.0 common tools (vs110comntools and vs120comntools)
// has been implemented (added) in getVisualStudioDir()
#ifdef _MSC_VER
// Include the necessary headers to interface with the Windows registry and
// environment.
# define WIN32_LEAN_AND_MEAN
# define NOGDI
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <Windows.h>
# include <direct.h>
# include <sstream>
# define popen _popen
# define pclose _pclose
# define getcwd_func _getcwd
# pragma comment(lib, "Advapi32.lib")

using namespace clang;

/// \brief Read registry string.
/// This also supports a means to look for high-versioned keys by use
/// of a $VERSION placeholder in the key path.
/// $VERSION in the key path is a placeholder for the version number,
/// causing the highest value path to be searched for and used.
/// I.e. "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\VisualStudio\\$VERSION".
/// There can be additional characters in the component.  Only the numberic
/// characters are compared.
static bool getSystemRegistryString(const char *keyPath, const char *valueName,
                                    char *value, size_t maxLength) {
  HKEY hRootKey = NULL;
  HKEY hKey = NULL;
  const char* subKey = NULL;
  DWORD valueType;
  DWORD valueSize = maxLength - 1;
  long lResult;
  bool returnValue = false;

  if (strncmp(keyPath, "HKEY_CLASSES_ROOT\\", 18) == 0) {
    hRootKey = HKEY_CLASSES_ROOT;
    subKey = keyPath + 18;
  } else if (strncmp(keyPath, "HKEY_USERS\\", 11) == 0) {
    hRootKey = HKEY_USERS;
    subKey = keyPath + 11;
  } else if (strncmp(keyPath, "HKEY_LOCAL_MACHINE\\", 19) == 0) {
    hRootKey = HKEY_LOCAL_MACHINE;
    subKey = keyPath + 19;
  } else if (strncmp(keyPath, "HKEY_CURRENT_USER\\", 18) == 0) {
    hRootKey = HKEY_CURRENT_USER;
    subKey = keyPath + 18;
  } else {
    return false;
  }

  const char *placeHolder = strstr(subKey, "$VERSION");
  char bestName[256];
  bestName[0] = '\0';
  // If we have a $VERSION placeholder, do the highest-version search.
  if (placeHolder) {
    const char *keyEnd = placeHolder - 1;
    const char *nextKey = placeHolder;
    // Find end of previous key.
    while ((keyEnd > subKey) && (*keyEnd != '\\'))
      keyEnd--;
    // Find end of key containing $VERSION.
    while (*nextKey && (*nextKey != '\\'))
      nextKey++;
    size_t partialKeyLength = keyEnd - subKey;
    char partialKey[256];
    if (partialKeyLength > sizeof(partialKey))
      partialKeyLength = sizeof(partialKey);
    strncpy(partialKey, subKey, partialKeyLength);
    partialKey[partialKeyLength] = '\0';
    HKEY hTopKey = NULL;
    lResult = RegOpenKeyEx(hRootKey, partialKey, 0, KEY_READ | KEY_WOW64_32KEY,
                           &hTopKey);
    if (lResult == ERROR_SUCCESS) {
      char keyName[256];
      int bestIndex = -1;
      double bestValue = 0.0;
      DWORD index, size = sizeof(keyName) - 1;
      for (index = 0; RegEnumKeyEx(hTopKey, index, keyName, &size, NULL,
          NULL, NULL, NULL) == ERROR_SUCCESS; index++) {
        const char *sp = keyName;
        while (*sp && !isDigit(*sp))
          sp++;
        if (!*sp)
          continue;
        const char *ep = sp + 1;
        while (*ep && (isDigit(*ep) || (*ep == '.')))
          ep++;
        char numBuf[32];
        strncpy(numBuf, sp, sizeof(numBuf) - 1);
        numBuf[sizeof(numBuf) - 1] = '\0';
        double dvalue = strtod(numBuf, NULL);
        if (dvalue > bestValue) {
          // Test that InstallDir is indeed there before keeping this index.
          // Open the chosen key path remainder.
          strcpy(bestName, keyName);
          // Append rest of key.
          strncat(bestName, nextKey, sizeof(bestName) - 1);
          bestName[sizeof(bestName) - 1] = '\0';
          lResult = RegOpenKeyEx(hTopKey, bestName, 0,
                                 KEY_READ | KEY_WOW64_32KEY, &hKey);
          if (lResult == ERROR_SUCCESS) {
            lResult = RegQueryValueEx(hKey, valueName, NULL, &valueType,
              (LPBYTE)value, &valueSize);
            if (lResult == ERROR_SUCCESS) {
              bestIndex = (int)index;
              bestValue = dvalue;
              returnValue = true;
            }
            RegCloseKey(hKey);
          }
        }
        size = sizeof(keyName) - 1;
      }
      RegCloseKey(hTopKey);
    }
  } else {
    lResult = RegOpenKeyEx(hRootKey, subKey, 0, KEY_READ | KEY_WOW64_32KEY,
                           &hKey);
    if (lResult == ERROR_SUCCESS) {
      lResult = RegQueryValueEx(hKey, valueName, NULL, &valueType,
        (LPBYTE)value, &valueSize);
      if (lResult == ERROR_SUCCESS)
        returnValue = true;
      RegCloseKey(hKey);
    }
  }
  return returnValue;
}

/// \brief Get Windows SDK installation directory.
static bool getWindowsSDKDir(std::string &path) {
  char windowsSDKInstallDir[256];
  // Try the Windows registry.
  bool hasSDKDir = getSystemRegistryString(
   "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Microsoft SDKs\\Windows\\$VERSION",
                                           "InstallationFolder",
                                           windowsSDKInstallDir,
                                           sizeof(windowsSDKInstallDir) - 1);
    // If we have both vc80 and vc90, pick version we were compiled with.
  if (hasSDKDir && windowsSDKInstallDir[0]) {
    path = windowsSDKInstallDir;
    return true;
  }
  return false;
}

  // Get Visual Studio installation directory.
static bool getVisualStudioDir(std::string &path) {
  // First check the environment variables that vsvars32.bat sets.
  const char* vcinstalldir = getenv("VCINSTALLDIR");
  if (vcinstalldir) {
    char *p = const_cast<char *>(strstr(vcinstalldir, "\\VC"));
    if (p)
      *p = '\0';
    path = vcinstalldir;
    return true;
  }
  int VSVersion = (_MSC_VER / 100) - 6;
  std::stringstream keyName;
  keyName << "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\VisualStudio\\" << VSVersion << ".0";
  char vsIDEInstallDir[256];
  char vsExpressIDEInstallDir[256];
  // Then try the windows registry.
  bool hasVCDir = getSystemRegistryString(keyName.str().c_str(),
    "InstallDir", vsIDEInstallDir, sizeof(vsIDEInstallDir) - 1);
  keyName.str(std::string());
  keyName << "HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\VCExpress\\" << VSVersion << ".0";
  bool hasVCExpressDir = getSystemRegistryString(keyName.str().c_str(),
    "InstallDir", vsExpressIDEInstallDir, sizeof(vsExpressIDEInstallDir) - 1);
    // If we have both vc80 and vc90, pick version we were compiled with.
  if (hasVCDir && vsIDEInstallDir[0]) {
    char *p = (char*)strstr(vsIDEInstallDir, "\\Common7\\IDE");
    if (p)
      *p = '\0';
    path = vsIDEInstallDir;
    return true;
  }

  if (hasVCExpressDir && vsExpressIDEInstallDir[0]) {
    char *p = (char*)strstr(vsExpressIDEInstallDir, "\\Common7\\IDE");
    if (p)
      *p = '\0';
    path = vsExpressIDEInstallDir;
    return true;
  }

  // Try the environment.
  const char *vs140comntools = getenv("VS140COMNTOOLS");
  const char *vs120comntools = getenv("VS120COMNTOOLS");
  const char *vs110comntools = getenv("VS110COMNTOOLS");
  const char *vs100comntools = getenv("VS100COMNTOOLS");
  const char *vs90comntools = getenv("VS90COMNTOOLS");
  const char *vs80comntools = getenv("VS80COMNTOOLS");
  const char *vscomntools = NULL;

  // Try to find the version that we were compiled with
  if(false) {}
  #if (_MSC_VER >= 1900)  // VC140
  else if (vs140comntools) {
	  vscomntools = vs140comntools;
  }
  #elif (_MSC_VER >= 1800)  // VC120
  else if(vs120comntools) {
    vscomntools = vs120comntools;
  }
  #elif (_MSC_VER >= 1700)  // VC110
  else if(vs110comntools) {
    vscomntools = vs110comntools;
  }
  #elif (_MSC_VER >= 1600)  // VC100
  else if(vs100comntools) {
    vscomntools = vs100comntools;
  }
  #elif (_MSC_VER == 1500) // VC80
  else if(vs90comntools) {
    vscomntools = vs90comntools;
  }
  #elif (_MSC_VER == 1400) // VC80
  else if(vs80comntools) {
    vscomntools = vs80comntools;
  }
  #endif
  // Otherwise find any version we can
  else if (vs140comntools)
	  vscomntools = vs140comntools;
  else if (vs120comntools)
    vscomntools = vs120comntools;
  else if (vs110comntools)
    vscomntools = vs110comntools;
  else if (vs100comntools)
    vscomntools = vs100comntools;
  else if (vs90comntools)
    vscomntools = vs90comntools;
  else if (vs80comntools)
    vscomntools = vs80comntools;

  if (vscomntools && *vscomntools) {
    const char *p = strstr(vscomntools, "\\Common7\\Tools");
    path = p ? std::string(vscomntools, p) : vscomntools;
    return true;
  }
  return false;
}

#elif defined(__APPLE__)

#include <dlfcn.h> // dlopen to avoid linking with CoreServices
#include <CoreServices/CoreServices.h>
#include <sstream>

static bool getISysRootVersion(const std::string& SDKs, int major,
                               int minor, std::string& sysRoot) {
  std::ostringstream os;
  os << SDKs << "MacOSX" << major << "." << minor << ".sdk";

  std::string SDKv = os.str();
  if (llvm::sys::fs::is_directory(SDKv)) {
    sysRoot.swap(SDKv);
    return true;
  }

  return false;
}

static int getXCodeRoot(std::string& xcodeDir) {

  // Some versions of OS X and Server have headers installed
  int result = llvm::sys::fs::is_regular_file("/usr/include/stdlib.h");

  std::string SDKs("/Applications/Xcode.app/Contents/Developer");

  // Is XCode installed where it usually is?
  if (!llvm::sys::fs::is_directory(SDKs)) {
    // Nope, use xcode-select -p to get the path
    if (FILE *pf = ::popen("xcode-select -p", "r")) {
      SDKs.clear();
      char buffer[512];
      while (fgets(buffer, sizeof(buffer), pf) && buffer[0])
        SDKs.append(buffer);

      // remove trailing \n
      while (!SDKs.empty() && SDKs.back() == '\n')
        SDKs.resize(SDKs.size() - 1);
      ::pclose(pf);
    } else // Nothing more we can do
      return result;
  }

  xcodeDir = SDKs;
  return 2;
}

static bool getISysRoot(std::string& sysRoot, std::string& xcodeDir) {
  using namespace llvm::sys;

  if (getXCodeRoot(xcodeDir) < 2)
    return false;

  std::string SDKs = xcodeDir + "/Platforms/MacOSX.platform/Developer/SDKs/";
  if (!fs::is_directory(SDKs))
    return false;

  // Seems to make more sense to get the currently running SDK so any loaded
  // libraries won't casue conflicts

  // Try to get the SDK for whatever version of OS X is currently running
  if (void *core = dlopen(
          "/System/Library/Frameworks/CoreServices.framework/CoreServices",
          RTLD_LAZY)) {
    SInt32 majorVersion = -1, minorVersion = -1;
    typedef ::OSErr (*GestaltProc)(::OSType, ::SInt32 *);
    if (GestaltProc Gestalt = (GestaltProc)dlsym(core, "Gestalt")) {
      Gestalt(gestaltSystemVersionMajor, &majorVersion);
      Gestalt(gestaltSystemVersionMinor, &minorVersion);
    }
    ::dlclose(core);

    if (majorVersion != -1 && minorVersion != -1) {
      if (getISysRootVersion(SDKs, majorVersion, minorVersion, sysRoot))
        return true;
    }
  }

#define GET_ISYSROOT_VER(maj, min) \
  if (getISysRootVersion(SDKs, maj, min, sysRoot)) \
    return true;

  // Try to get the SDK for whatever cling was compiled with
  #if defined(MAC_OS_X_VERSION_10_11)
    GET_ISYSROOT_VER(10, 11);
  #elif defined(MAC_OS_X_VERSION_10_10)
    GET_ISYSROOT_VER(10, 10);
  #elif defined(MAC_OS_X_VERSION_10_9)
    GET_ISYSROOT_VER(10, 9);
  #elif defined(MAC_OS_X_VERSION_10_8)
    GET_ISYSROOT_VER(10, 8);
  #elif defined(MAC_OS_X_VERSION_10_7)
    GET_ISYSROOT_VER(10, 7);
  #elif defined(MAC_OS_X_VERSION_10_6)
    GET_ISYSROOT_VER(10, 6);
  #elif defined(MAC_OS_X_VERSION_10_5)
    GET_ISYSROOT_VER(10, 5);
  #elif defined(MAC_OS_X_VERSION_10_4)
    GET_ISYSROOT_VER(10, 4);
  #elif defined(MAC_OS_X_VERSION_10_3)
    GET_ISYSROOT_VER(10, 3);
  #elif defined(MAC_OS_X_VERSION_10_2)
    GET_ISYSROOT_VER(10, 2);
  #elif defined(MAC_OS_X_VERSION_10_1)
    GET_ISYSROOT_VER(10, 1);
  #else // MAC_OS_X_VERSION_10_0
    GET_ISYSROOT_VER(10, 0);
  #endif

#undef GET_ISYSROOT_VER

  // Scan the SDKs directory and use the latest
  std::error_code ec;
  std::vector<std::string> srtd;
  for (fs::directory_iterator it(SDKs, ec), e; it != e; it.increment(ec))
    srtd.push_back(it->path());
  if (!srtd.empty()) {
    std::sort(srtd.begin(), srtd.end(), std::greater<std::string>());
    sysRoot.swap(srtd[0]);
    return true;
  }

  return false;
}

// This is a bit fragile and depenends on Apple's libc++ version being lower
// than than a version compiled with cling (Apple XCode 8 is at 3700)
#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION < 3800
  #define CLING_APPLE_LIBCPP 1
#endif

#ifdef CLING_APPLE_LIBCPP

  static bool getCXXHeaders(const std::string& root, std::string& cxxHeaders) {

    // Xcode < 6, c++ headers stored in <toolchain>/lib/c++/v1
  #if __apple_build_version__ < 6000000
    llvm::Twine tmp5(root, "/lib/c++/v1");
    if (llvm::sys::fs::is_directory(tmp5)) {
      cxxHeaders = tmp5.str();
      return true;
    }
  #endif

    llvm::Twine tmp(root, "/include/c++/v1");
    if (llvm::sys::fs::is_directory(tmp)) {
      cxxHeaders = tmp.str();
      return true;
    }

    return false;
  }

#endif // CLING_APPLE_LIBCPP

#endif // __APPLE__, _MSC_VER

using namespace clang;

namespace {
  //
  //  Dummy function so we can use dladdr to find the executable path.
  //
  void locate_cling_executable()
  {
  }

  struct CompilerOpts {
    enum {
      kHasMinusX      = 1,
      kHasNoBultinInc = 2,
      kHasNoCXXInc    = 4,
      kHasResourceDir = 8,
      kHasSysRoot     = 16,
      kHasOuptut      = 32,
      kHasCXXVersion  = 64,
      kHasCXXLibrary  = 128,
      
      kAllFlags = kHasMinusX|kHasNoBultinInc|kHasNoCXXInc|
                  kHasResourceDir|kHasSysRoot|kHasOuptut|kHasCXXVersion|
                  kHasCXXLibrary,
    };
    unsigned m_Flags;

    static bool strEqual(const char* a, const char* b, size_t n) {
      return !strncmp(a, b, n) && !a[n];
    }

    void parse(const char* arg) {
      if (!hasMinusX() && !strncmp(arg, "-x", 2))
        m_Flags |= kHasMinusX;
      else if (!hasCXXVersion() && !hasCXXLib() && !strncmp(arg, "-std",4)) {
        arg += 4;
        if (!hasCXXVersion() && !strncmp(arg, "=c++", 4))
          m_Flags |= kHasCXXVersion;
        else if (!hasCXXLib() && !strncmp(arg, "lib=", 4))
          m_Flags |= kHasCXXLibrary;
      }
      else if (!hasCXXVersion() && !strncmp(arg, "-std=c++", 8))
        m_Flags |= kHasCXXVersion;
      else if (!noBuiltinInc() && strEqual(arg, "-nobuiltininc", 13))
        m_Flags |= kHasNoBultinInc;
      else if (!noCXXIncludes() && strEqual(arg, "-nostdinc++", 11))
        m_Flags |= kHasNoCXXInc;
      else if (!hasRsrcPath() && strEqual(arg, "-resource-dir", 13))
        m_Flags |= kHasResourceDir;
      else if (!hasOutput() && strEqual(arg, "-o", 2))
        m_Flags |= kHasOuptut;
#ifdef __APPLE__
      else if (!hasSysRoot() && strEqual(arg, "-isysroot", 9))
        m_Flags |= kHasSysRoot;
#endif
    }

  public:
    bool hasMinusX() const { return m_Flags & kHasMinusX; }
    bool hasRsrcPath() const { return m_Flags & kHasResourceDir; }
    bool hasSysRoot() const { return m_Flags & kHasSysRoot; }
    bool hasOutput() const { return m_Flags & kHasOuptut; }
    bool hasCXXVersion() const { return m_Flags & kHasCXXVersion; }
    bool noBuiltinInc() const { return m_Flags & kHasNoBultinInc; }
    bool noCXXIncludes() const { return m_Flags & kHasNoCXXInc; }
    bool hasCXXLib() const { return m_Flags & kHasCXXLibrary; }

    CompilerOpts(const char* const* iarg, const char* const* earg)
      : m_Flags(0) {

      while (iarg < earg && (m_Flags != kAllFlags)) {
        if (strEqual(*iarg, "-Xclang", 7)) {
          // goto next arg if there is one
          if (++iarg < earg)
            parse(*iarg);
        }
        else
          parse(*iarg);

        ++iarg;
      }
    }
  };

  class AdditionalArguments {
    typedef std::vector< std::pair<const char*,std::string> > container_t;
    container_t m_Saved;

  public:
    
    void addArgument(const char* arg, std::string value) {
      m_Saved.push_back(std::make_pair(arg,std::move(value)));
    }
    container_t::const_iterator begin() const { return m_Saved.begin(); }
    container_t::const_iterator end() const { return m_Saved.end(); }
    bool empty() const { return m_Saved.empty(); }
  };
  
  ///\brief Adds standard library -I used by whatever compiler is found in PATH.
  static void AddHostArguments(std::vector<const char*>& args,
                               const char* llvmdir, const CompilerOpts& opts) {
    static AdditionalArguments sArguments;
    if (sArguments.empty()) {
#ifdef _MSC_VER
      // Honor %INCLUDE%. It should know essential search paths with vcvarsall.bat.
      if (const char *cl_include_dir = getenv("INCLUDE")) {
        SmallVector<StringRef, 8> Dirs;
        StringRef(cl_include_dir).split(Dirs, ";");
        for (SmallVectorImpl<StringRef>::iterator I = Dirs.begin(), E = Dirs.end();
             I != E; ++I) {
          StringRef d = *I;
          if (d.size() == 0)
            continue;
          sArguments.addArgument("-I", d);
        }
      }
      std::string VSDir;
      std::string WindowsSDKDir;

      // When built with access to the proper Windows APIs, try to actually find
      // the correct include paths first.
      if (getVisualStudioDir(VSDir)) {
        if (!opts.noCXXIncludes()) {
          sArguments.addArgument("-I", VSDir + "\\VC\\include");
        }
        if (!opts.noBuiltinInc()) {
          if (getWindowsSDKDir(WindowsSDKDir)) {
            sArguments.addArgument("-I", WindowsSDKDir + "\\include");
          }
          else {
            sArguments.addArgument("-I", VSDir + "\\VC\\PlatformSDK\\Include");
          }
        }
      }

#else

      bool noCXXIncludes = opts.noCXXIncludes();

#ifdef __APPLE__

      std::string toolchain;
      if (!opts.noBuiltinInc() && !opts.hasSysRoot()) {
        std::string sysRoot;
        if (getISysRoot(sysRoot, toolchain))
          sArguments.addArgument("-isysroot", std::move(sysRoot));
      } else if (!noCXXIncludes && !getXCodeRoot(toolchain))
        noCXXIncludes = false;

#ifdef CLING_APPLE_LIBCPP
      if (!noCXXIncludes) {
        // XCode may be installed, or CommandLineTools only, or neither
        toolchain.append(!toolchain.empty() ?
                        "/Toolchains/XcodeDefault.xctoolchain/usr" :
                        "/Library/Developer/CommandLineTools/usr");

        // If we can't determine anything, fallback to launching clang
        if (!llvm::sys::fs::is_directory(toolchain))
          toolchain.clear();

        if (!toolchain.empty()) {
          std::string cxxHeaders;
          noCXXIncludes = getCXXHeaders(toolchain, cxxHeaders);
          if (noCXXIncludes)
            sArguments.addArgument("-I", std::move(cxxHeaders));
        }
      }
#endif // CLING_APPLE_LIBCPP

#endif // __APPLE__

      // Skip LLVM_CXX execution if -nostdinc++ was provided.
      if (!noCXXIncludes) {
        // Try to use a version of clang that is located next to cling
        SmallString<2048> buffer;
        std::string clang = llvm::sys::fs::getMainExecutable("cling",
                                         (void*)(intptr_t) locate_cling_executable
                                                            );
        clang = llvm::sys::path::parent_path(clang);
        buffer.assign(clang);
        llvm::sys::path::append(buffer, "clang");
        clang.assign(&buffer[0], buffer.size());

        std::string CppInclQuery("echo | LC_ALL=C ");
        if (llvm::sys::fs::is_regular_file(clang)) {
          CppInclQuery.append(clang);
          if (!opts.hasCXXLib()) {
#ifdef _LIBCPP_VERSION
            CppInclQuery.append(" -stdlib=libc++");
#else
            CppInclQuery.append(" -stdlib=libstdc++");
#endif
          }
        } else
          CppInclQuery.append(LLVM_CXX);

        CppInclQuery.append(" -xc++ -E -v /dev/null 2>&1 "
          "| awk '/^#include </,/^End of search"
          "/{if (!/^#include </ && !/^End of search/){ print }}' "
          "| GREP_OPTIONS= grep -E \"(c|g)\\+\\+\"");

        if (FILE *pf = ::popen(CppInclQuery.c_str(), "r")) {

          while (fgets(&buffer[0], buffer.capacity_in_bytes(), pf) && buffer[0]) {
            size_t lenbuf = strlen(&buffer[0]);
            buffer.data()[lenbuf - 1] = 0;   // remove trailing \n
            // Skip leading whitespace:
            const char* start = &buffer[0];
            while (start < (&buffer[0] + lenbuf) && *start == ' ')
              ++start;
            if (*start) {
              sArguments.addArgument("-I", start);
            }
          }
          ::pclose(pf);
        }
        else
          ::perror("popen failure");

        if (sArguments.empty()) {
          llvm::errs() <<
            "ERROR in cling::CIFactory::createCI(): cannot extract"
            "  standard library include paths!\nInvoking:\n"
            "    " << CppInclQuery << "\nresults in\n";
          int ExitCode = system(CppInclQuery.c_str());
          llvm::errs() << "with exit code " << ExitCode << "\n";
        }

      }

#endif // !_MSC_VER

      if (!opts.hasRsrcPath() && !opts.noBuiltinInc()) {
        std::string resourcePath;
        if (!llvmdir) {
          // FIXME: The first arg really does need to be argv[0] on FreeBSD.
          //
          // Note: The second arg is not used for Apple, FreeBSD, Linux,
          //       or cygwin, and can only be used on systems which support
          //       the use of dladdr().
          //
          // Note: On linux and cygwin this uses /proc/self/exe to find the path
          // Note: On Apple it uses _NSGetExecutablePath().
          // Note: On FreeBSD it uses getprogpath().
          // Note: Otherwise it uses dladdr().
          //
          resourcePath
            = CompilerInvocation::GetResourcesPath("cling",
                                       (void*)(intptr_t) locate_cling_executable);
        } else {
          llvm::SmallString<512> tmp(llvmdir);
          llvm::sys::path::append(tmp, "lib", "clang", CLANG_VERSION_STRING);
          resourcePath.assign(&tmp[0], tmp.size());
        }

        // FIXME: Handle cases, where the cling is part of a library/framework.
        // There we can't rely on the find executable logic.
        if (!llvm::sys::fs::is_directory(resourcePath)) {
          llvm::errs()
            << "ERROR in cling::CIFactory::createCI():\n  resource directory "
            << resourcePath << " not found!\n";
          resourcePath = "";
        } else {
          sArguments.addArgument("-resource-dir", std::move(resourcePath));
        }
      }
    }

    for (auto& arg : sArguments) {
      args.push_back(arg.first);
      args.push_back(arg.second.c_str());
    }
  }

  static void SetClingCustomLangOpts(LangOptions& Opts,
                                     const CompilerOpts* UserOpts) {
    Opts.EmitAllDecls = 0; // Otherwise if PCH attached will codegen all decls.
#ifdef _MSC_VER
    Opts.Exceptions = 0;
    if (Opts.CPlusPlus) {
      Opts.CXXExceptions = 0;
    }
#else
    Opts.Exceptions = 1;
    if (Opts.CPlusPlus) {
      Opts.CXXExceptions = 1;
    }
#endif // _MSC_VER
    Opts.Deprecated = 1;
    //Opts.Modules = 1;

    // See test/CodeUnloading/PCH/VTables.cpp which implicitly compares clang
    // to cling lang options. They should be the same, we should not have to
    // give extra lang options to their invocations on any platform.
    // Except -fexceptions -fcxx-exceptions.

    Opts.Deprecated = 1;
    Opts.GNUKeywords = 0;
    Opts.Trigraphs = 1; // o no??! but clang has it on by default...

#ifdef __APPLE__
    Opts.Blocks = 1;
    Opts.MathErrno = 0;
#endif

    // C++11 is turned on if cling is built with C++11: it's an interpreter;
    // cross-language compilation doesn't make sense.
    // Extracted from Boost/config/compiler.
    // SunProCC has no C++11.
    // VisualC's support is not obvious to extract from Boost...

    // The value of __cplusplus in GCC < 5.0 (e.g. 4.9.3) when
    // either -std=c++1y or -std=c++14 is specified is 201300L, which fails
    // the test for C++14 or more (201402L) as previously specified.
    // I would claim that the check should be relaxed to:

    // Only set this if C++ is the language.
    // User can override, but there will be an ABI warning waiting for them.
    if (Opts.CPlusPlus && (!UserOpts || !UserOpts->hasCXXVersion())) {
#if __cplusplus > 201103L
      if (Opts.CPlusPlus) Opts.CPlusPlus14 = 1;
#endif
#if __cplusplus >= 201103L
      if (Opts.CPlusPlus) Opts.CPlusPlus11 = 1;
#endif
    }

#ifdef _REENTRANT
    Opts.POSIXThreads = 1;
#endif
  }

  static void SetClingTargetLangOpts(LangOptions& Opts,
                                     const TargetInfo& Target) {
    if (Target.getTriple().getOS() == llvm::Triple::Win32) {
      Opts.MicrosoftExt = 1;
#ifdef _MSC_VER
      Opts.MSCompatibilityVersion = (_MSC_VER * 100000);
#endif
      // Should fix http://llvm.org/bugs/show_bug.cgi?id=10528
      Opts.DelayedTemplateParsing = 1;
    } else {
      Opts.MicrosoftExt = 0;
    }
  }

  // This must be a copy of clang::getClangToolFullVersion(). Luckily
  // we'll notice quickly if it ever changes! :-)
  static std::string CopyOfClanggetClangToolFullVersion(StringRef ToolName) {
    std::string buf;
    llvm::raw_string_ostream OS(buf);
#ifdef CLANG_VENDOR
    OS << CLANG_VENDOR;
#endif
    OS << ToolName << " version " CLANG_VERSION_STRING " "
       << getClangFullRepositoryVersion();

    // If vendor supplied, include the base LLVM version as well.
#ifdef CLANG_VENDOR
    OS << " (based on LLVM " << PACKAGE_VERSION << ")";
#endif

    return OS.str();
  }

  ///\brief Check the compile-time clang version vs the run-time clang version,
  /// a mismatch could cause havoc. Reports if clang versions differ.
  static void CheckClangCompatibility() {

    #ifdef CLING_CLANG_RUNTIME_PATCH
      // Setup the clang libraries to use changes neccessary for clang
      cling::setClientFlags(cling::kClingIsHost);
    #endif

    if (clang::getClangToolFullVersion("cling")
        != CopyOfClanggetClangToolFullVersion("cling"))
      llvm::errs()
        << "Warning in cling::CIFactory::createCI():\n  "
        "Using incompatible clang library! "
        "Please use the one provided by cling!\n";
    return;
  }

  /// \brief Retrieves the clang CC1 specific flags out of the compilation's
  /// jobs. Returns NULL on error.
  static const llvm::opt::ArgStringList*
  GetCC1Arguments(clang::DiagnosticsEngine *Diagnostics,
                  clang::driver::Compilation *Compilation) {
    // We expect to get back exactly one Command job, if we didn't something
    // failed. Extract that job from the Compilation.
    const clang::driver::JobList &Jobs = Compilation->getJobs();
    if (!Jobs.size() || !isa<clang::driver::Command>(*Jobs.begin())) {
      // diagnose this better...
      llvm::errs() << "No Command jobs were built.\n";
      return nullptr;
    }

    // The one job we find should be to invoke clang again.
    const clang::driver::Command *Cmd
      = cast<clang::driver::Command>(&(*Jobs.begin()));
    if (llvm::StringRef(Cmd->getCreator().getName()) != "clang") {
      // diagnose this better...
      llvm::errs() << "Clang wasn't the first job.\n";
      return nullptr;
    }

    return &Cmd->getArguments();
  }

  /// Set cling's preprocessor defines to match the cling binary.
  static void SetPreprocessorFromBinary(PreprocessorOptions& PPOpts) {
#ifdef _MSC_VER
    PPOpts.addMacroDef("_HAS_EXCEPTIONS=0");
#ifdef _DEBUG
    PPOpts.addMacroDef("_DEBUG=1");
#elif defined(NDEBUG)
    PPOpts.addMacroDef("NDEBUG=1");
#else // well, what else?
    PPOpts.addMacroDef("NDEBUG=1");
#endif
#endif

    // Since cling, uses clang instead, macros always sees __CLANG__ defined
    // In addition, clang also defined __GNUC__, we add the following two macros
    // to allow scripts, and more important, dictionary generation to know which
    // of the two is the underlying compiler.

#ifdef __clang__
  #if defined(__apple_build_version__)
     #if __apple_build_version__ >= 7000000
      #define __CLING__clang__ 370
     #elif __apple_build_version__ >= 6020037
      #define __CLING__clang__ 360
     #elif __apple_build_version__ >= 6000051
      #define __CLING__clang__ 350
     #elif __apple_build_version__ >= 5030038
      #define __CLING__clang__ 340
     #elif __apple_build_version__ >= 5000275
      #define __CLING__clang__ 330
     #elif __apple_build_version__ >= 4250024
      #define __CLING__clang__ 320
     #elif __apple_build_version__ >= 3180045
      #define __CLING__clang__ 310
     #elif __apple_build_version__ >= 2111001
      #define __CLING__clang__ 300
     #endif
  #endif
  #ifndef __CLING__clang__
    #define __CLING__clang__ (((__clang_major__*10) + __clang_minor__) * 10) \
                                                          + __clang_patchlevel__
  #endif
    PPOpts.addMacroDef("__CLING__clang__=" ClingStringify(__CLING__clang__));
  #undef __CLING__clang__
#elif defined(__GNUC__)
    PPOpts.addMacroDef("__CLING__GNUC__=" ClingStringify(__GNUC__));
    PPOpts.addMacroDef("__CLING__GNUC_MINOR__=" ClingStringify(__GNUC_MINOR__));
#endif

// https://gcc.gnu.org/onlinedocs/libstdc++/manual/using_dual_abi.html
#ifdef _GLIBCXX_USE_CXX11_ABI
    PPOpts.addMacroDef("_GLIBCXX_USE_CXX11_ABI="
                       ClingStringify(_GLIBCXX_USE_CXX11_ABI));
#endif
  }

  /// Set target-specific preprocessor defines.
  static void SetPreprocessorFromTarget(PreprocessorOptions& PPOpts,
                                        const llvm::Triple& TTriple) {
    if (TTriple.getEnvironment() == llvm::Triple::Cygnus) {
      // clang "forgets" the basic arch part needed by winnt.h:
      if (TTriple.getArch() == llvm::Triple::x86) {
        PPOpts.addMacroDef("_X86_=1");
      } else if (TTriple.getArch() == llvm::Triple::x86_64) {
        PPOpts.addMacroDef("__x86_64=1");
      } else {
        llvm::errs() << "Warning in cling::CIFactory::createCI():\n"
          "unhandled target architecture "
        << TTriple.getArchName() << '\n';
      }
    }
  }

  template <class CONTAINER>
  static void insertBehind(CONTAINER& To, const CONTAINER& From) {
    To.insert(To.end(), From.begin(), From.end());
  }

  static llvm::IntrusiveRefCntPtr<DiagnosticsEngine>
  setupDiagnostics(DiagnosticOptions& DiagOpts) {
    // The compiler invocation is the owner of the diagnostic options.
    // Everything else points to them.
    llvm::IntrusiveRefCntPtr<clang::DiagnosticIDs> DiagIDs(new DiagnosticIDs());
    if (!DiagIDs)
      return nullptr;

    std::unique_ptr<TextDiagnosticPrinter>
      DiagnosticPrinter(new TextDiagnosticPrinter(llvm::errs(), &DiagOpts));
    if (!DiagnosticPrinter)
      return nullptr;

    llvm::IntrusiveRefCntPtr<DiagnosticsEngine>
      Diags(new DiagnosticsEngine(DiagIDs, &DiagOpts,
                                  DiagnosticPrinter.get(), /*Owns it*/ true));
    if (Diags)
      DiagnosticPrinter.release();

    return Diags;
  }

  static bool setupCompiler(CompilerInstance* CI, const CompilerOpts *UserOpt) {
    // Set the language options, which cling needs.
    // This may have already been done via a precompiled header
    if (UserOpt)
      SetClingCustomLangOpts(CI->getLangOpts(), UserOpt);

    PreprocessorOptions& PPOpts = CI->getInvocation().getPreprocessorOpts();
    SetPreprocessorFromBinary(PPOpts);

    PPOpts.addMacroDef("__CLING__");
    if (CI->getLangOpts().CPlusPlus11 == 1) {
      // http://llvm.org/bugs/show_bug.cgi?id=13530
      PPOpts.addMacroDef("__CLING__CXX11");
    }

    if (CI->getDiagnostics().hasErrorOccurred()) {
      llvm::errs() << "Compiler error to early in initialization.\n";
      return false;
    }

    CI->setTarget(TargetInfo::CreateTargetInfo(CI->getDiagnostics(),
                                               CI->getInvocation().TargetOpts));
    if (!CI->hasTarget()) {
      llvm::errs() << "Could not determine compiler target.\n";
      return false;
    }

    CI->getTarget().adjust(CI->getLangOpts());

    // This may have already been done via a precompiled header
    if (UserOpt)
      SetClingTargetLangOpts(CI->getLangOpts(), CI->getTarget());

    SetPreprocessorFromTarget(PPOpts, CI->getTarget().getTriple());
    return true;
  }

  class ActionScan {
    std::set<const clang::driver::Action*> m_Visited;
    const clang::driver::Action::ActionClass m_Kind;

    bool find (const clang::driver::Action* A) {
      if (A && !m_Visited.count(A)) {
        if (A->getKind() == m_Kind)
          return true;

        m_Visited.insert(A);
        return find(*A->input_begin());
      }
      return false;
    }

  public:
    ActionScan(clang::driver::Action::ActionClass k) : m_Kind(k) {}

    bool find (clang::driver::Compilation* C) {
      for (clang::driver::Action* A : C->getActions()) {
        if (find(A))
          return true;
      }
      return false;
    }
  };
  
  static std::pair<CompilerInstance*,bool>
  createCIImpl(std::unique_ptr<llvm::MemoryBuffer> buffer,
               int argc,
               const char* const *argv,
               const char* llvmdir,
               bool OnlyLex) {
    // Create an instance builder, passing the llvmdir and arguments.
    //

    CheckClangCompatibility();

    //  Initialize the llvm library.
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmParser();
    llvm::InitializeNativeTargetAsmPrinter();

    CompilerOpts copts(argv, argv + argc);

    std::vector<const char*> argvCompile(argv, argv+1);
    argvCompile.reserve(argc+5);
    if (!copts.hasMinusX()) {
      // We do C++ by default; append right after argv[0] if no "-x" given
      argvCompile.push_back("-x");
      argvCompile.push_back( "c++");
    }
    // argv[0] already inserted, get the rest
    argvCompile.insert(argvCompile.end(), argv+1, argv + argc);

    if (!copts.hasCXXLib()) {
#ifdef _LIBCPP_VERSION
      argvCompile.push_back("-stdlib=libc++");
#else
      argvCompile.push_back("-stdlib=libstdc++");
#endif
    }

    // Add host specific includes, -resource-dir if necessary, and -isysroot
    AddHostArguments(argvCompile, llvmdir, copts);

    if (!copts.hasOutput() && !OnlyLex) {
      argvCompile.push_back("-c");
      argvCompile.push_back("-");
    }

    std::unique_ptr<clang::CompilerInvocation>
      InvocationPtr(new clang::CompilerInvocation);
    if (!InvocationPtr)
      return std::make_pair(nullptr,false);

    using llvm::IntrusiveRefCntPtr;
    // The compiler invocation is the owner of the diagnostic options.
    // Everything else points to them.
    DiagnosticOptions& DiagOpts = InvocationPtr->getDiagnosticOpts();
    IntrusiveRefCntPtr<DiagnosticsEngine> Diags = setupDiagnostics(DiagOpts);
    if (!Diags) {
      // If we can't even setup the diagnostic engine, lets not use llvm::errs
      ::perror("Could not setup diagnostic engine");
      return std::make_pair(nullptr,false);
    }

    clang::driver::Driver Driver(argv[0], llvm::sys::getDefaultTargetTriple(),
                                 *Diags);
    //Driver.setWarnMissingInput(false);
    Driver.setCheckInputsExist(false); // think foo.C(12)
    llvm::ArrayRef<const char*>RF(&(argvCompile[0]), argvCompile.size());
    std::unique_ptr<clang::driver::Compilation>
      Compilation(Driver.BuildCompilation(RF));
    if (!Compilation) {
      ::perror("Couldn't create clang::driver::Compilation");
      return std::make_pair(nullptr,false);
    }

    const driver::ArgStringList* CC1Args = GetCC1Arguments(Diags.get(),
                                                           Compilation.get());
    if (!CC1Args)
      return std::make_pair(nullptr,false);

    clang::CompilerInvocation::CreateFromArgs(*InvocationPtr, CC1Args->data() + 1,
                                              CC1Args->data() + CC1Args->size(),
                                              *Diags);
    // We appreciate the error message about an unknown flag (or do we? if not
    // we should switch to a different DiagEngine for parsing the flags).
    // But in general we'll happily go on.
    Diags->Reset();
    // This used to be set to true, but what was it accomplishing?
    // The lifetime of the invocation is tied to the Interpreter's lifetime
    // so we want the memory freed in ~Interpreter so as not to leak it.
    InvocationPtr->getFrontendOpts().DisableFree = false;

    // Create and setup a compiler instance.
    std::unique_ptr<CompilerInstance> CI(new CompilerInstance());
    if (CI) {
      CI->setInvocation(InvocationPtr.get());
      InvocationPtr.release();
      CI->setDiagnostics(Diags.get()); // Diags is ref-counted
    }
    else {
      ::perror("Could not allocate CompilerInstance");
      return std::make_pair(nullptr,false);
    }

    if (copts.hasOutput() && !OnlyLex) {
      ActionScan scan(clang::driver::Action::PrecompileJobClass);
      if (!scan.find(Compilation.get())) {
        llvm::errs() << "Only output to precompiled header is supported.\n";
        return std::make_pair(nullptr,false);
      }
      if (!setupCompiler(CI.get(), &copts))
        return std::make_pair(nullptr,false);

      ProcessWarningOptions(*Diags, DiagOpts);
      ExecuteCompilerInvocation(CI.get());
      return std::make_pair(CI.release(), false);
    }

    CI->createFileManager();

    std::string& PCHFileName
      = CI->getInvocation().getPreprocessorOpts().ImplicitPCHInclude;
    if (!PCHFileName.empty()) {
      assert(!Diags->hasErrorOccurred() && !Diags->hasFatalErrorOccurred() &&
             "Error has already occured");
      if (llvm::sys::fs::is_regular_file(PCHFileName)) {
        // Load target options etc from PCH.
        struct PCHListener: public ASTReaderListener {
          CompilerInvocation& m_Invocation;
          DiagnosticsEngine& m_Diags;

          PCHListener(CompilerInvocation& I, DiagnosticsEngine& D)
            : m_Invocation(I), m_Diags(D) {}

          bool ReadLanguageOptions(const LangOptions &LangOpts,
                                   bool /*Complain*/,
                                 bool /*AllowCompatibleDifferences*/) override {
            *m_Invocation.getLangOpts() = LangOpts;
            
            #ifdef CLING_OBJC_SUPPORT
              // We'd like to do this later to share the loaded libraries with
              // a DynamicLibraryManager instance, but I'm not entirely sure
              // no selectors can be emited via the PCH.
              cling::objectivec::ObjCSupport::create(m_Invocation);
            #endif
            return false;
          }
          bool ReadTargetOptions(const TargetOptions &TargetOpts,
                                 bool /*Complain*/,
                                 bool /*AllowCompatibleDifferences*/) override {
            m_Invocation.getTargetOpts() = TargetOpts;
            return false;
          }
          bool ReadPreprocessorOptions(const PreprocessorOptions &PPOpts,
                                       bool /*Complain*/,
                                std::string &/*SuggestedPredefines*/) override {
            // Import selected options, e.g. don't overwrite ImplicitPCHInclude.
            PreprocessorOptions& myPP = m_Invocation.getPreprocessorOpts();
            insertBehind(myPP.Macros, PPOpts.Macros);
            insertBehind(myPP.Includes, PPOpts.Includes);
            insertBehind(myPP.MacroIncludes, PPOpts.MacroIncludes);
            return false;
          }
          bool ReadFullVersionInformation(StringRef FullVersion) override {
            if (ASTReaderListener::ReadFullVersionInformation(FullVersion)) {
              m_Diags.Report(diag::err_pch_different_branch)
                << FullVersion << getClangFullRepositoryVersion();
              return true;
            }
            return false;
          }
        };
        PCHListener listener(CI->getInvocation(), *Diags);
        if (ASTReader::readASTFileControlBlock(PCHFileName,
                                           CI->getFileManager(),
                                           CI->getPCHContainerReader(),
                                           false /*FindModuleFileExtensions*/,
                                           listener)) {
          // Failed!
          // Some errors will have been already issued, others are silent
          // PCHFileName is curently ignored with err_fe_unable_to_load_pch
          if (!Diags->hasErrorOccurred() && !Diags->hasFatalErrorOccurred())
            Diags->Report(diag::err_fe_unable_to_load_pch) << PCHFileName;
        }
      } else
        Diags->Report(clang::diag::err_drv_no_such_file) << PCHFileName;

      // Follow convention where missing or bad file shows an error, but
      // execution will continue.
      if (Diags->hasErrorOccurred() && !Diags->hasFatalErrorOccurred()) {
        Diags->Reset(true);
        // SetClingCustomLangOpts and SetClingTargetLangOpts must still be
        // called below and we don't want anyone to try to load later
        std::string().swap(PCHFileName);
      }
    }

    // Copied from CompilerInstance::createDiagnostics:
    // Chain in -verify checker, if requested.
    if (DiagOpts.VerifyDiagnostics)
      Diags->setClient(new clang::VerifyDiagnosticConsumer(*Diags));
    // Configure our handling of diagnostics.
    ProcessWarningOptions(*Diags, DiagOpts);

    // Set up compiler language and target
    if (!setupCompiler(CI.get(), PCHFileName.empty() ? &copts : nullptr))
      return std::make_pair(nullptr,false);

    // Set up source managers
    SourceManager* SM = new SourceManager(CI->getDiagnostics(),
                                          CI->getFileManager(),
                                          /*UserFilesAreVolatile*/ true);
    CI->setSourceManager(SM); // CI now owns SM

    // As main file we want
    // * a virtual file that is claiming to be huge
    // * with an empty memory buffer attached (to bring the content)
    FileManager& FM = SM->getFileManager();

    // When asking for the input file below (which does not have a directory
    // name), clang will call $PWD "." which is terrible if we ever change
    // directories (see ROOT-7114). By asking for $PWD (and not ".") it will
    // be registered as $PWD instead, which is stable even after chdirs.
    char cwdbuf[2048];
    if (!getcwd_func(cwdbuf, sizeof(cwdbuf))) {
      // getcwd can fail, but that shouldn't mean we have to.
      ::perror("Could not get current working directory");
    } else
      FM.getDirectory(cwdbuf);

    // Build the virtual file, Give it a name that's likely not to ever
    // be #included (so we won't get a clash in clangs cache).
    const char* Filename = "<<< cling interactive line includer >>>";
    const FileEntry* FE = FM.getVirtualFile(Filename, 1U << 15U, time(0));

    // Tell ASTReader to create a FileID even if this file does not exist:
    SM->setFileIsTransient(FE);
    FileID MainFileID = SM->createFileID(FE, SourceLocation(), SrcMgr::C_User);
    SM->setMainFileID(MainFileID);
    const SrcMgr::SLocEntry& MainFileSLocE = SM->getSLocEntry(MainFileID);
    const SrcMgr::ContentCache* MainFileCC
      = MainFileSLocE.getFile().getContentCache();
    if (!buffer)
      buffer = llvm::MemoryBuffer::getMemBuffer("/*CLING DEFAULT MEMBUF*/\n");
    const_cast<SrcMgr::ContentCache*>(MainFileCC)->setBuffer(std::move(buffer));

    // Set up the preprocessor
    CI->createPreprocessor(TU_Complete);
    Preprocessor& PP = CI->getPreprocessor();
    PP.getBuiltinInfo().initializeBuiltins(PP.getIdentifierTable(),
                                           PP.getLangOpts());

    // Set up the ASTContext
    CI->createASTContext();

    if (OnlyLex) {
      class IgnoreConsumer: public clang::ASTConsumer {
      };
      std::unique_ptr<clang::ASTConsumer> ignoreConsumer(new IgnoreConsumer());
      CI->setASTConsumer(std::move(ignoreConsumer));
    } else {
      std::unique_ptr<cling::DeclCollector>
        stateCollector(new cling::DeclCollector());

      // Set up the ASTConsumers
      CI->getASTContext().setASTMutationListener(stateCollector.get());
      // Add the callback keeping track of the macro definitions
      PP.addPPCallbacks(stateCollector->MakePPAdapter());
      CI->setASTConsumer(std::move(stateCollector));
    }

    // Set up Sema
    CodeCompleteConsumer* CCC = 0;
    CI->createSema(TU_Complete, CCC);

    // Set CodeGen options
    // want debug info
    //CI->getCodeGenOpts().setDebugInfo(clang::CodeGenOptions::FullDebugInfo);
    // CI->getCodeGenOpts().EmitDeclMetadata = 1; // For unloading, for later
    CI->getCodeGenOpts().CXXCtorDtorAliases = 0; // aliasing the complete
                                                 // ctor to the base ctor causes
                                                 // the JIT to crash
    CI->getCodeGenOpts().VerifyModule = 0; // takes too long

    // Passes over the ownership to the caller.
    return std::make_pair(CI.release(), true);
  }

} // unnamed namespace

namespace cling {
  std::pair<CompilerInstance*, bool>
  CIFactory::createCI(llvm::StringRef code, int argc, const char* const *argv,
                      const char* llvmdir) {
    return createCIImpl(llvm::MemoryBuffer::getMemBuffer(code), argc, argv,
                        llvmdir, false /*OnlyLex*/);
  }

  CompilerInstance* CIFactory::createCI(MemBufPtr_t buffer,
                                        int argc,
                                        const char* const *argv,
                                        const char* llvmdir,
                                        bool OnlyLex) {
    return createCIImpl(std::move(buffer), argc, argv, llvmdir, OnlyLex).first;
  }

} // end namespace
