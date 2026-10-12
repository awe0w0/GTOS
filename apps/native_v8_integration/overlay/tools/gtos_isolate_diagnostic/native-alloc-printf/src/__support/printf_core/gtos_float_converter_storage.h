// Native storage for the pinned full-precision converter.
#ifndef GTOS_FLOAT_CONVERTER_STORAGE_H
#define GTOS_FLOAT_CONVERTER_STORAGE_H
#include "hdr/func/malloc.h"
#include "hdr/func/free.h"
#include "src/__support/CPP/new.h"

namespace LIBC_NAMESPACE_DECL {
namespace printf_core {
template <typename T, bool Large = (sizeof(FloatToString<T>) > 1024)>
class GtosFloatConverterStorage {
  FloatToString<T> converter;
public:
  LIBC_INLINE explicit GtosFloatConverterStorage(T value) : converter(value) {}
  LIBC_INLINE bool valid() const { return true; }
  LIBC_INLINE FloatToString<T> &get() { return converter; }
};

template <typename T> class GtosFloatConverterStorage<T, true> {
  using Converter = FloatToString<T>;
  Converter *converter;
public:
  LIBC_INLINE explicit GtosFloatConverterStorage(T value)
      : converter(static_cast<Converter *>(malloc(sizeof(Converter)))) {
    if (converter)
      new (converter) Converter(value);
  }
  GtosFloatConverterStorage(const GtosFloatConverterStorage &) = delete;
  GtosFloatConverterStorage &operator=(const GtosFloatConverterStorage &) = delete;
  LIBC_INLINE ~GtosFloatConverterStorage() {
    if (converter) {
      converter->~Converter();
      free(converter);
    }
  }
  LIBC_INLINE bool valid() const { return converter != nullptr; }
  LIBC_INLINE Converter &get() { return *converter; }
};
} // namespace printf_core
} // namespace LIBC_NAMESPACE_DECL
#endif
