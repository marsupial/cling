//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Vassil Vassilev <vasil.georgiev.vasilev@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/Interpreter/Value.h"

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/LookupHelper.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Interpreter/Value.h"
#include "cling/Utils/AST.h"
#include "cling/Utils/Output.h"
#include "cling/Utils/Validation.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Type.h"
#include "clang/Sema/Sema.h"
#include "clang/Frontend/CompilerInstance.h"

#include "llvm/Support/Format.h"
#include "llvm/ExecutionEngine/GenericValue.h"

#include <locale>
#include <string>

// GCC 4.x doesn't have the proper UTF-8 conversion routines. So use the
// LLVM conversion routines (which require a buffer 4x string length).
#if !defined(__GLIBCXX__) || (__GNUC__ >= 5)
 #include <codecvt>
#else
 #define LLVM_UTF8
 #include "llvm/Support/ConvertUTF.h"
#endif

using namespace cling;

// Exported for RuntimePrintValue.h
namespace cling {
  namespace valuePrinterInternal {
    extern const char* const kEmptyCollection = "{}";
    extern const char* const kUndefined = "<undefined>";
  }
}

namespace {

const static char
  * const kNullPtrStr = "nullptr",
  * const kNullPtrTStr = "nullptr_t",
  * const kTrueStr = "true",
  * const kFalseStr = "false",
  * const kInvalidAddr = " <invalid memory address>";

static std::string enclose(std::string Mid, const char* Begin,
                           const char* End, size_t Hint = 0) {
  Mid.reserve(Mid.size() + Hint ? Hint : (::strlen(Begin) + ::strlen(End)));
  Mid.insert(0, Begin);
  Mid.append(End);
  return Mid;
}

static std::string enclose(const clang::QualType& Ty, clang::ASTContext& C,
                           const char* Begin = "(", const char* End = "*)",
                           size_t Hint = 3, bool WithTag = false) {
  return enclose(cling::utils::TypeName::GetFullyQualifiedName(Ty, C, WithTag),
                 Begin, End, Hint);
}

static clang::QualType
getElementTypeAndExtent(const clang::ConstantArrayType *CArrTy,
                        std::string& extent) {
  clang::QualType ElementTy = CArrTy->getElementType();
  const llvm::APInt &APSize = CArrTy->getSize();
  extent += '[' +  std::to_string(APSize.getZExtValue()) + ']';
  if (auto CArrElTy
      = llvm::dyn_cast<clang::ConstantArrayType>(ElementTy.getTypePtr())) {
    return getElementTypeAndExtent(CArrElTy, extent);
  }
  return ElementTy;
}

static std::string getTypeString(const Value &V) {
  clang::ASTContext &C = V.getASTContext();
  clang::QualType Ty = V.getType().getDesugaredType(C).getNonReferenceType();

  if (llvm::dyn_cast<clang::BuiltinType>(Ty.getCanonicalType()))
    return enclose(Ty, C);

  if (Ty->isPointerType()) {
    // Print char pointers as strings.
    if (Ty->getPointeeType()->isCharType())
      return enclose(Ty, C);

    // Fallback to void pointer for other pointers and print the address.
    return "(const void**)";
  }
  if (Ty->isArrayType()) {
    if (Ty->isConstantArrayType()) {
      std::string extent("(*)");
      clang::QualType InnermostElTy
        = getElementTypeAndExtent(C.getAsConstantArrayType(Ty), extent);
      return enclose(InnermostElTy, C, "(", (extent + ")*(void**)").c_str());
    }
    return "(void**)";
  }
  if (Ty->isObjCObjectPointerType())
    return "(const void**)";

  // In other cases, dereference the address of the object.
  // If no overload or specific template matches,
  // the general template will be used which only prints the address.
  return enclose(Ty, C, "*(", "**)", 5, true);
}

/// RAII object to disable and then re-enable access control in the LangOptions.
struct AccessCtrlRAII_t {
  clang::LangOptions& LangOpts;
  clang::DiagnosticsEngine& Diags;
  const bool PrevAccess, PrevSupress;

  AccessCtrlRAII_t(cling::Interpreter& Interp, bool Supress = false)
      : LangOpts(Interp.getCI()->getLangOpts()),
        Diags(Interp.getDiagnostics()), PrevAccess(LangOpts.AccessControl),
        PrevSupress(Diags.getSuppressAllDiagnostics()) {
    LangOpts.AccessControl = false;
    if (Supress) Diags.setSuppressAllDiagnostics(true);
  }

