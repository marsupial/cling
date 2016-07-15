//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vvasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "MetaSema.h"
#include "cling/Interpreter/DynamicLibraryManager.h"
#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Interpreter/Value.h"
#include "cling/MetaProcessor/MetaProcessor.h"
#include "../lib/Interpreter/IncrementalParser.h"

#include "clang/AST/ASTContext.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Sema/Sema.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "clang/Lex/LexDiagnostic.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/Support/Casting.h"
#include "cling/Interpreter/ClangInternalState.h"


#include "clang/Lex/Preprocessor.h"
#include "clang/Basic/SourceManager.h"

#include <cstdlib>
#include <iostream>

#ifdef __APPLE__
 #include "llvm/Support/Process.h"
#endif

namespace cling {

  MetaSema::ActionResult MetaSema::actOnLCommand(llvm::StringRef file,
                                             Transaction** transaction /*= 0*/){
    FileEntry fe = getInterpreter().lookupFileOrLibrary(file);
    ActionResult result = doUCommand(file, fe);
    if (result != AR_Success)
      return result;

    // In case of libraries we get .L lib.so, which might automatically pull in
    // decls (from header files). Thus we want to take the restore point before
    // loading of the file and revert exclusively if needed.
    const Transaction* unloadPoint = getInterpreter().getLastTransaction();
     // fprintf(stderr,"DEBUG: Load for %s unloadPoint is %p\n",file.str().c_str(),unloadPoint);
    // TODO: extra checks. Eg if the path is readable, if the file exists...

    //if (fe.empty())
    //  canFile = file;

    if (getInterpreter().loadFile(fe, true /*allowSharedLib*/, transaction)
        == Interpreter::kSuccess) {
      registerUnloadPoint(unloadPoint, std::move(fe));
      return AR_Success;
    }
    return AR_Failure;
  }

