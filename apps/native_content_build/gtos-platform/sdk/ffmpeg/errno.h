#ifndef TOOLS_GTOS_NATIVE_SDK_FFMPEG_ERRNO_H_
#define TOOLS_GTOS_NATIVE_SDK_FFMPEG_ERRNO_H_

#include_next <errno.h>

#ifndef EDEADLK
#define EDEADLK 35
#endif
#ifndef ENAMETOOLONG
#define ENAMETOOLONG 36
#endif
#ifndef ENOLCK
#define ENOLCK 37
#endif
#ifndef ENOSYS
#define ENOSYS 38
#endif
#ifndef ENOTEMPTY
#define ENOTEMPTY 39
#endif
#ifndef ENOPROTOOPT
#define ENOPROTOOPT 92
#endif
#ifndef ENOTSUP
#define ENOTSUP 95
#endif

#endif  // TOOLS_GTOS_NATIVE_SDK_FFMPEG_ERRNO_H_
