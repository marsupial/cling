//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Roman Zulak
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_FILE_ENTRY_H
#define CLING_FILE_ENTRY_H

#include "llvm/ADT/StringRef.h"
#include <string>

namespace clang {
  class FileEntry;
}
namespace cling {
  class Interpreter;
  class DynamicLibraryManager;
  
  namespace utils {

    ///\brief FileEntry to describe how a path was resolved, to avoid resolving
    /// a path multiple times (both for performance reasons and atomicity).
    ///
    /// For the most part these are not constructed directly, rather a
    /// a string is used as arguments to a functions that take these.
    /// Those functions in turn pass the constucted object around and when the
    /// filepath is finally resolved mark the object accordingly.
    ///
    class FileEntry {
      friend class cling::Interpreter;
      friend class cling::DynamicLibraryManager;

      const clang::FileEntry *mEntry;
      const std::string mNameOrPath;
      const unsigned mFlags : 3;

      enum {
        kResolved   = 1 << 0,
        kIsLibrary  = 1 << 1,
        kExists     = 1 << 2,
        kLibrarySet = kIsLibrary | kExists
      };

      /// ###FIXME Defined in Interpreter.cpp to get inlined & linked their
      FileEntry(const clang::FileEntry* fe, const char* absPath);

      FileEntry(std::string f, unsigned char flags)
          : mEntry(nullptr), mNameOrPath(std::move(f)),
            mFlags(flags | kResolved) {}

    public:
      ///\brief FileEntry to describe how a path was resolved, to avoid
      /// resolving multiple times. You probably shouldn't be using any of these
      /// constructors directly they are to automatically construct FileEntry
      /// objects from strings.
      FileEntry(const FileEntry& fe)
          : mEntry(fe.mEntry), mNameOrPath(fe.mNameOrPath), mFlags(fe.mFlags) {}
      FileEntry(FileEntry&& e)
          : mEntry(e.mEntry), mNameOrPath(std::move(e.mNameOrPath)),
            mFlags(e.mFlags) {}
      FileEntry(std::string f)
          : mEntry(nullptr), mNameOrPath(std::move(f)), mFlags(0) {}
      FileEntry(llvm::StringRef f)
          : mEntry(nullptr), mNameOrPath(f.str()), mFlags(0) {}
      FileEntry(const char* f)
          : mEntry(nullptr), mNameOrPath(f), mFlags(0) {}

      ///\brief Only allowed once the path is resolved and known to exist.
      /// Use name() otherwise
      ///
      const std::string &filePath() const {
        assert(mFlags & (kExists | kResolved) &&
               "Path doesn't exist or hasn't been resolved");
        return mNameOrPath;
      }

      ///\brief Returns the name, once resolved() is true it is the fullpath
      const std::string& name() const { return mNameOrPath; }
      bool resolved() const { return mFlags & kResolved; }
      bool isLibrary() const { return (mFlags & kLibrarySet) == kLibrarySet; }
      bool exists() const { return mFlags & kExists; }

      ///\brief Return clang::FileEntry, can be null
      operator const clang::FileEntry* () const { return mEntry; }

      ///\brief Old API emulation
      /// These simulate a previously used pattern perhaps still esed in ROOT:
      /// string canPath = lookup(file)
      /// if (canPath.empty())
      ///   canPath = file;
      ///
      bool empty() const { return !exists(); }
      operator const std::string& () const { return mNameOrPath; }
    };
  }
  typedef utils::FileEntry FileEntry;
}

#endif // CLING_FILE_ENTRY_H