  MetaSema::ActionResult MetaSema::actOnFCommand(llvm::StringRef file,
                                             Transaction** transaction /*= 0*/){
#if defined(__APPLE__)
    class FrameworkResolver {
    public:
      class FrameworkAction {
      public:
        struct FrameworkPaths {
          std::string library, header, bundle;
          FrameworkPaths(std::string &lib, std::string &hdr,
                         const std::string &bndl) {
            library.swap(lib), header.swap(hdr);
            if (!library.empty() || !header.empty())
              bundle = bndl;
          }
        };
        virtual ~FrameworkAction() {}
        virtual bool operator()(const FrameworkPaths &paths) = 0;
      };
      typedef FrameworkAction::FrameworkPaths FrameworkPaths;

    private:
      const std::string m_Extension;
      std::string m_Name;
      const llvm::StringRef *m_HeaderName;
      size_t m_IsAbsolute, m_DoHeader;

      // In the order in which they are seached
      static bool getLocalFrameworks(std::string &dir,
                                     const std::string * = 0) {
        dir = llvm::sys::fs::getMainExecutable(
            "cling", (void *)uintptr_t(&getLocalFrameworks));
        if (!dir.empty()) {
          // parent of directory containing binary
          llvm::StringRef parent = llvm::sys::path::parent_path(dir);
          dir = llvm::sys::path::parent_path(parent);
          dir.append("/Frameworks");
          return true;
        }
        return false;
      }
      static bool getUserFrameworks(std::string &dir, const std::string * = 0) {
        llvm::SmallString<1024> tmp;
        if (llvm::sys::path::home_directory(tmp)) {
          dir.assign(tmp.begin(), tmp.end());
          dir.append("/Library/Frameworks");
          return true;
        }
        return false;
      }
      static bool getLibraryFrameworks(std::string &dir,
                                       const std::string *sysRoot) {
        if (sysRoot)
          dir = *sysRoot;
        dir.append("/Library/Frameworks");
        return true;
      }
      static bool getSystemFrameworks(std::string &dir,
                                      const std::string *sysRoot) {
        if (sysRoot)
          dir = *sysRoot;
        dir.append("/System/Library/Frameworks");
        return true;
      }
      static bool getDYLD_LIBRARY_PATH(std::string &dir,
                                       const std::string * = 0) {
        llvm::Optional<std::string> path =
            llvm::sys::Process::GetEnv("DYLD_LIBRARY_PATH");
        if (path.hasValue()) {
          dir.swap(path.getValue());
          return !dir.empty();
        }
        return false;
      }
      static bool isRegFile(const std::string &dir) {
        return llvm::sys::fs::is_regular_file(dir);
      }
      static bool isDirectory(const std::string &dir) {
        return llvm::sys::fs::is_directory(dir);
      }

      bool tryDirectory(std::string &frPath, FrameworkAction &action) const {
        if (llvm::sys::fs::is_directory(frPath)) {
          frPath.append("/");
          frPath.append(m_Name);
          frPath.append(m_Extension);

          // <Path>/Name.framework is valid, try to find its children
          if (isDirectory(frPath))
            return action(resolve(frPath));
        }
        return false;
      }

    public:
      FrameworkResolver(const llvm::StringRef &file)
          : m_Extension(".framework"), m_HeaderName(nullptr),
            m_IsAbsolute(false) {
        {
          const size_t prevLen = file.size();
          llvm::SmallString<1024> tmp(file);
          llvm::sys::path::replace_extension(tmp, "");
          m_DoHeader = prevLen > tmp.size() ? prevLen - tmp.size() : 0;
        }

        if (/*file.endswith(m_Extension) &&*/ llvm::sys::fs::is_directory(
            file)) {
          m_Name.assign(file.data(), file.size() - m_DoHeader);
          llvm::sys::path::filename(m_Name).str().swap(m_Name);
          m_IsAbsolute = true; // phase2(file.str());
        } else if (m_DoHeader) {
          // Do we need to transform X.framework to X.h?
          if (m_DoHeader == m_Extension.size() &&
              std::string(file.end() - m_DoHeader, file.end()) == m_Extension) {
            m_HeaderName = NULL;
          } else
            m_HeaderName = &file;
        }
      }

      FrameworkPaths resolve(const std::string &frPath) const {

        std::string libPath(frPath + "/" +
                            m_Name); // <Path>/Name.framework/Name

        // std::error_code llvm::sys::fs:identify_magic(const Twine &path,
        // file_magic &result); macho_dynamically_linked_shared_lib ||
        // macho_bundle

        if (!isRegFile(libPath))
          std::string().swap(libPath);

        std::string headerPath;
        if (m_DoHeader) {
          headerPath = frPath + "/Headers/"; // <Path>/Name.framework/Headers
          if (m_HeaderName == NULL) {
            headerPath.append(m_Name);
            headerPath.append(".h");
          } else
            headerPath.append(m_HeaderName->str());

          if (!isRegFile(headerPath))
            std::string().swap(headerPath);
        }
        return FrameworkPaths(libPath, headerPath, frPath);
      }

      bool resolve(const llvm::StringRef &file,
                   const clang::HeaderSearchOptions &opts,
                   FrameworkAction &action) {

        m_Name.assign(file.begin(), file.end() - m_DoHeader);

        /* Cuurent order of search paths
          DYLD_LIBRARY_PATH, meant to ovveride anything else
          -F directories given on command line
          executable/../../Library/Framework
          <user>/Library/Framework

          The next are a bit of a conundrum, as isysroot will likely have the
         headers we need,
           For example: do we really want to load CoreFoundation from an SDKs
         folder?
           Or we do it from the running system, but we probably won't get the
         headers

         isysroot/Library/Frameworks
         isysroot/System/Library/Frameworks
         /Library/Frameworks
         /System/Library/Frameworks
        */
        
        // DYLD_LIBRARY_PATH
        std::string paths;
        if (getDYLD_LIBRARY_PATH(paths)) {
          size_t pos = 0;
          while ((pos = paths.find(':')) != std::string::npos) {
            std::string tmp = paths.substr(0, pos);
            if (tryDirectory(tmp, action))
              return true;
            paths.erase(0, pos + 1);
          }
          if (!paths.empty() && tryDirectory(paths, action))
            return true;
          
          std::string().swap(paths);
        }

        // -F command line flags
        for (clang::HeaderSearchOptions::Entry e : opts.UserEntries) {
          if (e.IsFramework && !e.Path.empty()) {
            if (tryDirectory(e.Path, action))
              return true;
          }
        }


        // -isysroot, then /
        typedef bool (*FrameworkPaths)(std::string & dir,
                                       const std::string *sysRoot);
        FrameworkPaths getPaths[] = {getLocalFrameworks, getUserFrameworks,
                                     getLibraryFrameworks, getSystemFrameworks,
                                     0};

        const std::string *sysRoot = opts.Sysroot.empty() ? nullptr :
                                                            &opts.Sysroot;
        for (unsigned r = 0; r < 2; ++r) {
          for (unsigned i = 0; getPaths[i]; ++i) {
            std::string cur;
            if (getPaths[i](cur, sysRoot)) {
              if (tryDirectory(cur, action))
                return true;
            }
          }
          if (!sysRoot)
            break;

          // Maybe the framework isn't in -isysroot, but the actual system root
          sysRoot = NULL;
          getPaths[0] = getLibraryFrameworks;
          getPaths[1] = getSystemFrameworks;
          getPaths[2] = NULL;
        }

        return false;
      }

      bool isAbsolute() const { return m_IsAbsolute; }
    };

    class ClingLoadFramework : public FrameworkResolver::FrameworkAction {
      MetaSema &m_Sema;
      Transaction **m_Transaction;

    public:
      ClingLoadFramework(MetaSema &s, Transaction **t)
          : m_Sema(s), m_Transaction(t) {}

      bool operator()(const FrameworkPaths &paths) override {

        const bool haveLib = !paths.library.empty(),
                   haveHeader = !paths.header.empty();
        if (haveLib || haveHeader) {
          if (m_Sema.actOnUCommand(paths.bundle) != MetaSema::AR_Success)
            return false;

          Interpreter &interp =
              const_cast<Interpreter&>(m_Sema.getInterpreter());

          Interpreter::CompilationResult iRslt =
              haveLib ? interp.loadLibrary(paths.library, false)
                      : Interpreter::kSuccess;

          // Probably shouldn't go for the headers if the library failed already
          if (haveHeader && iRslt == Interpreter::kSuccess) {
            const Transaction *unloadPoint = interp.getLastTransaction();

            iRslt = interp.loadHeader(paths.header, m_Transaction);
            if (interp.getLastTransaction() != unloadPoint)
              m_Sema.registerUnloadPoint(unloadPoint, paths.header);
          }

          return iRslt == Interpreter::kSuccess;
        }
        return false;
      }
    };

    ClingLoadFramework action(*this, transaction);
    FrameworkResolver fResolver(file);
    if (fResolver.isAbsolute()) {
      if (action(fResolver.resolve(file.str())))
        return AR_Success;
    } else if (llvm::sys::fs::is_regular_file(file))
      return actOnLCommand(file, transaction);
    else if (fResolver.resolve(file,
                 getInterpreter().getCI()->getHeaderSearchOpts(),
                 action))
      return AR_Success;

#endif
    return AR_Failure;
  }

