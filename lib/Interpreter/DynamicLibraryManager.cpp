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
namespace {

  class FileSearch {
    enum BestMatch {
      kNoMatch,       // Nothing matched
      kMatchNotRight, // Matched something, but it wasn't right filetype
      kMatchName,     // Matched by name
      kMatchEdited    // Matched by name after editing the name
    };
    typedef llvm::SmallVectorImpl<llvm::StringRef> const * NameEdits;

    llvm::SmallString<1024> m_Path;
    NameEdits m_Prefixes;
    NameEdits m_Extensions;
    std::string m_FilePath;
    BestMatch m_BestMatch;
    const bool m_Verbose;

    bool testFile(std::string FilePath) {
      if (m_Verbose)
        cling::errs() << "Looking for library: '" << FilePath << "'\n";
      bool Exists;
      if (DynamicLibraryManager::isSharedLib(FilePath, &Exists)) {
        m_FilePath.swap(FilePath);
        m_BestMatch = kMatchName;
        if (m_Verbose)
          cling::errs() << "Found library: '" << m_FilePath << "'\n";
        return true;
      }
      if (Exists && !m_BestMatch) {
        if (m_Verbose)
          cling::errs() << "Ignoring '" << FilePath << "', it is not a library\n";
        m_BestMatch = kMatchNotRight;
      }
      return false;
    }

    BestMatch lookForFileExt(llvm::StringRef Name, llvm::StringRef Path) {
      m_Path.assign(Path);
      llvm::sys::path::append(m_Path, Name);
      if (testFile(m_Path.str()))
        return kMatchName;

      if (m_Extensions && m_BestMatch < kMatchName) {
        for (const auto& Ext : *m_Extensions) {
          m_Path.resize(Path.size());
          llvm::sys::path::append(m_Path, Name+Ext);
          if (testFile(m_Path.str()))
            return kMatchEdited;
        }
      }
      return kNoMatch;
    }

    BestMatch lookForFile(llvm::StringRef Name, llvm::StringRef Path) {
      // If the directory doesn't exist, neither will any file!
      if (!llvm::sys::fs::is_directory(Path)) {
        if (m_Verbose)
          utils::LogNonExistantDirectory(Path);
        return kNoMatch;
      }

      // FileName or FileName.ext takes precedence over libFileName.ext
      const BestMatch Match = lookForFileExt(Name, Path);
      if (Match > kMatchNotRight)
        return Match;

      if (m_BestMatch < kMatchName) {
        if (m_Prefixes) {
          const std::string NameStr = Name.str();
          for (const auto& Prefix : *m_Prefixes) {
            if (lookForFileExt(Prefix.str()+NameStr, Path))
              return kMatchEdited;
          }
        }
      }
      return kNoMatch;
    }

    struct NameEditRAII {
      NameEdits m_Saved;
      NameEdits& m_Restore;
      NameEditRAII(llvm::StringRef Name, NameEdits& Exts, bool IsExt = true)
      : m_Saved(Exts), m_Restore(Exts) {
        if (!m_Saved)
          return;
        if (IsExt) {
          llvm::StringRef Ext = llvm::sys::path::extension(Name);
          if (Ext.empty())
            return;
          // If the extension is in the list already, don't run extension pass
          if (std::find(m_Saved->begin(), m_Saved->end(), Ext)
              != m_Saved->end()) {
            m_Restore = nullptr;
            return;
          }
        } else {
          // If the the name begins with a prefix, don't run prefix pass
          for (const auto& Prefix : *m_Saved) {
            if (Name.startswith(Prefix)) {
              m_Restore = nullptr;
              return;
            }
          }
        }
      }
      ~NameEditRAII() { if (m_Saved) m_Restore = m_Saved; }
    };

  public:
    FileSearch(NameEdits Extensions = nullptr,
               NameEdits Prefixes = nullptr, bool Verbose = false)
      : m_Prefixes(Prefixes), m_Extensions(Extensions), m_BestMatch(kNoMatch),
        m_Verbose(Verbose) {}

    std::string operator () (llvm::StringRef Name,
                             const std::vector<std::string>& Paths) {
      // Set the extension and prefix lists to null is the name has either
      // an extension or prefix.
      NameEditRAII Exts(Name, m_Extensions), Prfx(Name, m_Prefixes, false);
      for (const auto& Path : Paths) {
        // Absolute name match, best possible, done
        if (lookForFile(Name, Path) == kMatchName)
          break;

        // Prior behaviour, take first match whether it's a library or not
        // if (m_BestMatch == kMatchNotRight)
        //  break;
      }
      if (m_BestMatch >= kMatchName)
        return m_FilePath;
      return std::string();
    }

    bool isSharedLib() const {
      return m_BestMatch >= kMatchName;
    }
  };
} // anonymous namespace

  bool DynamicLibraryManager::isSharedLib(llvm::StringRef LibName,
                                          bool* exists) {
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

    // 'Overallocate' so users can add custom extensions/prefixes
    // Stackbased so it's not a big deal
    const llvm::SmallVector<llvm::StringRef, 6> kLibraryExtenstions = {
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
    };
    const llvm::SmallVector<llvm::StringRef, 3> kLibraryPrefixes = {
      "lib"
    };

    FileSearch Search(&kLibraryExtenstions, &kLibraryPrefixes, m_Opts.Verbose());
    std::string Found = Search(libName, m_Opts.LibSearchPath);
    if (Found.empty())
      Found = Search(libName, m_SystemSearchPaths);
    if (Search.isSharedLib())
      return FileEntry(platform::NormalizePath(Found), FileEntry::kLibrarySet);

    // Mark as resolved, nothing was found, or something that isn't a shared lib
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
