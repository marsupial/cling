//--------------------------------------------------------------------*- C++ -*-
// CLING - the C++ LLVM-based InterpreterG :)
// author:  Boris Perovic <boris.perovic@cern.ch>
// author:  Danilo Piparo <danilo.piparo@cern.ch>
//
// This file is dual-licensed: you can choose to license it under the University
// of Illinois Open Source License or the GNU Lesser General Public License. See
// LICENSE.TXT for details.
//------------------------------------------------------------------------------
#ifndef CLING_RUNTIME_PRINT_VALUE_H
#define CLING_RUNTIME_PRINT_VALUE_H

#if !defined(__CLING__)
#error "This file must not be included by compiled programs."
#endif

#include <string>
#if __cplusplus >= 201103L
  #include <tuple>
#endif

namespace cling {

  class Value;

  // General fallback - prints the address
  std::string printValue(const void *ptr);

  // void pointer
  std::string printValue(const void **ptr);

  // Bool
  std::string printValue(const bool *val);

  // Chars
  std::string printValue(const char *val);

  std::string printValue(const signed char *val);

  std::string printValue(const unsigned char *val);

  // Ints
  std::string printValue(const short *val);

  std::string printValue(const unsigned short *val);

  std::string printValue(const int *val);

  std::string printValue(const unsigned int *val);

  std::string printValue(const long *val);

  std::string printValue(const unsigned long *val);

  std::string printValue(const long long *val);

  std::string printValue(const unsigned long long *val);

  // Reals
  std::string printValue(const float *val);

  std::string printValue(const double *val);

  std::string printValue(const long double *val);

  // Char pointers
  std::string printValue(const char *const *val);

  std::string printValue(const char **val);

  // std::string
  std::string printValue(const std::string *val);

  // cling::Value
  std::string printValue(const Value *value);

  // Arrays
  template<typename T, size_t N>
  std::string printValue(const T (*obj)[N]) {
    std::string str = "{ ";

    for (int i = 0; i < N; ++i) {
      str += printValue(*obj + i);
      if (i < N - 1) str += ", ";
    }

    return str + " }";
  }

#if __cplusplus >= 201103L

  // Collections internal declaration
  namespace collectionPrinterInternal {
    // Maps declaration
    template<typename CollectionType>
    auto printValue_impl(const CollectionType *obj, short)
      -> decltype(
      ++(obj->begin()), obj->end(),
        obj->begin()->first, obj->begin()->second,
        std::string());

    // Vector, set, deque etc. declaration
    template<typename CollectionType>
    auto printValue_impl(const CollectionType *obj, int)
      -> decltype(
      ++(obj->begin()), obj->end(),
        *(obj->begin()),
        std::string());

    // No general fallback anymore here, void* overload used for that now
  }

  // Collections
  template<typename CollectionType>
  auto printValue(const CollectionType *obj)
  -> decltype(collectionPrinterInternal::printValue_impl(obj, 0), std::string())
  {
    return collectionPrinterInternal::printValue_impl(obj, (short)0);  // short -> int -> long = priority order
  }

  // Collections internal
  namespace collectionPrinterInternal {
    // Maps
    template<typename CollectionType>
    auto printValue_impl(const CollectionType *obj, short)
    -> decltype(
    ++(obj->begin()), obj->end(),
        obj->begin()->first, obj->begin()->second,
        std::string())
    {
      std::string str = "{ ";

      auto iter = obj->begin();
      auto iterEnd = obj->end();
      while (iter != iterEnd) {
        str += printValue(&iter->first);
        str += " => ";
        str += printValue(&iter->second);
        ++iter;
        if (iter != iterEnd) {
          str += ", ";
        }
      }

      return str + " }";
    }

    // Vector, set, deque etc.
    template<typename CollectionType>
    auto printValue_impl(const CollectionType *obj, int)
    -> decltype(
    ++(obj->begin()), obj->end(),
        *(obj->begin()),  &(*(obj->begin())),
        std::string())
    {
      std::string str = "{ ";

      auto iter = obj->begin();
      auto iterEnd = obj->end();
      while (iter != iterEnd) {
        str += printValue(&(*iter));
        ++iter;
        if (iter != iterEnd) {
          str += ", ";
        }
      }

      return str + " }";
    }

    // Vector, set, deque etc.
    template<typename CollectionType>
    auto printValue_impl(const CollectionType *obj, int)
    -> decltype(
    ++(obj->begin()), obj->end(),
        *(obj->begin()),
        std::string())
     {
        std::string str = "{ ";

        auto iter = obj->begin();
        auto iterEnd = obj->end();
        while (iter != iterEnd) {
           const auto value = (*iter);
           str += printValue(&value);
           ++iter;
           if (iter != iterEnd) {
              str += ", ";
           }
        }

        return str + " }";
     }

  }

  // Tuples
  template <class... ARGS>
  std::string printValue(std::tuple<ARGS...> *);

  namespace collectionPrinterInternal {