  ~AccessCtrlRAII_t() {
    LangOpts.AccessControl = PrevAccess;
    Diags.setSuppressAllDiagnostics(PrevSupress);
  }
};

#ifndef NDEBUG
/// Is typenam parsable?
bool canParseTypeName(cling::Interpreter& Interp, llvm::StringRef typenam) {

  AccessCtrlRAII_t AccessCtrlRAII(Interp);

  cling::Interpreter::CompilationResult Res
    = Interp.declare("namespace { void* cling_printValue_Failure_Typename_check"
                     " = (void*)" + typenam.str() + "nullptr; }");
  if (Res != cling::Interpreter::kSuccess)
    cling::errs() << "ERROR in cling::executePrintValue(): "
                     "this typename cannot be spelled.\n";
  return Res == cling::Interpreter::kSuccess;
}
#endif

static std::string printDeclType(const clang::QualType& QT,
                                 const clang::NamedDecl* D) {
  if (!QT.hasQualifiers())
    return D->getQualifiedNameAsString();
  return QT.getQualifiers().getAsString() + " " + D->getQualifiedNameAsString();
}

static std::string printQualType(clang::ASTContext& Ctx, clang::QualType QT) {
  using namespace clang;
  const QualType QTNonRef = QT.getNonReferenceType();

  std::string ValueTyStr("(");
  if (const TagType *TTy = dyn_cast<TagType>(QTNonRef))
    ValueTyStr += printDeclType(QTNonRef, TTy->getDecl());
  else if (const RecordType *TRy = dyn_cast<RecordType>(QTNonRef))
    ValueTyStr += printDeclType(QTNonRef, TRy->getDecl());
  else {
    const QualType QTCanon = QTNonRef.getCanonicalType();
    if (QTCanon->isBuiltinType() && !QTNonRef->isFunctionPointerType()
        && !QTNonRef->isMemberPointerType()) {
      ValueTyStr += QTCanon.getAsString(Ctx.getPrintingPolicy());
    }
    else if (const TypedefType* TDTy = dyn_cast<TypedefType>(QTNonRef)) {
      // FIXME: TemplateSpecializationType & SubstTemplateTypeParmType checks are
      // predominately to get STL containers to print nicer and might be better
      // handled in GetFullyQualifiedName.
      //
      // std::vector<Type>::iterator is a TemplateSpecializationType
      // std::vector<Type>::value_type is a SubstTemplateTypeParmType
      //
      QualType SSDesugar = TDTy->getLocallyUnqualifiedSingleStepDesugaredType();
      if (dyn_cast<SubstTemplateTypeParmType>(SSDesugar))
        ValueTyStr += utils::TypeName::GetFullyQualifiedName(QTCanon, Ctx);
      else if (dyn_cast<TemplateSpecializationType>(SSDesugar))
        ValueTyStr += utils::TypeName::GetFullyQualifiedName(QTNonRef, Ctx);
      else
        ValueTyStr += printDeclType(QTNonRef, TDTy->getDecl());
    }
    else
      ValueTyStr += utils::TypeName::GetFullyQualifiedName(QTNonRef, Ctx);
  }

  if (QT->isReferenceType())
    ValueTyStr += " &";

  return ValueTyStr + ")";
}

static std::string printAddress(const void* Ptr, const char Prfx = 0) {
  if (!Ptr)
    return kNullPtrStr;

  cling::smallstream Strm;
  if (Prfx)
    Strm << Prfx;
  Strm << Ptr;
  if (!utils::isAddressValid(Ptr))
    Strm << kInvalidAddr;
  return Strm.str();
}

} // anonymous namespace

namespace cling {

  // General fallback - prints the address
  std::string printValue(const void *ptr) {
    return printAddress(ptr, '@');
  }

  // void pointer
  std::string printValue(const void **ptr) {
    return printAddress(*ptr);
  }

  // Bool
  std::string printValue(const bool *val) {
    return *val ? kTrueStr : kFalseStr;
  }

  // Chars
  static std::string printOneChar(char Val,
                                  const std::locale& Locale = std::locale()) {
    llvm::SmallString<128> Buf;
    llvm::raw_svector_ostream Strm(Buf);
    Strm << "'";
    if (!std::isprint(Val, Locale)) {
      switch (std::isspace(Val, Locale) ? Val : 0) {
        case '\t': Strm << "\\t"; break;
        case '\n': Strm << "\\n"; break;
        case '\r': Strm << "\\r"; break;
        case '\f': Strm << "\\f"; break;
        case '\v': Strm << "\\v"; break;
        default:
          Strm << llvm::format_hex(uint64_t(Val)&0xff, 4);
      }
    }
    else
      Strm << Val;
    Strm << "'";
    return Strm.str();
  }

  std::string printValue(const char *val) {
    return printOneChar(*val);
  }

  std::string printValue(const signed char *val) {
    return printOneChar(*val);
  }

  std::string printValue(const unsigned char *val) {
    return printOneChar(*val);
  }

  // Ints
  std::string printValue(const short *val) {
    cling::smallstream strm;
    strm << *val;
    return strm.str();
  }

  std::string printValue(const unsigned short *val) {
    cling::smallstream strm;
    strm << *val;
    return strm.str();
  }

  std::string printValue(const int *val) {
    cling::smallstream strm;
    strm << *val;
    return strm.str();
  }

  std::string printValue(const unsigned int *val) {
    cling::smallstream strm;
    strm << *val;
    return strm.str();
  }

  std::string printValue(const long *val) {
    cling::smallstream strm;
    strm << *val;
    return strm.str();
  }

  std::string printValue(const unsigned long *val) {
    cling::smallstream strm;
    strm << *val;
    return strm.str();
  }

  std::string printValue(const long long *val) {
    cling::smallstream strm;
    strm << *val;
    return strm.str();
  }

  std::string printValue(const unsigned long long *val) {
    cling::smallstream strm;
    strm << *val;
    return strm.str();
  }

  // Reals
  std::string printValue(const float *val) {
    cling::smallstream strm;
    strm << llvm::format("%.5f", *val) << 'f';
    return strm.str();
  }

  std::string printValue(const double *val) {
    cling::smallstream strm;
    strm << llvm::format("%.6f", *val);
    return strm.str();
  }

  std::string printValue(const long double *val) {
    cling::smallstream strm;
    strm << llvm::format("%.8Lf", *val) << 'L';
    //strm << llvm::format("%Le", *val) << 'L';
    return strm.str();
  }

