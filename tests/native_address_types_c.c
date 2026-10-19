/* The same representation from C11, in include order B: the umbrella first, the
 * canonical headers second.
 *
 * Order A and order B both have to work. If <sys/socket.h> or <netinet/in.h>
 * forgot to include a canonical header, order A would still pass (the TU included
 * it directly) but order B would fail; if the canonical guards were wrong, order
 * B would produce a redefinition. Neither is allowed.
 *
 * Type identity is checked with _Generic, which is a C11 compile-time check on
 * the exact type, not on the size.
 */
#include <sys/socket.h>
#include <netinet/in.h>
#include <net/address_types.h>

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

#include <stddef.h>

/* 1 = the controlling expression has exactly the named type. */
#define GTOS_IS_TYPE(expression, type) _Generic((expression), type: 1, default: 0)

_Static_assert(GTOS_IS_TYPE((socklen_t)0, unsigned long) == 1,
               "socklen_t must be unsigned long in C too");
_Static_assert(GTOS_IS_TYPE((socklen_t)0, unsigned int) == 0,
               "socklen_t must not be unsigned int");
_Static_assert(GTOS_IS_TYPE((sa_family_t)0, unsigned short) == 1,
               "sa_family_t must be unsigned short");
_Static_assert(GTOS_IS_TYPE((in_addr_t)0, unsigned int) == 1,
               "in_addr_t must be the canonical 32-bit unsigned type");
_Static_assert(GTOS_IS_TYPE((in_port_t)0, unsigned short) == 1,
               "in_port_t must be the canonical 16-bit unsigned type");

/* The layout is the same table as the C++ unit, asserted independently. */
_Static_assert(sizeof(struct sockaddr) == 4, "sockaddr is 4 bytes");
_Static_assert(sizeof(struct sockaddr_in) == 16, "sockaddr_in is 16 bytes");
_Static_assert(sizeof(struct sockaddr_in6) == 28, "sockaddr_in6 is 28 bytes");
_Static_assert(sizeof(struct sockaddr_storage) == 128, "storage is 128 bytes");
_Static_assert(offsetof(struct sockaddr_storage, ss_family) == 0,
               "ss_family is first");
_Static_assert(offsetof(struct sockaddr_in6, sin6_scope_id) == 24,
               "sin6_scope_id is at 24");
_Static_assert(AF_UNSPEC == 0 && AF_INET == 2 && AF_INET6 == 10,
               "the three families keep their selected values");

/* The in6_addr union members all start at offset 0, which is what the
 * byte-copy in ip_address_util.cc relies on. */
_Static_assert(offsetof(struct in6_addr, s6_addr) == 0, "s6_addr is first");
_Static_assert(offsetof(struct in6_addr, s6_addr16) == 0, "s6_addr16 is first");
_Static_assert(offsetof(struct in6_addr, s6_addr32) == 0, "s6_addr32 is first");

int gtos_address_types_c_probe(void) {
    struct sockaddr_storage storage;
    struct sockaddr_in* v4 = (struct sockaddr_in*)(void*)&storage;
    struct sockaddr* generic = (struct sockaddr*)(void*)&storage;
    unsigned char* raw = (unsigned char*)&storage;
    int result = 0;
    unsigned int index;
    for (index = 0; index < sizeof(storage); ++index) {
        raw[index] = 0;
    }
    v4->sin_family = AF_INET6 - 8; /* 2, written through the family constant */
    v4->sin_port = 0x1f90;         /* network order for port 8080 */
    v4->sin_addr.s_addr = 0x0100007f;
    if (generic->sa_family == AF_INET) {
        result += 1;
    }
    if (raw[0] == 2 && raw[1] == 0) {
        result += 2;
    }
    if (raw[2] == 0x90 && raw[3] == 0x1f) {
        result += 4;
    }
    if (raw[4] == 0x7f && raw[5] == 0 && raw[6] == 0 && raw[7] == 1) {
        result += 8;
    }
    return result;
}
