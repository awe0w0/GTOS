#include "src/stdio/stdin.h"
#include "src/stdio/stdout.h"
#include "src/stdio/stderr.h"
// Regular-file qualification only. Console/input streams remain unimplemented.
namespace LIBC_NAMESPACE_DECL {
::FILE *stdin = nullptr;
::FILE *stdout = nullptr;
::FILE *stderr = nullptr;
}