  static std::string buildArguments(const char* buf,
                                    llvm::StringRef* fileName = nullptr,
                                    unsigned* argCount = nullptr) {
    const char* scopeTok[] = { "()", "{}" };
    const bool brack = fileName != nullptr;

    std::string args(1, scopeTok[brack][0]);
    if (fileName) {
      // Add argv[0]: filename
      args.append(1, '"');
      args.append(fileName->str());
      args.append(1, '"');
    }
    unsigned nArgs = 1;

    if (buf) {
      while (*buf) {
        // Skip whitespace
        while (*buf && ::isspace(*buf))
          ++buf;

        if (const char tok = *buf) {
          // Mark current state
          
          const char* end = buf+1;
          const bool isLit = tok == '"';
          if (tok == '\'' || isLit) {
            // Grab literal
            while (*end && *end != tok) {
              if (*(++end) == '\\')
                end += 2; // jump whatever it is
            }
            if (brack && !isLit) {
              // Make 'some string' to "some string"
              ++buf;
            } else if (*end)
              ++end;
          } else {
            // Grab whatever
            while (*end && !::isspace(*end))
              ++end;
          }
          if (const size_t N = end-buf) {
            ++nArgs;

            const llvm::StringRef arg(buf, N);
            if (args.size() > 1)
              args.append(1, ',');

            if (brack && !isLit) {
              args.append(1, '"');
              args.append(arg.str());
              args.append(1, '"');
            }
            else {
              args.append(arg.str());

              // Advance over terminating '
              if (brack && tok == '\'')
                ++end;
            }
          }
          buf = end;
        }
      }
    }

    // Close it out
    args.append(1, scopeTok[brack][1]);
    if (argCount)
      *argCount = nArgs;

    return args;
  }
  
