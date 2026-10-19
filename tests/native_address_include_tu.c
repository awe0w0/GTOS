/* The smallest consumer of the name, used twice.
 *
 * Against the bound original SDK alone this must fail with exactly
 * `'sys/socket.h' file not found` - the same diagnostic that stopped the Content
 * -M frontier. That is the negative control: the SDK does not provide the name,
 * and a different error would mean the test proved something else.
 *
 * Against the export overlay the same bytes must compile, which is the positive.
 * The constant and the type are both exercised so the header cannot be empty.
 */
#include <sys/socket.h>

int gtos_address_include_probe(void) {
    socklen_t length = (socklen_t)sizeof(struct sockaddr_storage);
    return (int)(AF_INET + AF_INET6 + AF_UNSPEC + length);
}
