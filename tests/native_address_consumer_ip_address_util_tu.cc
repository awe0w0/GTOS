/* Independent-include gate: the real Chromium `net/base/ip_address_util.h`.
 *
 * One header, nothing else. It reaches <netinet/in.h> under its non-Windows arm
 * and needs in_addr and in6_addr from there; on GTOS that name resolves to the
 * canonical llvm-libc types through the overlay.
 *
 * The functions themselves (ToInAddr/ToIn6Addr) are only declared here and are
 * NOT defined: this is -fsyntax-only, so no link happens and no stub is written
 * in their place. Their behaviour is NOT_RUN this round.
 */
#include "net/base/ip_address_util.h"

namespace {

// The two declarations have to name the canonical types with the canonical sizes.
static_assert(sizeof(in_addr) == 4, "in_addr is one 32-bit word");
static_assert(sizeof(in6_addr) == 16, "in6_addr is sixteen bytes");

// The return types are exactly those, which is what ip_address_util.cc asserts
// before it memcpys into them.
static_assert(sizeof(decltype(net::ToInAddr(*static_cast<net::IPAddress*>(
                  nullptr)))) == 4,
              "ToInAddr must return in_addr");
static_assert(sizeof(decltype(net::ToIn6Addr(*static_cast<net::IPAddress*>(
                  nullptr)))) == 16,
              "ToIn6Addr must return in6_addr");

}  // namespace

extern "C" int gtos_consumer_ip_address_util_probe(void) {
    return static_cast<int>(sizeof(in_addr) + sizeof(in6_addr));
}
