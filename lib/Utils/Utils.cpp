//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vasil.georgiev.vasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/Utils/Utils.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/ErrorHandling.h"
#include "clang/Lex/HeaderSearchOptions.h"

using namespace clang;
using namespace llvm;
using namespace cling::utils;

namespace {

  // Adapted from clang/lib/Frontend/CompilerInvocation.cpp
  class FormatIncludes {
    const HeaderSearchOptions& m_Opts;
    const bool m_WithSystem, m_WithFlags;

    inline void addArg(const char* Arg) {
      if (m_WithFlags)
        addFlag(Arg);
    }
    inline void addFlag(const HeaderSearchOptions::Entry& E) {
      addFlag(E.IsFramework? "-F" : "-I");
    }

  protected:
    virtual void addFlag(const char* Arg) = 0;
    virtual void addPath(const std::string& Path) = 0;
  
  public:
    FormatIncludes(const HeaderSearchOptions& Opts, bool WithSystem,
                   bool WithFlags) :
      m_Opts(Opts), m_WithSystem(WithSystem), m_WithFlags(WithFlags) {}
    virtual ~FormatIncludes() {}
  
    void dump() {
      if (m_WithSystem && m_Opts.Sysroot != "/") {
        addArg("-isysroot");
        addPath(m_Opts.Sysroot);
      }

      /// User specified include entries.
      for (unsigned i = 0, e = m_Opts.UserEntries.size(); i != e; ++i) {
        const HeaderSearchOptions::Entry &E = m_Opts.UserEntries[i];

        if (E.IsFramework && E.Group != frontend::Angled)
          llvm::report_fatal_error("Invalid option set!");

        if (!m_WithSystem) {
          if (E.Group != frontend::After || E.Group != frontend::Quoted)
            continue;
        }
        if (m_WithFlags) {
          switch (E.Group) {
            case frontend::After:
              addFlag("-idirafter");
              break;

            case frontend::Quoted:
              addFlag("-iquote");
              break;

            case frontend::System:
              addFlag("-isystem");
              break;

            case frontend::IndexHeaderMap:
              addFlag("-index-header-map");
              addFlag(E);
              break;

            case frontend::CSystem:
              addFlag("-c-isystem");
              break;

            case frontend::ExternCSystem:
              addFlag("-extern-c-isystem");
              break;

            case frontend::CXXSystem:
              addFlag("-cxx-isystem");
              break;

            case frontend::ObjCSystem:
              addFlag("-objc-isystem");
              break;

            case frontend::ObjCXXSystem:
              addFlag("-objcxx-isystem");
              break;

            case frontend::Angled:
              addFlag(E);
              break;
          }
        }

        addPath(E.Path);
      }

      if (m_WithSystem) {
        if (!m_Opts.ResourceDir.empty()) {
          addArg("-resource-dir");
          addPath(m_Opts.ResourceDir);
        }
        if (!m_Opts.ModuleCachePath.empty()) {
          addArg("-fmodule-cache-path");
          addPath(m_Opts.ModuleCachePath);
        }
        if (!m_Opts.UseStandardSystemIncludes)
          addArg("-nostdinc");
        if (!m_Opts.UseStandardCXXIncludes)
          addArg("-nostdinc++");
        if (m_Opts.UseLibcxx)
          addArg("-stdlib=libc++");
        if (m_Opts.Verbose)
          addArg("-v");
      }
    }
  };

  class StreamDump : public FormatIncludes {
    llvm::raw_ostream& m_Stream;
  public:
    StreamDump(const HeaderSearchOptions& Opts, bool WithSystem, bool WithFlags,
               llvm::raw_ostream& S) :
      FormatIncludes(Opts, WithSystem, WithFlags), m_Stream(S) {}

    ~StreamDump() { m_Stream << "\n"; }
    void addFlag(const char* Arg) override { m_Stream << Arg << " "; }
    void addPath(const std::string& Str) override {  m_Stream << Str << "\n"; }
  };

  class VectorCopy : public FormatIncludes {
    SmallVectorImpl<std::string>& m_Vector;
  public:
    VectorCopy(const HeaderSearchOptions& Opts, bool WithSystem, bool WithFlags,
               SmallVectorImpl<std::string>& V) :
      FormatIncludes(Opts, WithSystem, WithFlags), m_Vector(V) {}

    void addFlag(const char* Arg) override { m_Vector.push_back(Arg); }
    void addPath(const std::string& Str) override {  m_Vector.push_back(Str); }
  };
}

namespace cling {
  namespace utils {

    void CopyIncludePaths(const HeaderSearchOptions& Opts,
                          SmallVectorImpl<std::string>& Paths,
                          bool WithSystem, bool WithFlags) {
      VectorCopy(Opts, WithSystem, WithFlags, Paths).dump();
    }

    void DumpIncludePaths(const HeaderSearchOptions& Opts,
                          llvm::raw_ostream& Out,
                          bool WithSystem, bool WithFlags) {
      StreamDump(Opts, WithSystem, WithFlags, Out).dump();
    }

  } // namespace utils
} // namespace cling
