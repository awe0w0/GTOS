/* Independent-include gate: the real Chromium `net/base/ip_endpoint.h`.
 *
 * One header, nothing else. This is the header the Content -M frontier actually
 * reaches: shell_main_delegate.cc -> shell_content_browser_client.h ->
 * content_browser_client.h -> process_selection_deferring_condition.h ->
 * navigation_handle.h -> net/base/ip_endpoint.h, which is where the old
 * `'sys/socket.h' file not found` came from.
 *
 * Its non-Windows arm is just `#include <sys/socket.h>` after a bare
 * `struct sockaddr;` forward declaration, so the overlay has to supply the
 * canonical definition and socklen_t. Nothing is pre-included.
 *
 * -fsyntax-only: no link, no Chromium run, and the ToSockAddr/FromSockAddr bodies
 * live in the .cc file, which this round does not build. Their behaviour is
 * NOT_RUN.
 */
#include "net/base/ip_endpoint.h"

namespace {

// Local type-identity helper: this unit must not include anything the real header
// does not already bring in.
template <typename A, typename B>
struct Same {
    static const bool value = false;
};
template <typename A>
struct Same<A, A> {
    static const bool value = true;
};

// The signatures the .cc implements, checked through the declarations only.
using ToSockAddrSignature = bool (net::IPEndPoint::*)(sockaddr*,
                                                      socklen_t*) const;
using FromSockAddrSignature = bool (net::IPEndPoint::*)(
    const struct sockaddr*, socklen_t);

static_assert(Same<decltype(&net::IPEndPoint::ToSockAddr),
                   ToSockAddrSignature>::value,
              "ToSockAddr takes sockaddr* and socklen_t*");
static_assert(Same<decltype(&net::IPEndPoint::FromSockAddr),
                   FromSockAddrSignature>::value,
              "FromSockAddr takes a const sockaddr* and a socklen_t length");
static_assert(sizeof(socklen_t) == 4, "socklen_t is 4 bytes on i386");
static_assert(sizeof(sockaddr) == 4,
              "the canonical sockaddr is 4 bytes, not a 16-byte host shape");

}  // namespace

extern "C" int gtos_consumer_ip_endpoint_probe(void) {
    // Constructing the value type is enough: it proves IPAddress and the port
    // member are complete without calling anything.
    net::IPEndPoint endpoint;
    return static_cast<int>(endpoint.port()) + static_cast<int>(sizeof(sockaddr));
}
