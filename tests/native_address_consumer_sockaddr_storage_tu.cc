/* Independent-include gate: the real Chromium `net/base/sockaddr_storage.h`.
 *
 * This translation unit includes exactly one header and nothing else. That is the
 * whole point: the real header only reaches <sys/socket.h> under IS_WIN or
 * IS_POSIX/IS_FUCHSIA, and on GTOS both are 0, so it does NOT define
 * `sockaddr_storage` or `socklen_t` for itself. If this file included
 * <sys/socket.h> first it would compile anyway - and would then be proving the
 * include ORDER rather than the header. Pre-including is forbidden here.
 *
 * It compiles only because the reviewed overlay adds BUILDFLAG(IS_GTOS) to that
 * one include arm, with the raw header left byte-identical.
 *
 * This is a small translation unit compile (-fsyntax-only), not a Content CXX
 * build, and it runs no Chromium code.
 */
#include "net/base/sockaddr_storage.h"

namespace {

// The header must have produced both names on its own.
static_assert(sizeof(sockaddr_storage) == 128,
              "sockaddr_storage must be the canonical 128-byte block");
static_assert(sizeof(socklen_t) == 4, "socklen_t must be 4 bytes on i386");

// SockaddrStorage's contract: addr() reinterpret_casts the storage block, so
// ss_family has to be the first member and the whole block has to be at least as
// aligned as the sockaddr family type.
static_assert(sizeof(net::SockaddrStorage) >= 128 + sizeof(socklen_t),
              "SockaddrStorage holds the block plus its length");
static_assert(alignof(net::SockaddrStorage) >= alignof(sockaddr_storage),
              "the storage must keep its own alignment");

}  // namespace

// Exercise the two inline members the header actually defines. No socket is
// called; this only proves the reinterpret_cast chain type-checks.
extern "C" int gtos_consumer_sockaddr_storage_probe(void) {
    net::SockaddrStorage storage;
    sockaddr* generic = storage.addr();
    const net::SockaddrStorage& view = storage;
    const sockaddr* const_generic = view.addr();
    generic->sa_family = AF_INET;
    int result = 0;
    if (const_generic->sa_family == AF_INET) {
        result += 1;
    }
    if (storage.addr_len == sizeof(storage.addr_storage)) {
        result += 2;
    }
    return result;
}