    template <std::size_t N>
    const char *GetCommaOrEmpty()
    {
      static const auto comma = ", ";
      return comma;
    }

    template <>
    const char *GetCommaOrEmpty<0>()
    {
      static const auto empty = "";
      return empty;
    }

    // We loop at compile time from element 0 to element TUPLE_SIZE - 1
    // of the tuple. The running index is N which has as initial value
    // TUPLE_SIZE. We can therefore stop the iteration and account for the
    // empty tuple case with one single specialisation.
    template <class TUPLE,
              std::size_t N = std::tuple_size<TUPLE>(),
              std::size_t TUPLE_SIZE = std::tuple_size<TUPLE>()>
    struct tuplePrinter {
      static std::string print(TUPLE *t)
      {
        constexpr std::size_t elementNumber = TUPLE_SIZE - N;
        using Element_t = decltype(std::get<elementNumber>(*t));
        std::string ret;
        ret += GetCommaOrEmpty<elementNumber>();
        ret += cling::printValue(&std::get<elementNumber>(*t));
        // If N+1 is not smaller than the size of the tuple,
        // reroute the call to the printing function to the
        // no-op specialisation to stop recursion.
        constexpr std::size_t Nm1 = N - 1;
        ret += tuplePrinter<TUPLE, Nm1>::print((TUPLE *)t);
        return ret;
      }
    };

    // Special case: no op if last element reached or empty tuple
    template <class TUPLE, std::size_t TUPLE_SIZE>
    struct tuplePrinter<TUPLE, 0, TUPLE_SIZE>
    {
      static std::string print(TUPLE *t) {return "";}
    };

    template <class T>
    std::string tuplePairPrintValue(T *val)
    {
      std::string ret("{ ");
      ret += collectionPrinterInternal::tuplePrinter<T>::print(val);
      ret += " }";
      return ret;
    }
  }

  template <class... ARGS>
  std::string printValue(std::tuple<ARGS...> *val)
  {
    using T = std::tuple<ARGS...>;
    return collectionPrinterInternal::tuplePairPrintValue<T>(val);
  }

  template <class... ARGS>
  std::string printValue(std::pair<ARGS...> *val)
  {
    using T = std::pair<ARGS...>;
    return collectionPrinterInternal::tuplePairPrintValue<T>(val);
  }

#else // __cplusplus < 201103L (no C++11 features)

  // std::string
  std::string printValue(std::string *val) {
    return printValue(const_cast<const std::string*>(val));
  }

  template <class T1, class T2>
  std::string printValue(const std::pair<T1, T2> *val) {
    return "{ " + printValue(&val->first) + ", " +
                  printValue(&val->second) + " }";
  }

  #define CLING_HAS_MEM_FUNC(func, name)                                      \
    template<typename T, typename Sign>                                       \
      struct name {                                                           \
      typedef char yes[1];                                                    \
      typedef char no [2];                                                    \
      template <typename U, U> struct type_check;                             \
      template <typename _1> static yes &chk(type_check<Sign, &_1::func > *); \
      template <typename   > static no  &chk(...);                            \
      static bool const value = sizeof(chk<T>(0)) == sizeof(yes);             \
    }

  namespace {
    template<bool C, typename T = void>
    struct enable_if { typedef T type; };

    // object has iterator type and begin
    CLING_HAS_MEM_FUNC(begin, has_begin);
    CLING_HAS_MEM_FUNC(end,   has_end);

    // const variant: const_iterator & cbegin
    CLING_HAS_MEM_FUNC(cbegin, has_cbegin);
    CLING_HAS_MEM_FUNC(cend,   has_cend);

    template <class T1, class T2>
    std::string printContainterValue(const std::pair<T1, T2>& val) {
      return cling::printValue(&val.first) + " => " +
               cling::printValue(&val.second);
    }
    template <class T>
    std::string printContainterValue(const T& val) {
      return cling::printValue(&val);
    }
  }

  template <typename T> typename enable_if<
    (has_begin<T,typename T::iterator (T::*)()>::value &&
     has_end<T,typename T::iterator (T::*)()>::value),
    std::string >::type
  printValue(T* obj) {
    std::string str = "{ ";

    typename T::iterator iter = obj->begin(), end = obj->end();
    while (iter != end) {
      str += printContainterValue(*iter);
      if (++iter != end) {
        str += ", ";
      }
    }
    return str + " }";
  }

  template <typename T> typename enable_if<
    (has_cbegin<T,typename T::const_iterator (T::*)() const>::value &&
     has_cend<T,typename T::const_iterator (T::*)() const>::value),
    std::string >::type
  printValue(const T* obj) {
    std::string str = "{ ";

    typename T::const_iterator iter = obj->cbegin(), end = obj->cend();
    while (iter != end) {
      str += printContainterValue(*iter);
      if (++iter != end) {
        str += ", ";
      }
    }
    return str + " }";
  }
  #undef CLING_HAS_MEM_FUNC

#endif // __cplusplus >= 201103L (C++11)

}

#endif
