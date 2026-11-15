#ifndef GTOS_PROCESS_TCP_ABI_H
#define GTOS_PROCESS_TCP_ABI_H

// EAX selects the operation, EBX points to its request and ECX is its
// exact size. A successful CONNECT returns an owned handle in EAX.
// The configured endpoint decides which IPv4 destinations are available.
#define GTOS_TCP_ABI_VERSION 1U
#define GTOS_SYS_TCP_CONNECT 0x4780U
#define GTOS_SYS_TCP_INFO 0x4781U
#define GTOS_SYS_TCP_READ 0x4782U
#define GTOS_SYS_TCP_WRITE 0x4783U
#define GTOS_SYS_TCP_SHUTDOWN_WRITE 0x4784U
#define GTOS_SYS_TCP_CLOSE 0x4785U

#define GTOS_TCP_TRANSFER_LIMIT 4096U
#define GTOS_TCP_END_OF_STREAM 1U
#define GTOS_TCP_ERR_BAD_HANDLE (-9)
#define GTOS_TCP_ERR_AGAIN (-11)
#define GTOS_TCP_ERR_DENIED (-13)
#define GTOS_TCP_ERR_BAD_ADDRESS (-14)
#define GTOS_TCP_ERR_INVALID (-22)
#define GTOS_TCP_ERR_LIMIT (-24)
#define GTOS_TCP_ERR_CLOSED (-32)
#define GTOS_TCP_ERR_UNAVAILABLE (-38)
#define GTOS_TCP_ERR_RESET (-104)
#define GTOS_TCP_ERR_TIMEOUT (-110)
#define GTOS_TCP_ERR_REFUSED (-111)

typedef struct GtosTcpConnectRequest {
    unsigned version, peer_ipv4, peer_port, flags;
} GtosTcpConnectRequest;
typedef struct GtosTcpControlRequest {
    unsigned version, handle;
} GtosTcpControlRequest;
typedef struct GtosTcpInfoRequest {
    unsigned version, handle, result_va;
} GtosTcpInfoRequest;
typedef struct GtosTcpTransferRequest {
    unsigned version, handle, buffer_va, bytes;
} GtosTcpTransferRequest;
typedef struct GtosTcpInfo {
    unsigned version;
    int status;
    unsigned local_ipv4, local_port, peer_ipv4, peer_port, readable, flags;
} GtosTcpInfo;

// Addresses retain their four wire bytes in i386 memory. Ports are numeric.
// INFO reports the current connect status, queued bytes and end of stream.
// READ and WRITE return the number of bytes transferred, or AGAIN.
// A pending shutdown returns AGAIN until its FIN has been acknowledged.
#if defined(__cplusplus)
static_assert(sizeof(GtosTcpConnectRequest) == 16, "TCP connect request size");
static_assert(sizeof(GtosTcpControlRequest) == 8, "TCP control request size");
static_assert(sizeof(GtosTcpInfoRequest) == 12, "TCP info request size");
static_assert(sizeof(GtosTcpTransferRequest) == 16, "TCP transfer request size");
static_assert(sizeof(GtosTcpInfo) == 32, "TCP info result size");
#endif
#endif
