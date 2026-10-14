#include "src/__support/File/file.h"
#include "src/__support/common.h"
#include "src/__support/libc_errno.h"
#include "src/__support/macros/config.h"
#include "hdr/types/FILE.h"
#include "hdr/types/wint_t.h"

namespace LIBC_NAMESPACE_DECL {

wint_t fputwc(wchar_t wc, ::FILE *stream);
wint_t getwc(::FILE *stream);
wint_t ungetwc(wint_t wc, ::FILE *stream);

LLVM_LIBC_FUNCTION(wint_t, fputwc, (wchar_t wc, ::FILE *stream)) {
  auto result = reinterpret_cast<File *>(stream)->write(&wc, 1);
  if (result.has_error())
    libc_errno = result.error;
  return result.value == 1 ? static_cast<wint_t>(wc) : WEOF;
}

LLVM_LIBC_FUNCTION(wint_t, getwc, (::FILE *stream)) {
  wchar_t wc;
  auto result = reinterpret_cast<File *>(stream)->read(&wc, 1);
  if (result.has_error())
    libc_errno = result.error;
  return result.value == 1 ? static_cast<wint_t>(wc) : WEOF;
}

LLVM_LIBC_FUNCTION(wint_t, ungetwc, (wint_t wc, ::FILE *stream)) {
  auto result = reinterpret_cast<File *>(stream)->ungetwc(wc);
  if (!result.has_value()) {
    libc_errno = result.error();
    return WEOF;
  }
  return result.value();
}

} // namespace LIBC_NAMESPACE_DECL
