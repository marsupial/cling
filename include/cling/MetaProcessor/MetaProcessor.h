//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Lukasz Janyst <ljanyst@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_METAPROCESSOR_H
#define CLING_METAPROCESSOR_H

#include "cling/Interpreter/Interpreter.h"

#include "clang/Basic/SourceLocation.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallString.h"

#include <memory>
#include <stdio.h>

namespace cling {

  class Interpreter;
  class InputValidator;
  class MetaSema;
  class Value;

  ///\brief Class that helps processing meta commands, which add extra
  /// interactivity. Syntax .Command [arg0 arg1 ... argN]
  ///
  class MetaProcessor {
  private:
    ///\brief Reference to the interpreter
    ///
    Interpreter& m_Interp;

    ///\brief The input validator is used to figure out whether to switch to
    /// multiline mode or not. Checks for balanced parenthesis, etc.
    ///
    std::unique_ptr<InputValidator> m_InputValidator;

    ///\brief The actions/state that can be performed.
    ///
    std::unique_ptr<MetaSema> m_Actions;

    ///\brief Currently executing file as passed into executeFile
    ///
    llvm::StringRef m_CurrentlyExecutingFile;

    ///\brief Outermost currently executing file as passed into executeFile
    ///
    llvm::StringRef m_TopExecutingFile;

    ///\brief The output stream being used for various purposes.
    ///
    llvm::raw_ostream* m_Outs;

    ///\brief Internal class to store redirection state.
    ///
    class RedirectOutput;
    std::unique_ptr<RedirectOutput> m_RedirectOutput;

    //Was quit requested?
    bool m_QuitRequested;

  public:
    enum RedirectionScope {
      kSTDOUT  = 1,
      kSTDERR  = 2,
      kSTDBOTH = 3,
      kSTDSTRM = 4 // "&1" or "&2" is not a filename
    };

    ///\brief Class to be created for each processing input to be
    /// able to redirect std.
    class MaybeRedirectOutputRAII {
      MetaProcessor& m_MetaProcessor;
    public:
      MaybeRedirectOutputRAII(MetaProcessor& P);
      ~MaybeRedirectOutputRAII();
    };

  public:
    MetaProcessor(Interpreter& interp, llvm::raw_ostream& outs);
    ~MetaProcessor();

    Interpreter& getInterpreter() const { return m_Interp; }

    ///\brief Get the output stream used by the MetaProcessor for its output.
    /// (in contrast to the interpreter's output which is redirected using
    /// setStdStream()).
    llvm::raw_ostream& getOuts() const { return *m_Outs; }

    ///\brief Set the output stream used by the MetaProcessor for its output.
    /// (in contrast to the interpreter's output which is redirected using
    /// setStdStream()).
    ///
    ///\returns the address of the previous output stream, or 0 if it was unset.
    llvm::raw_ostream* setOuts(llvm::raw_ostream& outs) {
      llvm::raw_ostream* prev = m_Outs;
      m_Outs = &outs;
      return prev;
    }

    ///\brief Process the input coming from the prompt and possibli returns
    /// result of the execution of the last statement
    /// @param[in] input_line - the user input
    /// @param[out] result - the cling::Value as result of the
    ///             execution of the last statement
    /// @param[out] compRes - whether compilation was successful
    ///
    ///\returns 0 on success or the indentation of the next input line should
    /// have in case of multi input mode.
    ///\returns -1 if quit was requiested.
    ///
    int process(const char* input_line,
                Interpreter::CompilationResult& compRes,
                cling::Value* result = nullptr);

    ///\brief When continuation is requested, this cancels and ignores previous
    /// input, resetting the continuation to a new line.
    void cancelContinuation() const;

    ///\brief Returns the number of imbalanced tokens seen in the current input.
    ///
    int getExpectedIndent() const;

    ///\brief Exectue the given meta commad
    /// @param[in] cmd - Command to execute (not prefixed with the meta-token)
    ///\returns kSuccess if the command succeded, kFailure if the command failed
    /// or kMoreInputExpected if it wasn't even a command.
    ///
    Interpreter::CompilationResult doMetaCommand(llvm::StringRef cmd,
                                                 Value* result);

    ///\brief Reads prompt input from file.
    ///
    ///\param [in] filename - The file to read.
    /// @param[out] result - the cling::Value as result of the
    ///             execution of the last statement
    ///\param [in] posOpenCurly - position of the opening '{'; -1 if no curly.
    ///
    ///\returns result of the compilation.
    ///
    Interpreter::CompilationResult
    readInputFromFile(llvm::StringRef filename,
                      Value* result,
                      size_t posOpenCurly = (size_t)(-1));

    ///\brief Set the stdout and stderr stream to the appropriate file.
    ///
    ///\param [in] file - The file for the redirection.
    ///\param [in] stream - Which stream to redirect: stdout, stderr or both.
    ///\param [in] append - Write in append mode.
    ///
    void setStdStream(llvm::StringRef file, RedirectionScope stream,
                      bool append);

    ///\brief Register the file as an unload point for the Transaction T
    ///  when unloading that file, all transactions after T will be reverted.
    ///
    ///\param [in] T - the last transaction stay should filename be unloaded.
    ///\param [in] filename - The name of the file to be used as unload point.
    ///\returns whether registration was successful or not
    ///
    bool registerUnloadPoint(const Transaction* T, llvm::StringRef filename);

    ///\brief Return the MetaSema actions of the MetaProcessor
    MetaSema& getActions() const { return *m_Actions; }

    ///\brief Return a reference to query/set whether quit was requested
    bool& quit() { return m_QuitRequested; }
  };
} // end namespace cling

#endif // CLING_METAPROCESSOR_H
