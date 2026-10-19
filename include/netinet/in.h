#ifndef __GTOS__NETINET__IN_H
#define __GTOS__NETINET__IN_H
// The second name for the same thing.
//
// net/base/ip_address_util.h includes <netinet/in.h> under its non-Windows arm
// because that is where in_addr and in6_addr come from. On GTOS they come from
// the canonical llvm-libc headers through net/address_types.h, so this header is
// an alias for that and adds nothing of its own.
//
// Like <sys/socket.h>, it declares no function and defines no constant beyond
// the three families. IPPROTO_*, INADDR_*, IN6ADDR_*, INET_ADDRSTRLEN and
// INET6_ADDRSTRLEN are not part of the fixed membership this revision selected,
// and they are not silently supplied from the Linux macro table.
#include <net/address_types.h>

#endif  // __GTOS__NETINET__IN_H
