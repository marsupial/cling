//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vasil.georgiev.vasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/Interpreter/DynamicLibraryManager.h"
#include "cling/Interpreter/InterpreterCallbacks.h"
#include "cling/Interpreter/InvocationOptions.h"
#include "cling/Utils/Paths.h"
#include "cling/Utils/Platform.h"
#include "cling/Utils/Output.h"

#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

#include <system_error>
#include <sys/stat.h>

namespace llvm {
  template<>
  struct DenseMapInfo<std::string>  {
    static inline std::string getEmptyKey() { return std::string(); }
    static inline std::string getTombstoneKey() { return std::string(1, 0); }
    static unsigned getHashValue(const std::string& Val) {
      return hash_value(Val);
    }
    static bool isEqual(const std::string& LHS, const std::string& RHS) {
      return LHS == RHS;
    }
  };
}

namespace cling {
  DynamicLibraryManager::DynamicLibraryManager(const InvocationOptions& Opts)
    : m_Opts(Opts), m_Callbacks(0) {
    const llvm::SmallVector<const char*, 10> kSysLibraryEnv = {
      "LD_LIBRARY_PATH",
  #if __APPLE__
      "DYLD_LIBRARY_PATH",
      "DYLD_FALLBACK_LIBRARY_PATH",
      /*
      "DYLD_VERSIONED_LIBRARY_PATH",
      "DYLD_FRAMEWORK_PATH",
      "DYLD_FALLBACK_FRAMEWORK_PATH",
      "DYLD_VERSIONED_FRAMEWORK_PATH",
      */
  #elif defined(LLVM_ON_WIN32)
      "PATH",
  #endif
    };

    // Behaviour is to not add paths that don't exist...In an interpreted env
    // does this make sense? Path could pop into existance at any time.
    for (const char* Var : kSysLibraryEnv) {
      if (Opts.Verbose())
        cling::log() << "Adding library paths from '" << Var << "':\n";
      if (const char* Env = ::getenv(Var)) {
        llvm::SmallVector<llvm::StringRef, 10> CurPaths;
        SplitPaths(Env, CurPaths, utils::kPruneNonExistant, platform::kEnvDelim,
                   Opts.Verbose());
        for (const auto& Path : CurPaths)
          m_SystemSearchPaths.push_back(Path.str());
      }
    }

    platform::GetSystemLibraryPaths(m_SystemSearchPaths);

    // This will currently be the last path searched, should it be pushed to
    // the front of the line, or even to the front of user paths?
    m_SystemSearchPaths.push_back(".");
  }

  DynamicLibraryManager::~DynamicLibraryManager() {
    if (m_DyLibs) {
      std::string Err;
      for (DyLibs::const_reverse_iterator Itr = m_DyLibs->rbegin(),
           End = m_DyLibs->rend(); Itr < End; ++Itr) {
        platform::DLClose(Itr->second.getPointer(), &Err);
        if (!Err.empty()) {
          llvm::errs() << "DynamicLibraryManager::~DynamicLibraryManager(): "
                       << Err << '\n';
          Err.clear();
        }
      }
    }
  }

  bool DynamicLibraryManager::isSharedLib(llvm::StringRef LibName,
                                          bool* exists /*= nullptr*/) {
    using namespace llvm::sys::fs;
    file_magic Magic;
    const std::error_code Error = identify_magic(LibName, Magic);
    if (exists)
      *exists = !Error;

    return !Error &&
#ifdef __APPLE__
      (Magic == file_magic::macho_fixed_virtual_memory_shared_lib
       || Magic == file_magic::macho_dynamically_linked_shared_lib
       || Magic == file_magic::macho_dynamically_linked_shared_lib_stub
       || Magic == file_magic::macho_universal_binary)
#elif defined(LLVM_ON_UNIX)
#ifdef __CYGWIN__
      (Magic == file_magic::pecoff_executable)
#else
      (Magic == file_magic::elf_shared_object)
#endif
#elif defined(LLVM_ON_WIN32)
      (Magic == file_magic::pecoff_executable || platform::IsDLL(LibName.str()))
#else
# error "Unsupported platform."
#endif
      ;
  }

  static std::string lookInPaths(llvm::StringRef libStem,
                                 const std::vector<std::string>& Paths,
                                 bool &exists) {
    llvm::SmallString<1024> ThisPath;
    for (const auto& Path : Paths) {
      ThisPath.assign(Path);
      llvm::sys::path::append(ThisPath, libStem);
      const std::string lib = ThisPath.str();
      if (isSharedLib(lib, &exists))
        return lib;
      if (exists)
        break;
    }
    return std::string();
  }

  std::string
  DynamicLibraryManager::lookupLibInPaths(llvm::StringRef libStem) const {
    bool exists = false;
    const std::string Path = lookInPaths(libStem, m_Opts.LibSearchPath, exists);
    if (!Path.empty() || exists)
      return Path;

    return lookInPaths(libStem, m_SystemSearchPaths, exists);
  }

