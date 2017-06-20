//------------------------------------------------------------------------------
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Axel Naumann <axel@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------

#include "cling/Interpreter/Value.h"

#include "cling/Interpreter/Interpreter.h"
#include "cling/Interpreter/Transaction.h"
#include "cling/Utils/AST.h"
#include "cling/Utils/Casting.h"
#include "cling/Utils/Output.h"
#include "cling/Utils/UTF8.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/CanonicalType.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Type.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Overload.h"
#include "clang/Sema/Sema.h"

#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_os_ostream.h"

#include <stddef.h> // offsetof

namespace {

  ///\brief The allocation starts with this layout; it is followed by the
  ///  value's object at m_Payload. This class does not inherit from
  ///  llvm::RefCountedBase because deallocation cannot use this type but must
  ///  free the character array.

  class AllocatedValue {
  public:
    typedef void (*DtorFunc_t)(void*);

  private:
    ///\brief The destructor function.
    DtorFunc_t m_DtorFunc;

    ///\brief The size of the allocation (for arrays)
    size_t m_AllocSize;

    ///\brief The number of elements in the array
    size_t m_NElements;

    ///\brief The reference count - once 0, this object will be deallocated.
    size_t m_RefCnt;

  public:
    ///\brief Initialize the storage management part of the allocated object.
    ///  The allocator is referencing it, thus initialize m_RefCnt with 1.
    ///\param [in] dtorFunc - the function to be called before deallocation.
    AllocatedValue(void* dtorFunc, size_t allocSize, size_t nElements) :
      m_DtorFunc(cling::utils::VoidToFunctionPtr<DtorFunc_t>(dtorFunc)),
      m_AllocSize(allocSize), m_NElements(nElements), m_RefCnt(1)
    {}

    static constexpr unsigned getPayloadOffset() {
      static_assert(std::is_standard_layout<AllocatedValue>::value,
                    "Cannot use offsetof");
      static_assert(((offsetof(AllocatedValue, m_RefCnt) + sizeof(m_RefCnt)) %
                            sizeof(size_t)) == 0,
                    "Buffer may not be machine aligned");
      return offsetof(AllocatedValue, m_RefCnt) + sizeof(m_RefCnt);
    }

    char* getPayload() {
      return reinterpret_cast<char*>(&m_RefCnt) + sizeof(m_RefCnt);
    }

    static AllocatedValue* getFromPayload(void* payload) {
      return
        reinterpret_cast<AllocatedValue*>((char*)payload - getPayloadOffset());
    }

    void Retain() { ++m_RefCnt; }

    ///\brief This object must be allocated as a char array. Deallocate it as
    ///   such.
    void Release() {
      assert (m_RefCnt > 0 && "Reference count is already zero.");
      if (--m_RefCnt == 0) {
        if (m_DtorFunc) {
          assert(m_NElements && "No elements!");
          char* Payload = getPayload();
          const auto Skip = m_AllocSize / m_NElements;
          while (m_NElements-- != 0)
            (*m_DtorFunc)(Payload + m_NElements * Skip);
        }
        this->~AllocatedValue();
        delete [] (char*)this;
      }
    }
  };
}

namespace cling {

  Value::Value(const Value& other):
    m_Storage(other.m_Storage), m_StorageType(other.m_StorageType),
    m_Type(other.m_Type), m_Interpreter(other.m_Interpreter) {
    if (other.needsManagedAllocation())
      AllocatedValue::getFromPayload(m_Storage.m_Ptr)->Retain();
  }

  Value::Value(clang::QualType clangTy, Interpreter& Interp):
    m_StorageType(determineStorageType(clangTy)),
    m_Type(clangTy.getAsOpaquePtr()),
    m_Interpreter(&Interp) {
    if (needsManagedAllocation())
      ManagedAllocate();
  }

  Value& Value::operator =(const Value& other) {
    // Release old value.
    if (needsManagedAllocation())
      AllocatedValue::getFromPayload(m_Storage.m_Ptr)->Release();

    // Retain new one.
    m_Type = other.m_Type;
    m_Storage = other.m_Storage;
    m_StorageType = other.m_StorageType;
    m_Interpreter = other.m_Interpreter;
    if (needsManagedAllocation())
      AllocatedValue::getFromPayload(m_Storage.m_Ptr)->Retain();
    return *this;
  }

  Value& Value::operator =(Value&& other) {
    // Release old value.
    if (needsManagedAllocation())
      AllocatedValue::getFromPayload(m_Storage.m_Ptr)->Release();

    // Move new one.
    m_Type = other.m_Type;
    m_Storage = other.m_Storage;
    m_StorageType = other.m_StorageType;
    m_Interpreter = other.m_Interpreter;
    // Invalidate other so it will not release.
    other.m_StorageType = kUnsupportedType;

    return *this;
  }

  Value::~Value() {
    if (needsManagedAllocation())
      AllocatedValue::getFromPayload(m_Storage.m_Ptr)->Release();
  }

