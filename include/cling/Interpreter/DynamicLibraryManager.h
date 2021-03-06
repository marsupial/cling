//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vasil.georgiev.vasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_DYNAMIC_LIBRARY_MANAGER_H
#define CLING_DYNAMIC_LIBRARY_MANAGER_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"

#include "llvm/Support/Path.h"
#include "cling/Utils/FileEntry.h"

namespace cling {
  class InterpreterCallbacks;
  class InvocationOptions;

  ///\brief A helper class managing dynamic shared objects.
  ///
  class DynamicLibraryManager {
  public:
    ///\brief Describes the result of loading a library.
    ///
    enum LoadLibResult {
      kLoadLibSuccess, ///< library loaded successfully
      kLoadLibAlreadyLoaded,  ///< library was already loaded
      kLoadLibNotFound, ///< library was not found
      kLoadLibLoadError, ///< loading the library failed
      kLoadLibNumResults
    };

  private:
    typedef const void* DyLibHandle;
    typedef llvm::PointerIntPair<DyLibHandle,1,bool> DyLibValue;
    typedef llvm::MapVector<std::string,DyLibValue> DyLibs;
    ///\brief DynamicLibraries loaded by this Interpreter.
    ///
    std::unique_ptr<DyLibs> m_DyLibs;

    ///\brief Contains the list of the current include paths.
    ///
    const InvocationOptions& m_Opts;

    ///\brief System's include path, get initialized at construction time.
    ///
    std::vector<std::string> m_SystemSearchPaths;

    InterpreterCallbacks* m_Callbacks;

  public:
    DynamicLibraryManager(const InvocationOptions& Opts);
    ~DynamicLibraryManager();
    InterpreterCallbacks* getCallbacks() { return m_Callbacks; }
    const InterpreterCallbacks* getCallbacks() const { return m_Callbacks; }
    void setCallbacks(InterpreterCallbacks* C) { m_Callbacks = C; }

    ///\brief Looks up a library taking into account the current include paths
    /// and the system include paths.
    ///\param[in] libStem - The filename being looked up
    ///
    ///\returns the canonical path to the file or empty string if not found
    ///
    FileEntry lookupLibrary(FileEntry libStem) const;

    ///\brief Loads a shared library.
    ///
    ///\param [in] libStem - The file to load.
    ///\param [in] permanent - If false, the file can be unloaded later.
    ///\param [in] resolved - Whether libStem is an absolute path or resolved
    ///               from a previous call to DynamicLibraryManager::lookupLibrary
    ///
    ///\returns kLoadLibSuccess on success, kLoadLibAlreadyLoaded if the library
    /// was already loaded, kLoadLibError if the library cannot be found or any
    /// other error was encountered.
    ///
    LoadLibResult loadLibrary(FileEntry libStem, bool permanent);

    void unloadLibrary(FileEntry libStem);

    ///\brief Returns true if the file was a dynamic library and it was already
    /// loaded.
    ///
    bool isLibraryLoaded(const FileEntry &file) const;

    ///\brief Explicitly tell the execution engine to use symbols from
    ///       a shared library that would otherwise not be used for symbol
    ///       resolution, e.g. because it was dlopened with RTLD_LOCAL.
    ///\param [in] handle - the system specific shared library handle.
    ///
    static void ExposeHiddenSharedLibrarySymbols(void* handle);

    static std::string normalizePath(llvm::StringRef path);

    static bool isSharedLib(llvm::StringRef LibName, bool* exists = nullptr);
  };
} // end namespace cling
#endif // CLING_DYNAMIC_LIBRARY_MANAGER_H
