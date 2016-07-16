//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Axel Naumann <axel@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#ifndef CLING_INPUT_VALIDATOR_H
#define CLING_INPUT_VALIDATOR_H

#include "clang/Basic/TokenKinds.h"

#include "llvm/ADT/StringRef.h"

#include <stack>

namespace clang {
  class LangOptions;
}

namespace cling {
  namespace meta {

    ///\brief Provides storage for the input and tracks down whether
    /// (, [, {, are balanced.
    ///
    class InputValidator {
    private:
      ///\brief The input being collected.
      ///
      std::string m_Input;

      ///\brief Stack used for checking the brace balance.
      ///
      std::deque<int> m_ParenStack;

    public:
      InputValidator() {}
      ~InputValidator() {}

      ///\brief Brace balance validation could encounter.
      ///
      enum ValidationResult {
        kIncomplete, ///< There is dangling brace.
        kComplete, ///< All braces are in balance.
        kMismatch ///< Closing brace doesn't match to opening. Eg: void f(};
      };

      ///\brief Checks whether the input contains balanced number of braces
      ///
      ///\param[in] Line - Input line to validate.
      ///\param[in] Opts - Language options to validate against (default to C++)
      ///\returns Information about the outcome of the validation.
      ///
      ValidationResult validate(llvm::StringRef Line,
                                const clang::LangOptions* Opts = nullptr);

      ///\brief Retrieves the number of spaces that the next input line should
      /// be indented.
      ///
      int getExpectedIndent() const { return m_ParenStack.size(); }

      ///\brief Resets the collected input and its corresponding brace stack.
      ///
      ///\param[in] input - Grab the collected input before reseting.
      ///
      void reset(std::string* input = nullptr);

      ///\brief Return whether we are inside a mult-line comment
      ///
      ///\returns true if currently inside a multi-line comment block
      ///
      bool inBlockComment() const;
    };
  }
}
#endif // CLING_INPUT_VALIDATOR_H
