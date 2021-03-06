//===--- History.h - Previously Entered Lines -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defines the interface for setting and retrieving previously
//  entered input, with a persistent backend (i.e. a history file).
//
//  Axel Naumann <axel@cern.ch>, 2011-05-12
//===----------------------------------------------------------------------===//

#ifndef TEXTINPUT_HISTORY_H
#define TEXTINPUT_HISTORY_H

#include <cstddef>
#include <string>
#include <vector>

namespace textinput {
  // Previous input lines.

  class History {
  public:
    enum {
      kPruneLengthDefault = -1 // Prune length equals 80% of fMaxDepth
    };
    History(const char* filename, bool linematch = true);
    ~History();

    // If fMaxDepth == 0, do not create history output.
    void SetMaxDepth(size_t maxDepth) { fMaxDepth = maxDepth; }
    void SetPruneLength(size_t pruneLength = (size_t) kPruneLengthDefault) {
      fPruneLength = pruneLength; }

    // Indices are reverse! I.e. 0 is newest!
    const std::string& GetLine(size_t Idx) const {
      if (Idx == (size_t)-1)  {
        static const std::string sEmpty;
        return sEmpty;
      }
      return fEntries[fEntries.size() - 1 - Idx];
    }

    const std::string& GetLine(size_t& InOutIdx, int Increment,
                               const std::string& Search);

    size_t GetSize() const { return fEntries.size(); }

    void AddLine(const std::string& line);
    void ModifyLine(size_t Idx, const std::string& Line) {
      if (!LineSearch)
        fEntries[fEntries.size() - 1 - Idx] = Line;
      // Does not sync to file!
    }

    void AppendToFile();
    void ReadFile(const char* FileName);

    void EnableLineMatching(bool B);
    void TextEntered(size_t& HistoryLocStart);

  private:
    std::string fHistFileName; // History file name
    size_t fMaxDepth; // Max number of entries before pruning
    size_t fPruneLength; // Remaining entries after pruning
    size_t fNumHistFileLines; // Hist file's number of lines at previous access
    std::vector<std::string> fEntries; // Previous input lines

    struct LineSearcher { // Provide ine level filtering of the history
      std::string Match;
      bool Enabled;
      LineSearcher() : Enabled(false) {}
    } * LineSearch;
  };
}

#endif //TEXTINPUT_HISTORY_H
