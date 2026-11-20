#include <memory/criticalsection.h>
#include <process/clock_abi.h>
#include <process/native_tcp.h>

namespace gtos { namespace process {

static_assert(net::TcpClient::Again == GTOS_TCP_ERR_AGAIN, "TCP pending ABI");
static_assert(net::TcpClient::TimedOut == GTOS_TCP_ERR_TIMEOUT, "TCP timeout ABI");

NativeTcp::NativeTcp(net::TcpClient& transport, TaskManager& scheduler,
                     uint32_t local, uint32_t peer, uint16_t port, uint32_t sequenceSeed)
    : client(transport), tasks(scheduler), localAddress(local), peerAddress(peer),
      peerPort(port), sockets{}, nextPort(49152), sequence(sequenceSeed) {}

NativeTcp::Socket* NativeTcp::Find(uint32_t owner, uint32_t handle) {
    for (auto& socket : sockets)
        if (socket.owner == owner && static_cast<uint32_t>(socket.handle) == handle)
            return &socket;
    return nullptr;
}

int32_t NativeTcp::Connect(uint32_t owner, const GtosTcpConnectRequest& request) {
    if (request.version != GTOS_TCP_ABI_VERSION || request.flags || !request.peer_port
        || request.peer_port > 65535) return GTOS_TCP_ERR_INVALID;
    if (request.peer_ipv4 != peerAddress || request.peer_port != peerPort)
        return GTOS_TCP_ERR_DENIED;
    if (nextPort > 65535) return GTOS_TCP_ERR_LIMIT;
    for (auto& socket : sockets) {
        if (socket.owner) continue;
        const uint16_t port = static_cast<uint16_t>(nextPort);
        client.Poll(tasks.Ticks() * (GTOS_CLOCK_RESOLUTION_US / 1000));
        const int handle = client.Open(peerAddress, peerPort, port, sequence);
        if (handle <= 0) return handle;
        ++nextPort;
        sequence += 0x9e3779b9U;
        socket = {owner, handle, port};
        return handle;
    }
    return GTOS_TCP_ERR_LIMIT;
}

void NativeTcp::Release(Socket& socket) {
    client.Close(socket.handle);
    socket = {};
}

template<class Request>
bool NativeTcp::Snapshot(memory::ProcessAddressSpace& space, uint32_t address, Request& request) {
    return space.CheckCopyLayout() &&
        space.CopyFromUserWithCheckedLayout(&request, address, sizeof(request));
}

int32_t NativeTcp::Call(uint32_t owner, memory::ProcessAddressSpace& space,
                        uint32_t operation, uint32_t address, uint32_t bytes) {
    memory::InterruptGuard guard;
    if (!owner) return GTOS_TCP_ERR_UNAVAILABLE;
    const uint32_t milliseconds = tasks.Ticks() * (GTOS_CLOCK_RESOLUTION_US / 1000);

    // Socket operations leave this address space's mappings intact.
    // The checked layout remains valid until this call releases the guard.
    switch (operation) {
    case GTOS_SYS_TCP_CONNECT: {
        GtosTcpConnectRequest request;
        if (bytes != sizeof(request)) return GTOS_TCP_ERR_INVALID;
        if (!Snapshot(space, address, request))
            return GTOS_TCP_ERR_BAD_ADDRESS;
        return Connect(owner, request);
    }
    case GTOS_SYS_TCP_INFO: {
        GtosTcpInfoRequest request;
        if (bytes != sizeof(request)) return GTOS_TCP_ERR_INVALID;
        if (!Snapshot(space, address, request))
            return GTOS_TCP_ERR_BAD_ADDRESS;
        if (request.version != GTOS_TCP_ABI_VERSION) return GTOS_TCP_ERR_INVALID;
        const auto* socket = Find(owner, request.handle);
        if (!socket) return GTOS_TCP_ERR_BAD_HANDLE;
        if (!space.CheckCopyRange(request.result_va, sizeof(GtosTcpInfo), true))
            return GTOS_TCP_ERR_BAD_ADDRESS;
        client.Poll(milliseconds);
        unsigned readable = 0;
        bool endOfStream = false;
        const int status = client.Describe(socket->handle, readable, endOfStream);
        const GtosTcpInfo result = {GTOS_TCP_ABI_VERSION, status,
            localAddress, socket->port, peerAddress, peerPort, readable,
            endOfStream ? GTOS_TCP_END_OF_STREAM : 0};
        return space.CopyToUserWithCheckedLayout(request.result_va, &result, sizeof(result))
            ? 0 : GTOS_TCP_ERR_BAD_ADDRESS;
    }
    case GTOS_SYS_TCP_READ:
    case GTOS_SYS_TCP_WRITE: {
        GtosTcpTransferRequest request;
        if (bytes != sizeof(request)) return GTOS_TCP_ERR_INVALID;
        if (!Snapshot(space, address, request))
            return GTOS_TCP_ERR_BAD_ADDRESS;
        if (request.version != GTOS_TCP_ABI_VERSION || request.bytes > sizeof(bounce))
            return GTOS_TCP_ERR_INVALID;
        const auto* socket = Find(owner, request.handle);
        if (!socket) return GTOS_TCP_ERR_BAD_HANDLE;
        if (!request.bytes) return 0;
        if (operation == GTOS_SYS_TCP_READ) {
            // Validate the entire destination before consuming received bytes.
            if (!space.CheckCopyRange(request.buffer_va, request.bytes, true))
                return GTOS_TCP_ERR_BAD_ADDRESS;
            client.Poll(milliseconds);
            const int count = client.Read(socket->handle, bounce, request.bytes);
            return count <= 0 || space.CopyToUserWithCheckedLayout(request.buffer_va, bounce, count)
                ? count : GTOS_TCP_ERR_BAD_ADDRESS;
        }
        if (!space.CopyFromUserWithCheckedLayout(bounce, request.buffer_va, request.bytes))
            return GTOS_TCP_ERR_BAD_ADDRESS;
        client.Poll(milliseconds);
        return client.Write(socket->handle, bounce, request.bytes);
    }
    case GTOS_SYS_TCP_SHUTDOWN_WRITE:
    case GTOS_SYS_TCP_CLOSE: {
        GtosTcpControlRequest request;
        if (bytes != sizeof(request)) return GTOS_TCP_ERR_INVALID;
        if (!Snapshot(space, address, request))
            return GTOS_TCP_ERR_BAD_ADDRESS;
        if (request.version != GTOS_TCP_ABI_VERSION) return GTOS_TCP_ERR_INVALID;
        auto* socket = Find(owner, request.handle);
        if (!socket) return GTOS_TCP_ERR_BAD_HANDLE;
        if (operation == GTOS_SYS_TCP_CLOSE) {
            Release(*socket);
            return 0;
        }
        client.Poll(milliseconds);
        return client.ShutdownWrite(socket->handle);
    }
    default:
        return GTOS_TCP_ERR_INVALID;
    }
}

void NativeTcp::ReclaimOwner(uint32_t owner) {
    memory::InterruptGuard guard;
    if (!owner) return;
    for (auto& socket : sockets)
        if (socket.owner == owner) Release(socket);
}

unsigned NativeTcp::ActiveSockets() const {
    memory::InterruptGuard guard;
    unsigned active = 0;
    for (const auto& socket : sockets)
        if (socket.owner) ++active;
    return active;
}

} }
