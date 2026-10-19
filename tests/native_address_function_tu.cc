/* Negative: the umbrella names a representation, it does not declare a socket API.
 *
 * <sys/socket.h> on GTOS resolves to <net/address_types.h> and nothing else. It
 * declares no function, so this call must fail to compile with an undeclared
 * identifier. If it ever compiles, a stub has appeared and the layer is lying
 * about what exists.
 *
 * The runner greps for the undeclared-identifier diagnostic, so a failure for a
 * different reason does not count.
 */
#include <sys/socket.h>

int gtos_address_function_probe(void) {
    /* No such declaration exists in this layer, and no empty stand-in either. */
    return socket(AF_INET, 0, 0);
}