  MetaSema::ActionResult MetaSema::actOnxCommand(llvm::StringRef file,
                                                 llvm::StringRef args,
                                                 Value* result) {
    cling::Transaction* T = 0;
    MetaSema::ActionResult actionResult = actOnLCommand(file, &T);
    if (actionResult == AR_Success) {
      // Look for start of parameters:
      typedef std::pair<llvm::StringRef,llvm::StringRef> StringRefPair;

      StringRefPair pairPathFile = file.rsplit('/');
      if (pairPathFile.second.empty()) {
        pairPathFile.second = pairPathFile.first;
      }

      using namespace clang;
      NamedDecl* ND = nullptr;
      StringRefPair pairFuncExt = pairPathFile.second.rsplit('.');
      std::string expression;

      // T can be nullptr if there is no code (but comments)
      if (T) {
        const llvm::StringRef main("main", 4);
        llvm::PointerIntPair<clang::NamedDecl*, 1, bool> Named =
          T->containsNamedDecl(pairFuncExt.first, &main);
        if ((ND = Named.getPointer())) {
          if (Named.getInt() == 0) {
            // Function matching filename was found
            expression = pairFuncExt.first.str();
            if (!args.empty()) {
              if (args[0] != '(') {
                // Bash style
                expression += buildArguments(args.data());
              } else {
                // C-style
                expression += args.str();
              }
            } else {
              // No args given
              expression += "()";
            }
            expression += " /* invoking function corresponding to '.x' */";
          } else {
            // No function matching filename, but 'main' was found
            if (args.empty() || args[0] != '(') {
              // Convert bash args to args to argc, argv[]
              unsigned nArgs;
              const std::string bargs = buildArguments(args.data(), &file,
                                                       &nArgs);
              llvm::raw_string_ostream argStr(expression);
              argStr << "int argc = " << nArgs << "; const char* argv[] = "
                     << std::move(bargs) << ";" << main << "(argc, argv);";
            } else {
              // Users responsibility to be set up correctly
              expression = main.str() + args.str();
            }
            expression += " /* invoking main from .x */";
          }
        }
      }
      if (!ND) {
        DiagnosticsEngine& Diags = getInterpreter().getCI()->getDiagnostics();
        SourceLocation Loc;
        if (T)
          Loc = T->getSourceStart(Diags.getSourceManager());

        Diags.Report(Loc, Diags.getCustomDiagID(
                             DiagnosticsEngine::Level::Warning,
                             "cannot find function '%0()'; falling back to .L"))
            << pairFuncExt.first;
        return AR_Success;
      }

      assert(!expression.empty() && "Invocation expression wasn't built");
                               ;
      if (getInterpreter().echo(expression, result) != Interpreter::kSuccess)
        actionResult = AR_Failure;
    }
    return actionResult;
  }

