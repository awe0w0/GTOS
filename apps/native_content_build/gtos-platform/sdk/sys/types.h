#ifndef GTOS_CONTENT_SYS_TYPES_H
#define GTOS_CONTENT_SYS_TYPES_H

#if !defined(__GTOS__) || !defined(__i386__) || defined(__linux__) || defined(_WIN32) || defined(__APPLE__)
#error The native Content scalar ABI requires GTOS IA32
#endif

#include_next <sys/types.h>
#include <stdint.h>

// Scalar aliases used by the file subset and tracing interfaces. They do not
// supply Unix users, a permission database or POSIX permission enforcement.
typedef uint32_t uid_t;
typedef uint32_t mode_t;

#endif
