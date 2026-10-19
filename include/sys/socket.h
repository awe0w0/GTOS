#ifndef __GTOS__SYS__SOCKET_H
#define __GTOS__SYS__SOCKET_H
// A name, not an implementation.
//
// `<sys/socket.h>` is what the real Chromium consumers reach for when they need
// the address types: net/base/ip_endpoint.h and net/base/sockaddr_storage.h
// include it under their non-Windows arm, and net/base/sys_addrinfo.h includes it
// too. This header makes that name resolve to the GTOS user-space address
// representation and nothing else.
//
// It deliberately declares no function. There is no socket(), bind(), connect(),
// listen(), accept(), send(), recv(), sendto(), recvfrom(), setsockopt(),
// getaddrinfo(), freeaddrinfo(), if_nametoindex() or if_indextoname() here, no
// empty stand-in for any of them, no errno behaviour and no wildcard typedef.
// A consumer that calls one of those must fail to compile, which is the honest
// result: this revision did not implement a socket API.
//
// SOCK_*, MSG_*, SOL_*, SO_*, AI_*, AF_UNIX and struct addrinfo are also not part
// of the fixed membership. Only AF_UNSPEC, AF_INET and AF_INET6 are.
#include <net/address_types.h>

#endif  // __GTOS__SYS__SOCKET_H
