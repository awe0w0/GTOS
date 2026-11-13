#ifndef GTOS_SDK_FNMATCH_H_
#define GTOS_SDK_FNMATCH_H_

#include <llvm-libc-macros/fnmatch-macros.h>

#ifdef __cplusplus
extern "C" {
#endif
int fnmatch(const char* pattern, const char* string, int flags);
#ifdef __cplusplus
}
#endif

#endif  // GTOS_SDK_FNMATCH_H_