  MetaSema::ActionResult MetaSema::doUCommand(const llvm::StringRef &file,
                                              const FileEntry &fe ) {
    if (!m_Watermarks.get())
      return AR_Success;

    // FIXME: unload, once implemented, must return success / failure
    // Lookup the file
    if (const clang::FileEntry* Entry = fe) {
      Watermarks::iterator Pos = m_Watermarks->first.find(Entry);
       //fprintf(stderr,"DEBUG: unload request for %s\n",file.str().c_str());

      if (Pos != m_Watermarks->first.end()) {
        const Transaction* unloadPoint = Pos->second;
        // Search for the transaction, i.e. verify that is has not already
        // been unloaded ; This can be removed once all transaction unload
        // properly information MetaSema that it has been unloaded.
        bool found = false;
        //for (auto t : getInterpreter().m_IncrParser->getAllTransactions()) {
        for(const Transaction *t = getInterpreter().getFirstTransaction();
            t != 0; t = t->getNext()) {
           //fprintf(stderr,"DEBUG: On unload check For %s unloadPoint is %p are t == %p\n",file.str().c_str(),unloadPoint, t);
          if (t == unloadPoint ) {
            found = true;
            break;
          }
        }
        if (!found) {
          m_MetaProcessor.getOuts() << "!!!ERROR: Transaction for file: " << file << " has already been unloaded\n";
        } else {
           //fprintf(stderr,"DEBUG: On Unload For %s unloadPoint is %p\n",file.str().c_str(),unloadPoint);
          while(getInterpreter().getLastTransaction() != unloadPoint) {
             //fprintf(stderr,"DEBUG: unload transaction %p (searching for %p)\n",getInterpreter().getLastTransaction(),unloadPoint);
            const clang::FileEntry* EntryUnloaded
              = m_Watermarks->second[getInterpreter().getLastTransaction()];
            if (EntryUnloaded) {
              Watermarks::iterator PosUnloaded
                = m_Watermarks->first.find(EntryUnloaded);
              if (PosUnloaded != m_Watermarks->first.end()) {
                m_Watermarks->first.erase(PosUnloaded);
              }
            }
            getInterpreter().unload(/*numberOfTransactions*/1);
          }
        }
        DynamicLibraryManager* DLM = getInterpreter().getDynamicLibraryManager();
        DLM->unloadLibrary(std::move(fe));
        m_Watermarks->first.erase(Pos);
      }
    }
    return AR_Success;
  }

  MetaSema::ActionResult MetaSema::actOnUCommand(llvm::StringRef file) {
    //Get the canonical path, taking into account interp and system search paths
    return doUCommand(file, getInterpreter().lookupFileOrLibrary(file));
  }

  bool MetaSema::registerUnloadPoint(const Transaction* unloadPoint,
                                     FileEntry inEntry ) {
    FileEntry fileEntry = getInterpreter().lookupFileOrLibrary(std::move(inEntry));
    const clang::FileEntry* Entry = fileEntry;
    if (!Entry) {
      // There's a small chance clang couldn't resolve the relative path, and
      // fileEntry now contains a resolved absolute one.
      if (fileEntry.resolved())
        Entry =
          getInterpreter().getSema().getSourceManager().getFileManager().getFile(
              fileEntry.name(), /*OpenFile*/ false, /*CacheFailure*/ false);
    }
    if (Entry) {
      if (!m_Watermarks.get()) {
        m_Watermarks.reset(new std::pair<Watermarks, ReverseWatermarks>);
        if (!m_Watermarks.get()) {
          ::perror("Could not allocate watermarks");
          return false;
        }
      }
      if (!m_Watermarks->first[Entry]) {
        // register as a watermark
        m_Watermarks->first[Entry] = unloadPoint;
        m_Watermarks->second[unloadPoint] = Entry;
        return true;
      }
      getInterpreter().getCI()->getDiagnostics().Report(clang::SourceLocation(),
                         clang::diag::err_duplicate_member) << fileEntry.name();
    } else {
      getInterpreter().getCI()->getDiagnostics().Report(clang::SourceLocation(),
                        clang::diag::err_pp_file_not_found) << fileEntry.name();
    }
    return false;
  }
} // end namespace cling
