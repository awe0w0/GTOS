#ifndef GTOS_C_FILE_SDK_ERRNO_H
#define GTOS_C_FILE_SDK_ERRNO_H
#include_next <errno.h>
#if !defined(__GTOS__)
#error Native file errno requires GTOS
#endif
#ifndef ENAMETOOLONG
#define ENAMETOOLONG 9944
#endif
#ifndef ENOTEMPTY
#define ENOTEMPTY 9943
#endif
#endif