  clang::QualType Value::getType() const {
    return clang::QualType::getFromOpaquePtr(m_Type);
  }

  clang::ASTContext& Value::getASTContext() const {
    return m_Interpreter->get<clang::ASTContext>();
  }

  bool Value::isValid() const { return !getType().isNull(); }

  bool Value::isVoid() const {
    const clang::ASTContext& Ctx = getASTContext();
    return isValid() && Ctx.hasSameType(getType(), Ctx.VoidTy);
  }

  size_t Value::GetNumberOfElements() const {
    if (const clang::ConstantArrayType* ArrTy
        = llvm::dyn_cast<clang::ConstantArrayType>(getType())) {
      llvm::APInt arrSize(sizeof(size_t)*8, 1);
      do {
        arrSize *= ArrTy->getSize();
        ArrTy = llvm::dyn_cast<clang::ConstantArrayType>(ArrTy->getElementType()
                                                         .getTypePtr());
      } while (ArrTy);
      return static_cast<size_t>(arrSize.getZExtValue());
    }
    return 1;
  }

  Value::EStorageType Value::determineStorageType(clang::QualType QT) {
    const clang::Type* desugCanon = QT.getCanonicalType().getTypePtr();
    if (desugCanon->isSignedIntegerOrEnumerationType())
      return kSignedIntegerOrEnumerationType;
    else if (desugCanon->isUnsignedIntegerOrEnumerationType())
      return kUnsignedIntegerOrEnumerationType;
    else if (desugCanon->isRealFloatingType()) {
      const clang::BuiltinType* BT = desugCanon->getAs<clang::BuiltinType>();
      if (BT->getKind() == clang::BuiltinType::Double)
        return kDoubleType;
      else if (BT->getKind() == clang::BuiltinType::Float)
        return kFloatType;
      else if (BT->getKind() == clang::BuiltinType::LongDouble)
        return kLongDoubleType;
    } else if (desugCanon->isPointerType() || desugCanon->isObjectType()
               || desugCanon->isReferenceType()) {
      if (desugCanon->isRecordType() || desugCanon->isConstantArrayType()
          || desugCanon->isMemberPointerType())
        return kManagedAllocation;
      return kPointerType;
    }
    return kUnsupportedType;
  }

  void Value::ManagedAllocate() {
    assert(needsManagedAllocation() && "Does not need managed allocation");
    void* dtorFunc = 0;
    clang::QualType DtorType = getType();
    // For arrays we destruct the elements.
    if (const clang::ConstantArrayType* ArrTy
        = llvm::dyn_cast<clang::ConstantArrayType>(DtorType.getTypePtr())) {
      DtorType = ArrTy->getElementType();
    }
    if (const clang::RecordType* RTy = DtorType->getAs<clang::RecordType>())
      dtorFunc = m_Interpreter->compileDtorCallFor(RTy->getDecl());

    const clang::ASTContext& ctx = getASTContext();
    const auto payloadSize = ctx.getTypeSizeInChars(getType()).getQuantity();
    char* alloc = new char[AllocatedValue::getPayloadOffset() + payloadSize];
    AllocatedValue* allocVal = new (alloc) AllocatedValue(dtorFunc, payloadSize,
                                                          GetNumberOfElements());
    m_Storage.m_Ptr = allocVal->getPayload();
  }

  void Value::AssertOnUnsupportedTypeCast() const {
    assert("unsupported type in Value, cannot cast simplistically!" && 0);
  }

  namespace valuePrinterInternal {
    std::string printTypeInternal(const Value& V);
    std::string printValueInternal(const Value& V);
    extern const char* const kInvalid;
  } // end namespace valuePrinterInternal

  void Value::print(llvm::raw_ostream& Out, bool Escape) const {
    if (!m_Interpreter) {
      Out << valuePrinterInternal::kInvalid << "\n";
      return;
    }

    // Save the default type string representation so output can occur as one
    // operation (calling printValueInternal below may write to stderr).
    const std::string Type = valuePrinterInternal::printTypeInternal(*this);

    // Get the value string representation, by printValue() method overloading
    const std::string Val = cling::valuePrinterInternal::printValueInternal(*this);
    if (Escape) {
      const char* Data = Val.data();
      const size_t N = Val.size();
      switch (N ? Data[0] : 0) {
        case 'u': case 'U': case 'L':
          if (N < 3 || Data[1] != '\"')
            break;

          // Unicode string, encoded as Utf-8
        case '\"':
          if (N > 2 && Data[N-1] == '\"') {
            // Drop the terminating " so Utf-8 errors can be detected ("\xeA")
            Out << Type << ' ';
            utils::utf8::EscapeSequence().encode(Data, N-1, Out) << "\"\n";
            return;
          }
        default:
          break;
      }
    }
    Out << Type << ' ' << Val << '\n';
  }

  void Value::dump(bool Escape) const {
    print(cling::outs(), Escape);
  }
} // end namespace cling