  // Char pointers
  std::string printString(const char *const *Ptr, size_t N = 10000) {
    // Assumption is this is a string.
    // N is limit to prevent endless loop if Ptr is not really a string.

    const char* Start = *Ptr;
    if (!Start)
      return kNullPtrStr;

    const char* End = Start + N;
    bool IsValid = utils::isAddressValid(Start);
    if (IsValid) {
      // If we're gonnd do this, better make sure the end is valid too
      // FIXME: getpagesize() & GetSystemInfo().dwPageSize might be better
      enum { PAGE_SIZE = 1024 };
      while (!(IsValid = utils::isAddressValid(End)) && N > 1024) {
        N -= PAGE_SIZE;
        End = Start + N;
      }
    }
    if (!IsValid) {
      cling::smallstream Strm;
      Strm << static_cast<const void*>(Start) << kInvalidAddr;
      return Strm.str();
    }

    if (*Start == 0)
      return "\"\"";

    // Copy the bytes until we get a null-terminator
    llvm::SmallString<1024> Buf;
    llvm::raw_svector_ostream Strm(Buf);
    Strm << "\"";
    while (Start < End && *Start)
      Strm << *Start++;
    Strm << "\"";

    return Strm.str();
  }

  std::string printValue(const char *const *val) {
    return printString(val);
  }

  std::string printValue(const char **val) {
    return printString(val);
  }

  // std::string
  std::string printValue(const std::string *val) {
    return "\"" + *val + "\"";
  }

  static std::string quoteString(std::string Str, const char Prefix) {
    // No wrap
    if (!Prefix)
      return Str;
    // Quoted wrap
    if (Prefix==1)
      return enclose(std::move(Str), "\"", "\"", 2);

    // Prefix quoted wrap
    char Begin[3] = { Prefix, '"', 0 };
    return enclose(std::move(Str), Begin, &Begin[1], 3);
  }

  static std::string quoteString(const char* Str, size_t N, const char Prefix) {
    return quoteString(std::string(Str, Str[N-1] == 0 ? (N-1) : N), Prefix);
  }

#ifdef LLVM_UTF8
  using llvm::ConversionResult;
  using llvm::ConversionFlags;
  using llvm::lenientConversion;
  using llvm::UTF8;
  using llvm::UTF16;
  using llvm::UTF32;
  template <class T> struct CharTraits;
  template <> struct CharTraits<char16_t> {
    static ConversionResult convert(const char16_t** begin, const char16_t* end,
                                    char** d, char* dEnd, ConversionFlags F ) {
      return ConvertUTF16toUTF8(reinterpret_cast<const UTF16**>(begin),
                                reinterpret_cast<const UTF16*>(end),
                                reinterpret_cast<UTF8**>(d),
                                reinterpret_cast<UTF8*>(dEnd), F);
    }
  };
  template <> struct CharTraits<char32_t> {
    static ConversionResult convert(const char32_t** begin, const char32_t* end,
                                    char** d, char* dEnd, ConversionFlags F ) {
      return ConvertUTF32toUTF8(reinterpret_cast<const UTF32**>(begin),
                                reinterpret_cast<const UTF32*>(end),
                                reinterpret_cast<UTF8**>(d),
                                reinterpret_cast<UTF8*>(dEnd), F);
    }
  };
  template <> struct CharTraits<wchar_t> {
    static ConversionResult convert(const wchar_t** src, const wchar_t* srcEnd,
                                    char** dst, char* dEnd, ConversionFlags F) {
      switch (sizeof(wchar_t)) {
        case sizeof(char16_t):
          return CharTraits<char16_t>::convert(
                            reinterpret_cast<const char16_t**>(src),
                            reinterpret_cast<const char16_t*>(srcEnd),
                            dst, dEnd, F);
        case sizeof(char32_t):
          return CharTraits<char32_t>::convert(
                            reinterpret_cast<const char32_t**>(src),
                            reinterpret_cast<const char32_t*>(srcEnd),
                            dst, dEnd, F);
        default: break;
      }
      llvm_unreachable("wchar_t conversion failure");
    }
  };

  template <typename T>
  static std::string encodeUTF8(const T* const Str, size_t N, const char Prfx) {
    const T *Bgn = Str,
            *End = Str + N;
    std::string Result;
    Result.resize(UNI_MAX_UTF8_BYTES_PER_CODE_POINT * N);
    char *ResultPtr = &Result[0],
         *ResultEnd = ResultPtr + Result.size();

    CharTraits<T>::convert(&Bgn, End, &ResultPtr, ResultEnd, lenientConversion);
    Result.resize(ResultPtr - &Result[0]);
    return quoteString(std::move(Result), Prfx);
  }

#else // !LLVM_UTF8

  template <class T> struct CharTraits { typedef T value_type; };
#if defined(LLVM_ON_WIN32) // Likely only to be needed when _MSC_VER < 19??
  template <> struct CharTraits<char16_t> { typedef unsigned short value_type; };
  template <> struct CharTraits<char32_t> { typedef unsigned int value_type; };
#endif

  template <typename T>
  static std::string encodeUTF8(const T* const Str, size_t N, const char Prfx) {
    typedef typename CharTraits<T>::value_type value_type;
    std::wstring_convert<std::codecvt_utf8_utf16<value_type>, value_type> Convert;
    const value_type* Src = reinterpret_cast<const value_type*>(Str);
    return quoteString(Convert.to_bytes(Src, Src + N), Prfx);
  }
#endif // LLVM_UTF8

  template <typename T>
  std::string utf8Value(const T* const Str, size_t N, const char Prefix,
                        std::string (*Func)(const T* const Str, size_t N,
                        const char Prfx) ) {
    if (!Str)
      return kNullPtrStr;
    if (N==0)
      return printAddress(Str, '@');

    // Drop the null terminator or else it will be encoded into the std::string.
    return Func(Str, Str[N-1] == 0 ? (N-1) : N, Prefix);
  }

  // declaration: cling/Utils/UTF8.h & cling/Interpreter/RuntimePrintValue.h
  template <class T>
  std::string toUTF8(const T* const Str, size_t N, const char Prefix);

