#ifndef GTOS_PROCESS_LOOPBACK_ABI_H
#define GTOS_PROCESS_LOOPBACK_ABI_H

// BSP-local byte streams. Ports name listeners inside this GTOS instance.
#define GTOS_LOOPBACK_ABI_VERSION 1U
#define GTOS_SYS_LOOPBACK_OPEN 0x4770U
#define GTOS_SYS_LOOPBACK_BIND 0x4771U
#define GTOS_SYS_LOOPBACK_LISTEN 0x4772U
#define GTOS_SYS_LOOPBACK_CONNECT 0x4773U
#define GTOS_SYS_LOOPBACK_ACCEPT 0x4774U
#define GTOS_SYS_LOOPBACK_READ 0x4775U
#define GTOS_SYS_LOOPBACK_WRITE 0x4776U
#define GTOS_SYS_LOOPBACK_SHUTDOWN_WRITE 0x4777U
#define GTOS_SYS_LOOPBACK_INFO 0x4778U
#define GTOS_SYS_LOOPBACK_CLOSE 0x4779U

#define GTOS_LOOPBACK_BACKLOG_LIMIT 16U
#define GTOS_LOOPBACK_TRANSFER_LIMIT 4096U
#define GTOS_LOOPBACK_READABLE 1U
#define GTOS_LOOPBACK_WRITABLE 2U
#define GTOS_LOOPBACK_ACCEPTABLE 4U
#define GTOS_LOOPBACK_END_OF_STREAM 8U
#define GTOS_LOOPBACK_PEER_CLOSED 16U

#define GTOS_LOOPBACK_ERR_BAD_HANDLE (-9)
#define GTOS_LOOPBACK_ERR_AGAIN (-11)
#define GTOS_LOOPBACK_ERR_BAD_ADDRESS (-14)
#define GTOS_LOOPBACK_ERR_INVALID (-22)
#define GTOS_LOOPBACK_ERR_LIMIT (-24)
#define GTOS_LOOPBACK_ERR_BROKEN_PIPE (-32)
#define GTOS_LOOPBACK_ERR_UNAVAILABLE (-38)
#define GTOS_LOOPBACK_ERR_ADDRESS_IN_USE (-98)
#define GTOS_LOOPBACK_ERR_NOT_CONNECTED (-107)
#define GTOS_LOOPBACK_ERR_REFUSED (-111)

typedef struct GtosLoopbackControlRequest {
    unsigned version, handle;
} GtosLoopbackControlRequest;

typedef struct GtosLoopbackOptionRequest {
    unsigned version, handle, value;
} GtosLoopbackOptionRequest;

typedef struct GtosLoopbackTransferRequest {
    unsigned version, handle, buffer, bytes;
} GtosLoopbackTransferRequest;

typedef struct GtosLoopbackInfo {
    unsigned local_port, peer_port, ready;
} GtosLoopbackInfo;

// OPEN requires handle zero and returns a positive owned handle. BIND takes
// a port (zero selects an ephemeral port) and LISTEN takes a backlog in 1..16.
// CONNECT takes the listening port. ACCEPT returns a new handle or -AGAIN.
// READ/WRITE return a byte count. A nonempty read returns zero at end of stream.
// A full peer buffer and an empty live stream return -AGAIN. Partial transfers
// may occur in either direction. SHUTDOWN_WRITE lets the peer drain before EOF.
// INFO uses OptionRequest.value as the address of a GtosLoopbackInfo result.
// PEER_CLOSED means the remote handle was closed. END_OF_STREAM describes the
// peer's write direction and may be set while queued bytes remain readable.
// All handles are private to the calling process. No endpoint is reachable
// through an external network interface.
#ifdef __cplusplus
static_assert(sizeof(GtosLoopbackControlRequest) == 8, "loopback control ABI");
static_assert(sizeof(GtosLoopbackOptionRequest) == 12, "loopback option ABI");
static_assert(sizeof(GtosLoopbackTransferRequest) == 16, "loopback transfer ABI");
static_assert(sizeof(GtosLoopbackInfo) == 12, "loopback info ABI");
#endif

#endif
