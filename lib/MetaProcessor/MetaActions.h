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
#include "cling/MetaProcessor/Commands.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Utils/FileEntry.h"

namespace cling {
  class Transaction;
  class Interpreter;
  class Value;

  namespace meta {
    class Processor;

    ///\brief Actions related to loading and executing of files and managing
    ///
    class Actions {
      struct LoadPoints;
      Processor& m_MetaProcessor;
      std::unique_ptr<LoadPoints> m_Watermarks;

      Interpreter& getInterpreter() const {
        return m_MetaProcessor.getInterpreter();
      }

      ///\brief Private actOnUCommand that takes a resolved FileEntry
      ///
      CommandResult doUCommand(const llvm::StringRef& file,
                               const FileEntry& fileEntry);

    public:

      Actions(Processor&);
      ~Actions();

      ///\brief L command includes the given file or loads the given library.
      ///
      ///\param[in] file - The file/library to be loaded.
      ///\param[out] transaction - Transaction containing the loaded file.
      ///
      CommandResult actOnLCommand(llvm::StringRef file,
                                  Transaction** transaction = 0);

      ///\brief F command loads the given framework and optioanlly its root
      /// header (OS X only)
      ///
      ///\param[in] file - The framework to be loaded
      ///\param[out] transaction - Transaction containing the loaded file.
      ///
      CommandResult actOnFCommand(llvm::StringRef file,
                                  Transaction** transaction = 0);

      ///\brief Actions to be performed on a given file. Loads the given file
      /// and calls a function with the name of the file.
      ///
      /// If the function needs arguments they are specified after the filename
      /// in parenthesis.
      ///
      ///\param[in] file - The filename to load.
      ///\param[in] args - The optional list of arguments.
      ///\param[out] result - If not NULL, will hold the value of the last
      ///                     expression.
      ///
      CommandResult actOnxCommand(llvm::StringRef file, llvm::StringRef args,
                                  Value* result);

      ///\brief Actions to be performed on unload command.
      ///
      ///\param[in] file - The file to unload.
      ///
      CommandResult actOnUCommand(llvm::StringRef file);

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
    };

  } // end namespace meta
} // end namespace cling

#endif // CLING_META_PARSER_H