  template <>
  std::string toUTF8<char16_t>(const char16_t* const Str, size_t N,
                               const char Prefix) {
    return utf8Value(Str, N, Prefix, encodeUTF8);
  }

  template <>
  std::string toUTF8<char32_t>(const char32_t* const Str, size_t N,
                               const char Prefix) {
    return utf8Value(Str, N, Prefix, encodeUTF8);
  }

  template <>
  std::string toUTF8<wchar_t>(const wchar_t* const Str, size_t N,
                              const char Prefix) {
    static_assert(sizeof(wchar_t) == sizeof(char16_t) ||
                  sizeof(wchar_t) == sizeof(char32_t), "Bad wchar_t size");

    if (sizeof(wchar_t) == sizeof(char32_t))
      return toUTF8(reinterpret_cast<const char32_t * const>(Str), N, Prefix);

    return toUTF8(reinterpret_cast<const char16_t * const>(Str), N, Prefix);
  }

  template <>
  std::string toUTF8<char>(const char* const Str, size_t N, const char Prefix) {
    return utf8Value(Str, N, Prefix, quoteString);
  }

  template <typename T>
  static std::string toUTF8(
      const std::basic_string<T, std::char_traits<T>, std::allocator<T>>* Src,
      const char Prefix) {
    if (!Src)
      return kNullPtrStr;
    return encodeUTF8(Src->data(), Src->size(), Prefix);
  }

  std::string printValue(const std::u16string* Val) {
    return toUTF8(Val, 'u');
  }

  std::string printValue(const std::u32string* Val) {
    return toUTF8(Val, 'U');
  }

  std::string printValue(const std::wstring* Val) {
    return toUTF8(Val, 'L');
  }

  // Unicode chars
  template <typename T>
  static std::string toUnicode(const T* Src, const char Prefix, char Esc = 0) {
    if (!Src)
      return kNullPtrStr;
    if (!Esc)
      Esc = Prefix;

    llvm::SmallString<128> Buf;
    llvm::raw_svector_ostream Strm(Buf);
    Strm << Prefix << "'\\" << Esc
         << llvm::format_hex_no_prefix(unsigned(*Src), sizeof(T)*2) << "'";
    return Strm.str();
  }

  std::string printValue(const char16_t *Val) {
    return toUnicode(Val, 'u');
  }

  std::string printValue(const char32_t *Val) {
    return toUnicode(Val, 'U');
  }

  std::string printValue(const wchar_t *Val) {
    return toUnicode(Val, 'L', 'x');
  }
} // end namespace cling

