//===--- History.cpp - Previously Entered Lines -----------------*- C++ -*-===//
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

#include "textinput/History.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <assert.h>

#ifdef WIN32
# include <stdio.h>
extern "C" unsigned long __stdcall GetCurrentProcessId(void);
#else
# include <unistd.h>
#endif

namespace textinput {
  History::History(const char* filename, bool linematch):
    fHistFileName(filename ? filename : ""), fMaxDepth((size_t) -1),
    fPruneLength(0), fNumHistFileLines(0), LineSearch(nullptr) {
    // Create a history object, initialize from filename if the file
    // exists. Append new lines to filename taking into account the
    // maximal number of lines allowed by SetMaxDepth().
    if (filename) ReadFile(filename);
    if (linematch) EnableLineMatching(linematch);
  }

  History::~History() {}

  void
  History::AddLine(const std::string& line) {
    // Standard history search until text entered
    if (LineSearch) LineSearch->Enabled = false;
    // Add a line to entries and file.
    if (line.empty()) return;
    fEntries.push_back(line);
    AppendToFile();
  }

  void
  History::ReadFile(const char* FileName) {
    // Inject all lines of FileName.
    // Intentionally ignore fMaxDepth
    std::ifstream InHistFile(FileName);
    if (!InHistFile) return;
    std::string line;
    while (std::getline(InHistFile, line)) {
      while (!line.empty()) {
        size_t len = line.length();
        char c = line[len - 1];
        if (c != '\n' && c != '\r') break;
        line.erase(len - 1);
      }
      if (!line.empty()) {
        fEntries.push_back(line);
      }
    }
    fNumHistFileLines = fEntries.size();
  }

  void
  History::AppendToFile() {
    // Write last entry to hist file.
    // Prune if needed.
    if (fHistFileName.empty() || !fMaxDepth) return;

    // Calculate prune length to use
    size_t nPrune = fPruneLength;
    if (nPrune == (size_t)kPruneLengthDefault) {
      nPrune = (size_t)(fMaxDepth * 0.8);
    } else if (nPrune > fMaxDepth) {
      nPrune = fMaxDepth - 1; // fMaxDepth is guaranteed to be > 0.
    }

    // Don't check for the actual line count of the history file after every
    // single line. Once every 50% on the way between nPrune and fMaxDepth is
    // enough.
    if (fNumHistFileLines < fMaxDepth
        && (fNumHistFileLines % (fMaxDepth - nPrune)) == 0) {
      fNumHistFileLines = 0;
      std::string line;
      std::ifstream in(fHistFileName.c_str());
      while (std::getline(in, line))
        ++fNumHistFileLines;
    }

    size_t numLines = fNumHistFileLines;
    if (numLines >= fMaxDepth) {
      // Prune! But don't simply write our lines - other processes might have
      // added their own.
      std::string line;
      std::ifstream in(fHistFileName.c_str());
      std::stringstream pruneFileNameStream;
      pruneFileNameStream << fHistFileName + "_prune"
#if _WIN32
                          << ::GetCurrentProcessId();
#else
                          << ::getpid();
#endif
      std::ofstream out(pruneFileNameStream.str().c_str());
      if (out) {
        if (in) {
          while (numLines >= nPrune && std::getline(in, line)) {
            // skip
            --numLines;
          }
          while (std::getline(in, line)) {
            out << line << '\n';
          }
        }
        out << fEntries.back() << '\n';
        in.close();
        out.close();
#ifdef WIN32
        ::_unlink(fHistFileName.c_str());
#else
        ::unlink(fHistFileName.c_str());
#endif
        if (::rename(pruneFileNameStream.str().c_str(), fHistFileName.c_str()) == -1){
           std::cerr << "ERROR in textinput::History::AppendToFile(): "
              "cannot rename " << pruneFileNameStream.str() << " to " << fHistFileName;
        }
        fNumHistFileLines = nPrune;
      }
    } else {
      std::ofstream out(fHistFileName.c_str(), std::ios_base::app);
      out << fEntries.back() << '\n';
      ++fNumHistFileLines;
    }
  }


  void History::EnableLineMatching(bool B) {
    if (LineSearch) {
      if (!B)
        delete LineSearch;
    } else if (B)
      LineSearch = new LineSearcher;
  }

  void History::TextEntered(size_t& SearchStart) {
    if (LineSearch) {
      LineSearch->Enabled = true;
      SearchStart = 0;
    }
  }
    
  static size_t IncrWithOverflow(size_t Idx, int Incr, size_t Max, bool& Over) {
    const size_t Val = Idx + Incr;
    if (Incr < 0) {
      if (Val > Idx || Val >= Max) {
        Over = true;
        return 0;
      }
    } else if (Val < Idx || Val>= Max) {
      Over = true;
      return Max;
    }
    Over = false;
    return Idx + Incr;
  }

  static size_t IncrWithOverflow(size_t Idx, int Incr, size_t Max) {
    bool Drop;
    return IncrWithOverflow(Idx, Incr, Max, Drop);
  }

  const std::string& History::GetLine(size_t& InOuIdx, int Incr,
                                      const std::string& InStr) {
    assert(Incr != 0 && "Choose a direction!");
    const size_t LastEntry = fEntries.empty() ? 0 : fEntries.size() - 1;
    if (!LineSearch || !LineSearch->Enabled || InStr.empty() ||
        fEntries.empty()) {
      if (LineSearch) {
        // Free the string buffer
        if (!LineSearch->Match.empty())
          std::string().swap(LineSearch->Match);
        // Disable filtering until text entered
        LineSearch->Enabled = false;
      }
      InOuIdx = IncrWithOverflow(InOuIdx, Incr, LastEntry);
      return GetLine(InOuIdx);
    }

    assert(InOuIdx != (size_t) -1 && InOuIdx <= LastEntry);
    size_t Idx = InOuIdx;
    std::string& Search = LineSearch->Match;

    // Set the Search to InStr when:
    //   Search.empty(), first search
    //   InStr doesn't begin with previous Search, new search
    //   InStr doesn't match last hit, characters added or deleted
    if (Search.empty() || InStr.find(Search) != 0 ||
        InStr != fEntries[LastEntry - Idx]) {
      Search = InStr;
      if (fEntries[LastEntry - Idx].find(Search) == 0) {
        // Edited a previos match 'A' -> 'AB', drop the first match (a repeat)
        Idx = IncrWithOverflow(Idx, Incr, LastEntry);
      } else {
        // Restart from end or beginning of history
        Idx = Incr > 0 ? 0 : LastEntry;
      }
    } else
      Idx = IncrWithOverflow(Idx, Incr, LastEntry);

    bool Wrapped = false;
    const size_t StartIdx = Idx;

    while (!Wrapped && (Incr < 0 ? Idx <= StartIdx : Idx >= StartIdx)) {
      const std::string& Line = fEntries[LastEntry - Idx];
      if (Line.find(Search) == 0) {
        InOuIdx = Idx;
        return Line;
      }

      Idx = IncrWithOverflow(Idx, Incr, LastEntry, Wrapped);
    }
    if (false) {
      // Block history to matches only
      return InStr;
    }
    // When hits exhausted switch to regular mode
    InOuIdx = IncrWithOverflow(InOuIdx, Incr, LastEntry);
    return GetLine(InOuIdx);
  }
}