  std::string
  DynamicLibraryManager::lookupLibMaybeAddExt(llvm::StringRef libStem) const {
    using namespace llvm::sys;

    std::string foundDyLib = lookupLibInPaths(libStem);

    if (foundDyLib.empty()) {
      // Add DyLib extension:
      llvm::SmallString<512> filenameWithExt(libStem);
      llvm::SmallString<512>::iterator IStemEnd = filenameWithExt.end() - 1;

      const char* kLibraryExtenstions[] = {
#if defined(LLVM_ON_UNIX)
 #if defined(__APPLE__)
        ".dylib",
 #endif
        ".so",
#elif defined(LLVM_ON_WIN32)
        ".dll",
#else
 #error "Unknown library extension."
#endif
        nullptr,
      };

      for (unsigned i = 0; ;
           filenameWithExt.erase(IStemEnd + 1, filenameWithExt.end())) {
        filenameWithExt += kLibraryExtenstions[i];
        foundDyLib = lookupLibInPaths(filenameWithExt);
        if (!foundDyLib.empty())
          break;
        if (!kLibraryExtenstions[++i])
          break;
      }
    }

    if (foundDyLib.empty())
      return std::string();

    // get canonical path name and check if already loaded
    const std::string Path = platform::NormalizePath(foundDyLib);
    if (Path.empty()) {
      cling::errs() << "cling::DynamicLibraryManager::lookupLibMaybeAddExt(): "
        "error getting real (canonical) path of library " << foundDyLib << '\n';
      return foundDyLib;
    }
    return Path;
  }

  std::string DynamicLibraryManager::normalizePath(llvm::StringRef path) {
    // Make the path canonical if the file exists.
    const std::string Path = path.str();
    struct stat buffer;
    if (::stat(Path.c_str(), &buffer) != 0)
      return std::string();

    const std::string NPath = platform::NormalizePath(Path);
    if (NPath.empty())
      cling::log() << "Could not normalize: '" << Path << "'";
    return NPath;
  }

  FileEntry
  DynamicLibraryManager::lookupLibrary(FileEntry libStem) const {
    if (libStem.resolved())
      return libStem;

    // If it is an absolute path, don't try iterate over the paths.
    const std::string &libName = libStem.mNameOrPath;
    if (llvm::sys::path::is_absolute(libName)) {
      if (isSharedLib(libName))
        return FileEntry(std::move(libStem.mNameOrPath), FileEntry::kLibrarySet);
      else
        return FileEntry(std::move(libStem.mNameOrPath), FileEntry::kResolved);
    }

    std::string foundName = lookupLibMaybeAddExt(libName);
    if (foundName.empty() && libName.find("lib")!=0) {
      // try with "lib" prefix:
      foundName = lookupLibMaybeAddExt("lib" + libName);
    }

    if (isSharedLib(foundName))
      return FileEntry(normalizePath(foundName), FileEntry::kLibrarySet);
    return FileEntry(std::move(libStem.mNameOrPath), FileEntry::kResolved);
  }

  DynamicLibraryManager::LoadLibResult
  DynamicLibraryManager::loadLibrary(FileEntry libStem, bool permanent ) {
    FileEntry file = lookupLibrary(std::move(libStem));
    if (!file.isLibrary())
      return kLoadLibNotFound;

    const std::string &canonicalLib = file.filePath();
    if (!m_DyLibs)
      m_DyLibs.reset(new DyLibs);
    else if (m_DyLibs->count(canonicalLib))
      return kLoadLibAlreadyLoaded;

    std::string errMsg;
    DyLibHandle dyLibHandle = platform::DLOpen(canonicalLib, &errMsg);
    if (!dyLibHandle) {
      cling::errs() << "cling::DynamicLibraryManager::loadLibrary(): " << errMsg
                    << '\n';
      return kLoadLibLoadError;
    }

    if (!m_DyLibs->insert(std::make_pair(canonicalLib,
                                  DyLibValue(dyLibHandle, permanent))).second) {
      // Perhaps another thread beat us?
      platform::DLClose(dyLibHandle);
      return kLoadLibAlreadyLoaded;
    }

    if (InterpreterCallbacks* C = getCallbacks())
      C->LibraryLoaded(dyLibHandle, canonicalLib);

    return kLoadLibSuccess;
  }

  void DynamicLibraryManager::unloadLibrary(FileEntry libStem) {
    if (!m_DyLibs)
      return;

    FileEntry file = lookupLibrary(std::move(libStem));
    const std::string &canonicalLoadedLib = file.filePath();
    DyLibs::iterator Itr = m_DyLibs->find(canonicalLoadedLib);
    if (Itr == m_DyLibs->end())
      return;

    DyLibValue& loadedLib = Itr->second;
    if (loadedLib.getInt()) {
      llvm::errs() << "'" << canonicalLoadedLib << "' was loaded permanently.";
      return;
    }

    std::string errMsg;
    DyLibHandle dyLibHandle = loadedLib.getPointer();
    platform::DLClose(dyLibHandle, &errMsg);
    if (!errMsg.empty()) {
      cling::errs() << "cling::DynamicLibraryManager::unloadLibrary(): "
                    << errMsg << '\n';
    }

    if (InterpreterCallbacks* C = getCallbacks())
      C->LibraryUnloaded(dyLibHandle, canonicalLoadedLib);

    m_DyLibs->erase(Itr);
    if (m_DyLibs->empty())
      m_DyLibs.reset();
  }

  bool DynamicLibraryManager::isLibraryLoaded( const FileEntry &file ) const {
    assert(file.resolved() && "isLibraryLoaded requires a resolved path");
    return m_DyLibs && m_DyLibs->count(file.filePath());
  }

  void DynamicLibraryManager::ExposeHiddenSharedLibrarySymbols(void* handle) {
    llvm::sys::DynamicLibrary::addPermanentLibrary(const_cast<void*>(handle));
  }
} // end namespace cling