namespace {

static std::string toUTF8(clang::QualType RT, clang::ASTContext& Ctx,
                          const char* Ptr, size_t N, bool WroteSize) {
  if (RT->isCharType())
    return WroteSize ? std::string(Ptr, N) : std::string(Ptr);
  if (RT->isWideCharType())
    return cling::toUTF8(reinterpret_cast<const wchar_t*>(Ptr), N, 'L');
  if (RT->isChar16Type() ||
      Ctx.getTypeSizeInChars(RT).getQuantity() == sizeof(char16_t))
    return cling::toUTF8(reinterpret_cast<const char16_t*>(Ptr), N, 'u');
  if (RT->isChar32Type() ||
      Ctx.getTypeSizeInChars(RT).getQuantity() == sizeof(char32_t))
    return cling::toUTF8(reinterpret_cast<const char32_t*>(Ptr), N, 'U');
  return "<unkown string type>";
}

static std::string callCPrintValue(const Value& V, const void* Val,
                                   Interpreter* Interp,
                                   const clang::QualType& Ty) {
  clang::TagDecl* Tg = Ty->getAsTagDecl();
  if (!Tg)
    return printAddress(Val);

  cling::ostrstream Strm;
  const llvm::StringRef Name = Tg->getName();
  Strm << "clingPrint_" << Name;

  LookupHelper& LH = Interp->getLookupHelper();
  const clang::FunctionDecl* FD =
      LH.findAnyFunction(Strm.str(), LookupHelper::WithDiagnostics);
  if (!FD)
    return printAddress(Val);

  clang::ASTContext& Ctx = V.getASTContext();
  char Buf[2048];
  size_t Size = sizeof(Buf);
  bool WritesSize = false;
  Buf[0] = 0;

  Strm << "(";
  if (const size_t NArgs = FD->getNumParams()) {
    Strm << "(" << Name << "*)" << Val;
    if (NArgs > 1) {
      Strm << ",";
      if (FD->getParamDecl(1)->getType()->isPointerType()) {
        WritesSize = true;
        Strm << "(unsigned*)" << reinterpret_cast<void*>(&Size);
      } else
        Strm << Size;
      if (NArgs > 2) {
        Strm << "," << enclose(FD->getParamDecl(2)->getType(), Ctx, "(", ")")
             << reinterpret_cast<void*>(&Buf[0]);
      }
    }
  }
  Strm << ");";

  Value CallReturn;
  AccessCtrlRAII_t AccessCtrlRAII(*Interp, true);
  Interp->evaluate(Strm.str(), CallReturn);

  clang::QualType RT = FD->getReturnType();
  if (RT->isPointerType() && CallReturn.isValid()) {
    assert(CallReturn.getType() == RT && "Type mismatch");
    RT = RT->getPointeeType();
    if (!RT->isCharType() && !WritesSize) {
      llvm::errs() << "Unicode strings must write size\n";
      return "<unicode string>";
    }
    if (char* Ptr = reinterpret_cast<char*>(CallReturn.getPtr())) {
      // Done if function returned a Ptr inside Buf (including space for 0).
      if (Ptr >= &Buf[0] && Ptr <= &Buf[sizeof(Buf)-2])
        return toUTF8(RT, Ctx, Ptr, Size, WritesSize);

      struct AutoFree {
        char* Ptr;
        AutoFree(char* P) : Ptr(P) {}
        ~AutoFree() { ::free(Ptr); };
      } A(Ptr);
      return toUTF8(RT, Ctx, Ptr, Size, WritesSize);
    }
    return kNullPtrStr;
  }
  // Function might return void, but write into Buf argument
  return Buf[0] ? std::string(Buf) : printAddress(Val);
}

static std::string callPrintValue(const Value& V, const void* Val,
                                  const char* Cast = nullptr,
                                  const clang::QualType* Ty = nullptr) {
  Interpreter *Interp = V.getInterpreter();
  if (LLVM_UNLIKELY(!Interp->getSema().getLangOpts().CPlusPlus)) {
    assert(Ty && "C language printing requires a type.");
    return callCPrintValue(V, Val, Interp, *Ty);
  }

  Value printValueV;
  {
    // Use an llvm::raw_ostream to prepend '0x' in front of the pointer value.

    cling::ostrstream Strm;
    const char* Closer = ")";
    Strm << "cling::printValue(";
    if (Cast)
      Strm << "static_cast<" << Cast << ">(*";
    Strm << getTypeString(V) << &Val << Closer[!Cast] << ");";

    // We really don't care about protected types here (ROOT-7426)
    AccessCtrlRAII_t AccessCtrlRAII(*Interp, true);
    Interp->evaluate(Strm.str(), printValueV);
  }

  if (printValueV.isValid() && printValueV.getPtr())
    return *(std::string *) printValueV.getPtr();

  // That didn't work. We probably diagnosed the issue as part of evaluate().
  cling::errs() <<"ERROR in cling::executePrintValue(): cannot pass value!\n";

  // Check that the issue comes from an unparsable type name: lambdas, unnamed
  // namespaces, types declared inside functions etc. Assert on everything
  // else.
  assert(!canParseTypeName(*Interp, getTypeString(V))
         && "printValue failed on a valid type name.");

  return valuePrinterInternal::kUndefined;
}

template <typename T>
class HasExplicitPrintValue {
  template <typename C,
            typename = decltype(cling::printValue((C*)nullptr))>
  static std::true_type  test(int);
  static std::false_type test(...);
public:
    static constexpr bool value = decltype(test<T>(0))::value;
};

template <typename T> static
typename std::enable_if<!HasExplicitPrintValue<const T>::value, std::string>::type
executePrintValue(const Value& V, const T& Val) {
  return callPrintValue(V, &Val);
}

template <typename T> static
typename std::enable_if<HasExplicitPrintValue<const T>::value, std::string>::type
executePrintValue(const Value& V, const T& Val) {
  return printValue(&Val);
}

template <typename T> static std::string
convertPrint(const Value& V, const char* Conv) {
  return Conv ? callPrintValue(V, V.getPtr(), Conv)
              : executePrintValue(V, *reinterpret_cast<const T*>(V.getPtr()));
}

static std::string printEnumValue(const Value &V) {
  cling::ostrstream enumString;
  clang::ASTContext &C = V.getASTContext();
  clang::QualType Ty = V.getType().getDesugaredType(C);
  const clang::EnumType *EnumTy = Ty.getNonReferenceType()->getAs<clang::EnumType>();
  assert(EnumTy && "ValuePrinter.cpp: ERROR, printEnumValue invoked for a non enum type.");
  clang::EnumDecl *ED = EnumTy->getDecl();
  uint64_t value = V.getULL();
  bool IsFirst = true;
  llvm::APSInt ValAsAPSInt = C.MakeIntValue(value, Ty);
  for (clang::EnumDecl::enumerator_iterator I = ED->enumerator_begin(),
           E = ED->enumerator_end(); I != E; ++I) {
    if (I->getInitVal() == ValAsAPSInt) {
      if (!IsFirst) {
        enumString << " ? ";
      }
      enumString << "(" << I->getQualifiedNameAsString() << ")";
      IsFirst = false;
    }
  }
  enumString << " : " << printQualType(C, ED->getIntegerType()) << " "
    << ValAsAPSInt.toString(/*Radix = */10);
  return enumString.str();
}

static const clang::Expr* UnwrapExpression(const clang::Expr *Expr) {
  const clang::CastExpr *Cast = clang::dyn_cast<clang::CastExpr>(Expr);
  const clang::UnaryOperator *Op = clang::dyn_cast<clang::UnaryOperator>(Expr);
  while (Cast || Op) {
    if (Cast) Expr = Cast->getSubExpr();
    else if (Op) Expr = Op->getSubExpr();
    Cast = clang::dyn_cast<clang::CastExpr>(Expr);
    Op = clang::dyn_cast<clang::UnaryOperator>(Expr);
  }
  if (const clang::MaterializeTemporaryExpr* Mat =
                  llvm::dyn_cast<clang::MaterializeTemporaryExpr>(Expr))
    return UnwrapExpression(Mat->GetTemporaryExpr());
  return Expr;
}

static std::string printFunctionValue(const Value &V, const void *ptr, clang::QualType Ty) {
  cling::largestream o;
  o << "Function @" << ptr;

  // If a function is the first thing printed in a session,
  // getLastTransaction() will point to the transaction that loaded the
  // ValuePrinter, and won't have a wrapper FD.
  // Even if it did have one it wouldn't be the one that was requested to print.

  Interpreter &Interp = *const_cast<Interpreter *>(V.getInterpreter());
  const Transaction *T = Interp.getLastTransaction();
  if (clang::FunctionDecl *WrapperFD = T->getWrapperFD()) {
    clang::ASTContext &C = V.getASTContext();
    const clang::FunctionDecl *FD = nullptr;
    // CE should be the setValueNoAlloc call expr.
    if (const clang::CallExpr *CallE
            = llvm::dyn_cast_or_null<clang::CallExpr>(
                    utils::Analyze::GetOrCreateLastExpr(WrapperFD,
                                                        /*foundAtPos*/0,
                                                        /*omitDS*/false,
                                                        &Interp.getSema()))) {
      if (const clang::FunctionDecl *FDsetValue
        = llvm::dyn_cast_or_null<clang::FunctionDecl>(CallE->getCalleeDecl())) {
        if (FDsetValue->getNameAsString() == "cling_ValueExtraction" &&
            CallE->getNumArgs() == 5) {
          const clang::Expr* Arg4 = UnwrapExpression(CallE->getArg(4));
          if (const clang::DeclRefExpr* DeclRefExp =
                  llvm::dyn_cast<clang::DeclRefExpr>(Arg4)) {
            FD = llvm::dyn_cast<clang::FunctionDecl>(DeclRefExp->getDecl());
          }
        }
      }
    }

    if (FD) {
      o << '\n';
      clang::SourceRange SRange = FD->getSourceRange();
      const char *cBegin = 0;
      const char *cEnd = 0;
      bool Invalid;
      if (SRange.isValid()) {
        clang::SourceManager &SM = C.getSourceManager();
        clang::SourceLocation LocBegin = SRange.getBegin();
        LocBegin = SM.getExpansionRange(LocBegin).first;
        o << "  at " << SM.getFilename(LocBegin);
        unsigned LineNo = SM.getSpellingLineNumber(LocBegin, &Invalid);
        if (!Invalid)
          o << ':' << LineNo;
        o << ":\n";
        bool Invalid = false;
        cBegin = SM.getCharacterData(LocBegin, &Invalid);
        if (!Invalid) {
          clang::SourceLocation LocEnd = SRange.getEnd();
          LocEnd = SM.getExpansionRange(LocEnd).second;
          cEnd = SM.getCharacterData(LocEnd, &Invalid);
          if (Invalid)
            cBegin = 0;
        } else {
          cBegin = 0;
        }
      }
      if (cBegin && cEnd && cEnd > cBegin && cEnd - cBegin < 16 * 1024) {
        o << llvm::StringRef(cBegin, cEnd - cBegin + 1);
      } else {
        const clang::FunctionDecl *FDef;
        if (FD->hasBody(FDef))
          FD = FDef;
        FD->print(o);
        //const clang::FunctionDecl* FD
        //  = llvm::cast<const clang::FunctionType>(Ty)->getDecl();
      }
      // type-based print() never and decl-based print() sometimes does not
      // include a final newline:
      o << '\n';
    }
  }
  return o.str();
}

static std::string printStringType(const Value& V, const clang::Type* Type,
                                   bool Conv = false) {
  switch (V.getInterpreter()->getLookupHelper().isStringType(Type)) {
    case LookupHelper::kStdString:
      return convertPrint<std::string>(V, Conv ? "std::string" : nullptr);
    case LookupHelper::kWCharString:
      return convertPrint<std::wstring>(V, Conv ? "std::wstring" : nullptr);
    case LookupHelper::kUTF16Str:
      return convertPrint<std::u16string>(V, Conv ? "std::u16string" : nullptr);
    case LookupHelper::kUTF32Str:
      return convertPrint<std::u32string>(V, Conv ? "std::u32string" : nullptr);
    default: break;
  }
  return "";
}

typedef void (*PrintBuiltinValueProc)(const struct BuiltinPrintData& Data);

struct BuiltinPrintData {
  const cling::Value& Val;
  llvm::raw_ostream& Strm;

