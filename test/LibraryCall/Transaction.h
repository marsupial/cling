//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

struct CLING_EXPORT BaseClass {
  const char* const Name;
  void dump(const char* What) const;

  BaseClass(const char* N = "BaseClass");
  ~BaseClass();
  virtual void DoSomething() const;
};

#ifdef CLING_SUBCLASS
struct CLING_EXPORT SubClass : public BaseClass {
  SubClass();
  virtual void DoSomething() const;
};
#endif

