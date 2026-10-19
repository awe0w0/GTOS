/* A real i686 C++ consumer of the GTOS address representation, in include order A:
 * the canonical SDK headers first, then the umbrella.
 *
 * The point of this file is that the ten canonical llvm-libc type headers are
 * INCLUDED, not restated. Two independent proofs:
 *
 *   1. after including <net/address_types.h>, all ten canonical header guards are
 *      defined - which can only be true if those headers were actually opened;
 *   2. socklen_t is the same C++ type as `unsigned long` and a different type
 *      from `unsigned int`, even though both are 4 bytes on i386.
 *
 * The second one is why the previous revision's `typedef unsigned int socklen_t`
 * was wrong: `sizeof` alone cannot see the difference.
 */
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

#include <net/address_types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <stddef.h>

#include "build/build_config.h"

// ---- 1. the canonical headers were opened, not rewritten ---------------------
#ifndef LLVM_LIBC_TYPES_SOCKLEN_T_H
#error socklen_t did not come from the canonical SDK header
#endif
#ifndef LLVM_LIBC_TYPES_SA_FAMILY_T_H
#error sa_family_t did not come from the canonical SDK header
#endif
#ifndef LLVM_LIBC_TYPES_IN_ADDR_T_H
#error in_addr_t did not come from the canonical SDK header
#endif
#ifndef LLVM_LIBC_TYPES_IN_PORT_T_H
#error in_port_t did not come from the canonical SDK header
#endif
#ifndef LLVM_LIBC_TYPES_STRUCT_SOCKADDR_H
#error struct sockaddr did not come from the canonical SDK header
#endif
#ifndef LLVM_LIBC_TYPES_STRUCT_IN_ADDR_H
#error struct in_addr did not come from the canonical SDK header
#endif
#ifndef LLVM_LIBC_TYPES_STRUCT_IN6_ADDR_H
#error struct in6_addr did not come from the canonical SDK header
#endif
#ifndef LLVM_LIBC_TYPES_STRUCT_SOCKADDR_IN_H
#error struct sockaddr_in did not come from the canonical SDK header
#endif
#ifndef LLVM_LIBC_TYPES_STRUCT_SOCKADDR_IN6_H
#error struct sockaddr_in6 did not come from the canonical SDK header
#endif
#ifndef LLVM_LIBC_TYPES_STRUCT_SOCKADDR_STORAGE_H
#error struct sockaddr_storage did not come from the canonical SDK header
#endif