  struct RecursiveData {
    size_t* Dims; // Pointer to dimensions, *Dims == 0 means end of line
    PrintBuiltinValueProc Printer; // Prints the elements of last dimension
    size_t Offset;                 // Flat offset/index into element buffer
  } * Recursive; // Pointer to recursive data or null for single element

  BuiltinPrintData(const cling::Value& V, llvm::raw_ostream& S,
                   RecursiveData* R = nullptr)
      : Val(V), Strm(S), Recursive(R) {}
};

template <class T>
static void printBuiltinValues(const BuiltinPrintData& Data) {
  // When N == 0, not an array but single value
  if (!Data.Recursive) {
    const T Value = Data.Val.simplisticCastAs<T>();
    Data.Strm << cling::printValue(&Value);
  } else {
    const T* Array = reinterpret_cast<const T*>(Data.Val.getPtr());
    // Handle offsets for multi-dimensional arrays
    const size_t N = Data.Recursive->Dims[0];
    Array += Data.Recursive->Offset;
    Data.Recursive->Offset += N;

    Data.Strm << printValue(&Array[0]);
    for (size_t I = 1; I < N; ++I)
      Data.Strm << ", " << cling::printValue(&Array[I]);
  }
}

static void printBuiltinNull(const BuiltinPrintData& Data) {
  Data.Strm << kNullPtrStr;
}

static PrintBuiltinValueProc
getPrintBuiltinValueProc(const clang::BuiltinType::Kind Kind) {
  switch (Kind) {
    case clang::BuiltinType::NullPtr:  return printBuiltinNull;
    case clang::BuiltinType::Bool:     return printBuiltinValues<bool>;

    case clang::BuiltinType::Char_S:   return printBuiltinValues<char>;
    case clang::BuiltinType::SChar:    return printBuiltinValues<signed char>;
    case clang::BuiltinType::Short:    return printBuiltinValues<short>;
    case clang::BuiltinType::Int:      return printBuiltinValues<int>;
    case clang::BuiltinType::Long:     return printBuiltinValues<long>;
    case clang::BuiltinType::LongLong: return printBuiltinValues<long long>;

    case clang::BuiltinType::Char_U: return printBuiltinValues<unsigned char>;
    case clang::BuiltinType::UChar:  return printBuiltinValues<unsigned char>;
    case clang::BuiltinType::UShort: return printBuiltinValues<unsigned short>;
    case clang::BuiltinType::UInt:   return printBuiltinValues<unsigned int>;
    case clang::BuiltinType::ULong:  return printBuiltinValues<unsigned long>;
    case clang::BuiltinType::ULongLong:
      return printBuiltinValues<unsigned long long>;

    case clang::BuiltinType::Float:      return printBuiltinValues<float>;
    case clang::BuiltinType::Double:     return printBuiltinValues<double>;
    case clang::BuiltinType::LongDouble: return printBuiltinValues<long double>;

    case clang::BuiltinType::Char16:  return printBuiltinValues<char16_t>;
    case clang::BuiltinType::Char32:  return printBuiltinValues<char32_t>;
    case clang::BuiltinType::WChar_S: return printBuiltinValues<wchar_t>;

    default: break;
  }
  return nullptr;
}

static std::string printBuiltinValues(const clang::BuiltinType::Kind Kind,
                                      const cling::Value& V) {
  if (PrintBuiltinValueProc PrintProc = getPrintBuiltinValueProc(Kind)) {
    ostrstream Strm;
    PrintProc(BuiltinPrintData(V, Strm));
    return Strm.str();
  }
  return "";
}

static void RecursivePrint(const BuiltinPrintData& Data) {
  assert(Data.Recursive && "RecursivePrint without data.");
  BuiltinPrintData::RecursiveData* RD = Data.Recursive;
  const size_t Dim = *(RD->Dims++);
  if (RD->Dims[0] == 0) {
    // Last dimension, restore RD->Dims and dump the elements
    --RD->Dims;
    return RD->Printer(Data);
  }

  Data.Strm << "{ ";
  RecursivePrint(Data);
  Data.Strm << " }";
  for (size_t I = 1; I < Dim; ++I) {
    Data.Strm << ", { ";
    RecursivePrint(Data);
    Data.Strm << " }";
  }
  // Return Dims as prior dimension will likely call this again.
  --RD->Dims;
}

static std::string printBuiltinArray(const clang::BuiltinType* BT,
                                     const cling::Value& V,
                                     llvm::SmallVectorImpl<size_t>& Dims) {
  PrintBuiltinValueProc PrintProc = getPrintBuiltinValueProc(BT->getKind());
  if (!PrintProc)
    return "";

  largestream Strm;
  BuiltinPrintData::RecursiveData RD = {&Dims[0], PrintProc, 0};

  if (Dims.size() == 1) {
    const size_t N = Dims.front();
    // Try for strings first, they are special when full or empty
    const void* P = V.getPtr();
    switch (BT->getKind()) {
      case clang::BuiltinType::Char_S:
        return cling::toUTF8(reinterpret_cast<const char*>(P), N, 1);
      case clang::BuiltinType::Char16:
        return cling::toUTF8(reinterpret_cast<const char16_t*>(P), N, 'u');
      case clang::BuiltinType::Char32:
        return cling::toUTF8(reinterpret_cast<const char32_t*>(P), N, 'U');
      case clang::BuiltinType::WChar_S:
        return cling::toUTF8(reinterpret_cast<const wchar_t*>(P), N, 'L');
      default: break;
    }
  } else {
    // Terminate the dimensions
    Dims.push_back(0);
    PrintProc = RecursivePrint;
  }

  Strm << " { ";
  PrintProc(BuiltinPrintData(V, Strm, &RD));
  Strm << " }";
  return Strm.str();
}

static std::string printUnpackedClingValue(const Value &V) {
  // Find the Type for `std::string`. We are guaranteed to have that declared
  // when this function is called; RuntimePrintValue.h #includes it.
  const clang::ASTContext &C = V.getASTContext();
  const clang::QualType Td = V.getType().getDesugaredType(C);
  const clang::QualType Ty = Td.getNonReferenceType();

  if (Ty->isNullPtrType()) {
    // special case nullptr_t
    return kNullPtrTStr;
  } else if (Ty->isEnumeralType()) {
    // special case enum printing, using compiled information
    return printEnumValue(V);
  } else if (Ty->isFunctionType()) {
    // special case function printing, using compiled information
    return printFunctionValue(V, &V, Ty);
  } else if ((Ty->isPointerType() || Ty->isMemberPointerType()) && Ty->getPointeeType()->isFunctionType()) {
    // special case function printing, using compiled information
    return printFunctionValue(V, V.getPtr(), Ty->getPointeeType());
  } else if (clang::CXXRecordDecl *CXXRD = Ty->getAsCXXRecordDecl()) {
    if (CXXRD->isLambda())
      return printAddress(V.getPtr(), '@');

    std::string Str = printStringType(V, CXXRD->getTypeForDecl());
    if (!Str.empty())
      return Str;

    // look for conversion operator to std::string
    for (clang::NamedDecl* D : CXXRD->getVisibleConversionFunctions()) {
      if (clang::CXXConversionDecl* Conversion =
              llvm::dyn_cast<clang::CXXConversionDecl>(D->getUnderlyingDecl())) {
        clang::QualType RTy = Conversion->getConversionType().getNonReferenceType();
        if (clang::CXXRecordDecl *RTD = RTy->getAsCXXRecordDecl()) {
          Str = printStringType(V, RTD->getTypeForDecl(), true);
          if (!Str.empty())
            return Str;
        }
      }
    }
  } else if (const clang::BuiltinType *BT
      = llvm::dyn_cast<clang::BuiltinType>(Td.getCanonicalType().getTypePtr())) {
    const std::string ValueStr = printBuiltinValues(BT->getKind(), V);
    if (!ValueStr.empty())
      return ValueStr;
  } else
    assert(!Ty->isIntegralOrEnumerationType() && "Bad Type.");

  if (!V.getPtr())
    return kNullPtrStr;

  // Print all the other cases by calling into runtime 'cling::printValue()'.
  // Ty->isPointerType() || Ty->isReferenceType() || Ty->isArrayType()
  // Ty->isObjCObjectPointerType()
  return callPrintValue(V, V.getPtr(), nullptr, &Ty);
}

// Implements the CValuePrinter interface.
static std::string cling_PrintValue(const Value& Val) {
  if (Val.isValid() && !Val.getType()->isVoidType())
    return printUnpackedClingValue(Val);

  cling::smallstream Strm;
  Strm << valuePrinterInternal::kUndefined << ' ' << printAddress(&Val, '@');
  return Strm.str();
}

} // anonymous namespace

