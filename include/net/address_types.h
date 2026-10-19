#ifndef __GTOS__NET__ADDRESS_TYPES_H
#define __GTOS__NET__ADDRESS_TYPES_H
// The GTOS user-space address representation.
//
// This header adds NO type of its own. Every type below comes from the original
// native SDK's canonical llvm-libc headers, included by their real names, so the
// typedef spelling, the struct tags, the anonymous union in in6_addr, the
// may_alias attributes and the header guards are the ones that already exist.
// Writing a second `typedef unsigned int socklen_t` here would be wrong even
// though it is the same width on i386: it is a different C/C++ type, and the two
// cannot coexist in one translation unit.
//
// What this header does add is exactly three integer constants, AF_UNSPEC,
// AF_INET and AF_INET6, selected by this revision as the GTOS user-space address
// family values. They are a user-space representation only. They are not a
// kernel socket contract, nothing here is sent to or accepted by the kernel, and
// there is no socket(), connect(), send(), recv(), getaddrinfo() or
// if_nametoindex() anywhere in this layer.
//
// The Linux macro table is deliberately not included and __linux__/__unix__ are
// never defined to reach it. Those macros are reachable only under #ifdef
// __linux__, and claiming that identity to obtain a constant is a lie about the
// platform, not a portability shim.

// --- GTOS native identity gate ------------------------------------------------
// This layer describes the GTOS i386 ABI. It refuses to be compiled under any
// other identity, and it refuses a host OS smuggled in alongside GTOS.
#if !defined(__GTOS__)
#error "net/address_types.h is the GTOS address representation; build with -D__GTOS__=1"
#endif
#if !defined(__i386__)
#error "net/address_types.h describes the GTOS i386 ABI; build with --target=i686-unknown-none-elf"
#endif
#if defined(_WIN32) || defined(__linux__) || defined(__unix__) || defined(unix) || \
    defined(__APPLE__) || defined(__FreeBSD__)
#error "net/address_types.h refuses a host OS identity: GTOS has its own ABI"
#endif
#if defined(OS_LINUX) || defined(OS_CHROMEOS) || defined(OS_ANDROID) || \
    defined(OS_MAC) || defined(OS_WIN) || defined(OS_POSIX) || defined(OS_FUCHSIA)
#error "net/address_types.h refuses a second target OS alongside OS_GTOS"
#endif

// --- canonical types, from the original native SDK ----------------------------
// Ten headers, all under llvm-libc-types/ in the bound SDK. They are included,
// never copied. Their own relative includes (../llvm-libc-macros/stdint-macros.h
// and their siblings) resolve inside that same SDK.
#include <llvm-libc-types/socklen_t.h>
#include <llvm-libc-types/sa_family_t.h>
#include <llvm-libc-types/in_addr_t.h>
#include <llvm-libc-types/in_port_t.h>
#include <llvm-libc-types/struct_sockaddr.h>
#include <llvm-libc-types/struct_in_addr.h>
#include <llvm-libc-types/struct_in6_addr.h>
#include <llvm-libc-types/struct_sockaddr_in.h>
#include <llvm-libc-types/struct_sockaddr_in6.h>
#include <llvm-libc-types/struct_sockaddr_storage.h>

// --- the three family constants this revision selects -------------------------
// A same-named definition from anywhere else is checked, never silently
// overridden: an equal value is accepted, a different value is a hard error.
#if defined(AF_UNSPEC)
#if AF_UNSPEC != 0
#error "AF_UNSPEC is already defined with a different value"
#endif
#else
#define AF_UNSPEC 0
#endif

#if defined(AF_INET)
#if AF_INET != 2
#error "AF_INET is already defined with a different value"
#endif
#else
#define AF_INET 2
#endif

#if defined(AF_INET6)
#if AF_INET6 != 10
#error "AF_INET6 is already defined with a different value"
#endif
#else
#define AF_INET6 10
#endif

// The three must stay distinct, and none of them may be confused with the
// others: a switch on the family would silently collapse if they did.
#if defined(__cplusplus)
static_assert(AF_UNSPEC != AF_INET, "AF_UNSPEC and AF_INET must differ");
static_assert(AF_UNSPEC != AF_INET6, "AF_UNSPEC and AF_INET6 must differ");
static_assert(AF_INET != AF_INET6, "AF_INET and AF_INET6 must differ");
// The canonical socklen_t is unsigned long. Same width on i386 is not the same
// type, so both halves of this are asserted.
static_assert(sizeof(socklen_t) == sizeof(unsigned long),
              "socklen_t must be the canonical 32-bit unsigned long");
static_assert(sizeof(struct sockaddr) == 4,
              "the canonical sockaddr is family + sa_data[1], not a 16-byte host shape");
static_assert(sizeof(struct sockaddr_storage) == 128,
              "sockaddr_storage must stay 128 bytes");
#else
_Static_assert(AF_UNSPEC != AF_INET, "AF_UNSPEC and AF_INET must differ");
_Static_assert(AF_UNSPEC != AF_INET6, "AF_UNSPEC and AF_INET6 must differ");
_Static_assert(AF_INET != AF_INET6, "AF_INET and AF_INET6 must differ");
_Static_assert(sizeof(socklen_t) == sizeof(unsigned long),
               "socklen_t must be the canonical 32-bit unsigned long");
_Static_assert(sizeof(struct sockaddr) == 4,
               "the canonical sockaddr is family + sa_data[1], not a 16-byte host shape");
_Static_assert(sizeof(struct sockaddr_storage) == 128,
               "sockaddr_storage must stay 128 bytes");
#endif

#endif  // __GTOS__NET__ADDRESS_TYPES_H
