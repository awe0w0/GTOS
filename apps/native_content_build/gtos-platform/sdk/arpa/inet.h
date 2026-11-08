#ifndef GTOS_ARPA_INET_H
#define GTOS_ARPA_INET_H

#include <stdint.h>
#include <netinet/in.h>

// The native byte-order subset uses the IA32 little-endian representation.
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error GTOS network byte order adapters require little endian.
#endif

static inline uint16_t htons(uint16_t value) { return __builtin_bswap16(value); }
static inline uint32_t htonl(uint32_t value) { return __builtin_bswap32(value); }
static inline uint16_t ntohs(uint16_t value) { return htons(value); }
static inline uint32_t ntohl(uint32_t value) { return htonl(value); }

#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46

#ifdef __cplusplus
extern "C" {
#endif
int inet_pton(int family, const char* text, void* address);
const char* inet_ntop(int family, const void* address, char* text, socklen_t size);
#ifdef __cplusplus
}
#endif

#endif