namespace cling {
  // cling::Value
  std::string printValue(const Value *value) {
    cling::smallstream strm;

    if (value->isValid()) {
      clang::ASTContext &C = value->getASTContext();
      clang::QualType QT = value->getType();
      strm << "boxes [";
      strm << enclose(QT, C, "(", ") ", 3);
      if (!QT->isVoidType()) {
        strm << printUnpackedClingValue(*value);
      }
      strm << "]";
    } else {
      strm << valuePrinterInternal::kUndefined
           << ' ' << printAddress(value, '@');
    }
    return strm.str();
  }

  namespace valuePrinterInternal {

    std::string printTypeInternal(const Value &V) {
      assert(V.getInterpreter() && "Invalid cling::Value");
      return printQualType(V.getASTContext(), V.getType());
    }

    std::string printValueInternal(const Value &V) {
      assert(V.getInterpreter() && "Invalid cling::Value");
      using namespace llvm;
      using namespace clang;

      // Early out for builtin types, pointers to them, and const arrays of them
      clang::QualType Ty =
          V.getType().getDesugaredType(V.getASTContext()).getCanonicalType();
      if (const BuiltinType *BT = dyn_cast<BuiltinType>(Ty.getTypePtr())) {
          std::string Str = printBuiltinValues(BT->getKind(), V);
          if (!Str.empty())
            return Str;
      } else if (Ty->isPointerType() || Ty->isReferenceType()) {
        // Check if the pointer resolves to a builtin-type
        unsigned level = 0;
        do {
          Ty = Ty->getPointeeType();
          ++level;
        } while (Ty->isPointerType() || Ty->isReferenceType());

        if (const BuiltinType *BT = dyn_cast<BuiltinType>(Ty.getTypePtr())) {
          const void *ptr = V.getPtr();
          // Is it a const char* string literal?
          if (level == 1 && BT->getKind() == BuiltinType::Char_S) {
            return printValue(reinterpret_cast<const char* const*>(&ptr));
          }
          return printValue(&ptr);
        }
      } else if (Ty->isConstantArrayType()) {
        clang::ASTContext& Ctx = V.getASTContext();
        llvm::SmallVector<size_t, 32> Dims;
        do {
          const clang::ConstantArrayType* ArTy = Ctx.getAsConstantArrayType(Ty);
          Dims.push_back(ArTy->getSize().getZExtValue());
          // int[0] -> (int [0]) {}
          // int [2][0]  -> (int [2][0]) {}
          // Obj o[3][0][2]  -> (Obj [3][0][2]) {}
          // sizeof all of those is 0
          if (Dims.back() == 0)
            return valuePrinterInternal::kEmptyCollection;

          const clang::Type* Elem = Ty->getArrayElementTypeNoTypeQual();
          Ty = QualType(Elem, 0);
        } while (Ty->isConstantArrayType());

        if (const BuiltinType* BT =
                dyn_cast<BuiltinType>(Ty->getBaseElementTypeUnsafe())) {
          std::string Str = printBuiltinArray(BT, V, Dims);
          if (!Str.empty())
            return Str;
        }
      }

      Interpreter* Interp = V.getInterpreter();
      if (LLVM_UNLIKELY(!Interp->getSema().getLangOpts().CPlusPlus))
        return cling_PrintValue(V);

      // Include "RuntimePrintValue.h" only on the first printing.
      // This keeps the interpreter lightweight and reduces the startup time.
      // But user can undo past the transaction that invoked this, so whether
      // we are first or not is known by the interpreter.
      //
      // Additionally the user could have included RuntimePrintValue.h before
      // this code is run, so if there is no print transaction, check if
      // CLING_RUNTIME_PRINT_VALUE_H is defined.
      // FIXME: Relying on this macro isn't the best, but what's another way?

      Interpreter::TransactionMerge M(Interp, true);
      const Transaction*& T = Interp->printValueTransaction();
      if (!T && !Interp->getMacro("CLING_RUNTIME_PRINT_VALUE_H")) {
        // DiagnosticErrorTrap Trap(Interp->getSema().getDiagnostics());
        Interp->declare("#include \"cling/Interpreter/RuntimePrintValue.h\"",
                        const_cast<Transaction**>(&T));

        if (!T) {
          // Should also check T->getIssuedDiags() == Transaction::kErrors)?
          
          // It's redundant, but nicer to see the error at the bottom.
          // if (!Trap.hasErrorOccurred())
          cling::errs() << "RuntimePrintValue.h could not be loaded.\n";
          return valuePrinterInternal::kUndefined;
        }
      }
      return printUnpackedClingValue(V);
    }
  } // end namespace valuePrinterInternal
} // end namespace cling
