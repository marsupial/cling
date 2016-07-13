//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vasil.georgiev.vasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_META_SEMA_H
#define CLING_META_SEMA_H

#include "cling/MetaProcessor/MetaProcessor.h"

#include "cling/Interpreter/Transaction.h"

#include "clang/Basic/FileManager.h" // for DenseMap<FileEntry*>

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Optional.h"

namespace llvm {
  class StringRef;
  class raw_ostream;
}

namespace cling {
  class Interpreter;
  class MetaProcessor;
  class Value;

  ///\brief Semantic analysis for our home-grown language. All implementation
  /// details of the commands should go here.
  class MetaSema {
  private:
    MetaProcessor& m_MetaProcessor;
    typedef llvm::DenseMap<const clang::FileEntry*, const Transaction*> Watermarks;
    typedef llvm::DenseMap<const Transaction*, const clang::FileEntry*> ReverseWatermarks;
    std::unique_ptr< std::pair<Watermarks, ReverseWatermarks> > m_Watermarks;

    Interpreter& getInterpreter() const {
      return m_MetaProcessor.getInterpreter();
    }

  public:
    enum SwitchMode {
      kOff = 0,
      kOn = 1,
      kToggle = 2
    };

    enum ActionResult {
      AR_Failure = 0,
      AR_Success = 1
    };

    MetaSema(MetaProcessor& meta) : m_MetaProcessor(meta) {}


    ///\brief L command includes the given file or loads the given library.
    ///
    ///\param[in] file - The file/library to be loaded.
    ///\param[out] transaction - Transaction containing the loaded file.
    ///
    ActionResult actOnLCommand(llvm::StringRef file,
                               Transaction** transaction = 0);

    ///\brief F command loads the given framework and optioanlly its root header (OS X only)
    ///
    ///\param[in] file - The frameowrk to b eloaded
    ///\param[out] transaction - Transaction containing the loaded file.
    ///
    ActionResult actOnFCommand(llvm::StringRef file,
                               Transaction** transaction = 0);

    ///\brief Actions to be performed on a given file. Loads the given file and
    /// calls a function with the name of the file.
    ///
    /// If the function needs arguments they are specified after the filename in
    /// parenthesis.
    ///
    ///\param[in] file - The filename to load.
    ///\param[in] args - The optional list of arguments.
    ///\param[out] result - If not NULL, will hold the value of the last
    ///                     expression.
    ///
    ActionResult actOnxCommand(llvm::StringRef file, llvm::StringRef args,
                               Value* result);

    ///\brief Actions to be performed on unload command.
    ///
    ///\param[in] file - The file to unload.
    ///
    ActionResult actOnUCommand(llvm::StringRef file);

    ///\brief Register the file as an upload point for the current
    ///  Transaction: when unloading that file, all transactions after
    ///  the current one will be reverted.
    ///
    ///\param [in] T - The unload point - any later Transaction will be
    /// reverted when filename is unloaded.
    ///\param [in] filename - The name of the file to be used as unload
    ///  point.
    ///
    ///\returns Whether registration was successful or not.
    ///
    bool registerUnloadPoint(const Transaction* T, FileEntry filename);

  private:
    ///\brief Private actOnUCommand that takes a resolved FileEntry
    ///
    ActionResult doUCommand(const llvm::StringRef& file,
                            const FileEntry& fileEntry);
  };

} // end namespace cling

#endif // CLING_META_PARSER_H