namespace {

template <typename A, typename B>
struct Same {
    static const bool value = false;
};
template <typename A>
struct Same<A, A> {
    static const bool value = true;
};

// ---- 2. exact type identity, not merely equal width --------------------------
static_assert(Same<socklen_t, unsigned long>::value,
              "socklen_t must be the canonical unsigned long");
static_assert(!Same<socklen_t, unsigned int>::value,
              "socklen_t must not be unsigned int even though both are 4 bytes");
static_assert(Same<sa_family_t, unsigned short>::value,
              "sa_family_t must be the canonical unsigned short");
static_assert(Same<in_addr_t, unsigned int>::value,
              "in_addr_t must be the canonical 32-bit unsigned type");
static_assert(Same<in_port_t, unsigned short>::value,
              "in_port_t must be the canonical 16-bit unsigned type");

// ---- 3. the i386 layout the consumers actually index -------------------------
static_assert(sizeof(socklen_t) == 4, "socklen_t is 4 bytes on i386");
static_assert(alignof(socklen_t) == 4, "socklen_t is 4-aligned on i386");
static_assert(sizeof(sa_family_t) == 2, "sa_family_t is 2 bytes");
static_assert(alignof(sa_family_t) == 2, "sa_family_t is 2-aligned");
static_assert(sizeof(in_addr_t) == 4, "in_addr_t is 4 bytes");
static_assert(sizeof(in_port_t) == 2, "in_port_t is 2 bytes");
static_assert(sizeof(struct in_addr) == 4, "struct in_addr is one 32-bit word");
static_assert(alignof(struct in_addr) == 4, "struct in_addr is 4-aligned");
static_assert(sizeof(struct in6_addr) == 16, "struct in6_addr is 16 bytes");
static_assert(alignof(struct in6_addr) == 4, "struct in6_addr is 4-aligned");

// The canonical sockaddr is family + sa_data[1]: 4 bytes, not the 16 a host
// header would suggest. Getting this wrong would size every buffer incorrectly.
static_assert(sizeof(struct sockaddr) == 4, "the canonical sockaddr is 4 bytes");
static_assert(alignof(struct sockaddr) == 2, "the canonical sockaddr is 2-aligned");

static_assert(sizeof(struct sockaddr_in) == 16, "sockaddr_in is 16 bytes");
static_assert(alignof(struct sockaddr_in) == 4, "sockaddr_in is 4-aligned");
static_assert(sizeof(struct sockaddr_in6) == 28, "sockaddr_in6 is 28 bytes");
static_assert(alignof(struct sockaddr_in6) == 4, "sockaddr_in6 is 4-aligned");
static_assert(sizeof(struct sockaddr_storage) == 128,
              "sockaddr_storage must stay 128 bytes");
static_assert(alignof(struct sockaddr_storage) == 4,
              "sockaddr_storage is 4-aligned");

static_assert(offsetof(struct sockaddr, sa_family) == 0,
              "sa_family must be the first field");
static_assert(offsetof(struct sockaddr, sa_data) == 2, "sa_data follows the family");
static_assert(offsetof(struct sockaddr_in, sin_family) == 0,
              "sin_family must be first");
static_assert(offsetof(struct sockaddr_in, sin_port) == 2, "sin_port is at 2");
static_assert(offsetof(struct sockaddr_in, sin_addr) == 4, "sin_addr is at 4");
static_assert(offsetof(struct sockaddr_in, sin_zero) == 8, "sin_zero is at 8");
static_assert(offsetof(struct sockaddr_in6, sin6_family) == 0,
              "sin6_family must be first");
static_assert(offsetof(struct sockaddr_in6, sin6_port) == 2, "sin6_port is at 2");
static_assert(offsetof(struct sockaddr_in6, sin6_flowinfo) == 4,
              "sin6_flowinfo is at 4");
static_assert(offsetof(struct sockaddr_in6, sin6_addr) == 8, "sin6_addr is at 8");
static_assert(offsetof(struct sockaddr_in6, sin6_scope_id) == 24,
              "sin6_scope_id is at 24");
static_assert(offsetof(struct sockaddr_storage, ss_family) == 0,
              "ss_family must be first, so its address is the sockaddr address");
static_assert(offsetof(struct sockaddr_storage, __ss_align) == 124,
              "__ss_align closes the 128-byte block");

// ---- 4. the three constants this revision selected ---------------------------
static_assert(AF_UNSPEC == 0, "AF_UNSPEC is 0");
static_assert(AF_INET == 2, "AF_INET is 2");
static_assert(AF_INET6 == 10, "AF_INET6 is 10");

// ---- 5. the identity is exactly GTOS, through the real build config ----------
static_assert(BUILDFLAG(IS_GTOS) == 1, "IS_GTOS must be 1");
static_assert(BUILDFLAG(IS_LINUX) == 0, "GTOS must not claim Linux");
static_assert(BUILDFLAG(IS_POSIX) == 0, "GTOS is not a POSIX target here");
static_assert(BUILDFLAG(IS_FUCHSIA) == 0, "GTOS is not Fuchsia");
static_assert(BUILDFLAG(IS_WIN) == 0, "GTOS must not claim Windows");
static_assert(BUILDFLAG(IS_OZONE) == 0, "GTOS is not Ozone");

// ---- 6. the conversion the consumers actually do ----------------------------
// IPEndPoint::FromSockAddr reads `sa_family` first and compares it against these
// three values. That is a defined-behaviour read of a 2-byte field at offset 0,
// which is what the storage reinterpret_cast in SockaddrStorage::addr() relies
// on. Nothing here calls a socket.
int gtos_address_types_probe(void) {
    sockaddr_storage storage = {};
    storage.ss_family = AF_INET;
    struct sockaddr* generic = reinterpret_cast<struct sockaddr*>(&storage);
    struct sockaddr_in* v4 = reinterpret_cast<struct sockaddr_in*>(&storage);
    v4->sin_family = AF_INET;
    v4->sin_port = 0x5000;  // network order for port 80
    v4->sin_addr.s_addr = 0x0100007f;
    int result = 0;
    if (generic->sa_family == AF_INET) {
        result += 1;
    }
    if (v4->sin_addr.s_addr == 0x0100007f) {
        result += 2;
    }
    // The address bytes and the port must not have been reordered by the
    // representation: this layer stores network order and converts nothing.
    const unsigned char* raw = reinterpret_cast<const unsigned char*>(&storage);
    if (raw[0] == 2 && raw[1] == 0 && raw[2] == 0x00 && raw[3] == 0x50) {
        result += 4;
    }
    return result;
}

}  // namespace

extern "C" int gtos_address_types_entry(void) {
    return gtos_address_types_probe();
}
