#include <memory/criticalsection.h>
#include <process/native_loopback.h>

using namespace gtos::memory;
using namespace gtos::process;

namespace {
    const uint32_t FirstEphemeralPort = 49152;
    const uint32_t LastPort = 65535;
    const uint32_t BufferBytes = GTOS_LOOPBACK_TRANSFER_LIMIT;

    template<class Request>
    int32_t ReadRequest(ProcessAddressSpace& space, uint32_t address,
                        uint32_t bytes, Request& request) {
        if (bytes != sizeof(request)) return GTOS_LOOPBACK_ERR_INVALID;
        if (!space.CopyFromUser(&request, address, bytes)) return GTOS_LOOPBACK_ERR_BAD_ADDRESS;
        return request.version == GTOS_LOOPBACK_ABI_VERSION ? 0 : GTOS_LOOPBACK_ERR_UNAVAILABLE;
    }

    uint32_t Min(uint32_t a, uint32_t b) { return a < b ? a : b; }
}

NativeLoopback::NativeLoopback()
    : nextHandle(1), nextPort(FirstEphemeralPort) {
    for (auto& socket : sockets) socket.state = Free;
}

NativeLoopback::Socket* NativeLoopback::Find(uint32_t owner, uint32_t handle) {
    if (!handle) return nullptr;
    for (auto& socket : sockets)
        if (socket.state != Free && socket.state != Pending &&
            socket.owner == owner && socket.handle == handle) return &socket;
    return nullptr;
}

NativeLoopback::Socket* NativeLoopback::FindListener(uint32_t port) {
    for (auto& socket : sockets)
        if (socket.state == Listening && socket.localPort == port) return &socket;
    return nullptr;
}

bool NativeLoopback::PortInUse(uint32_t port) const {
    for (const auto& socket : sockets)
        if (socket.state != Free && socket.localPort == port) return true;
    return false;
}

uint32_t NativeLoopback::FindEphemeralPort() const {
    uint32_t candidate = nextPort;
    // There can be at most MaximumSockets occupied ports.
    for (uint32_t i = 0; i <= MaximumSockets; ++i) {
        if (!PortInUse(candidate)) return candidate;
        candidate = candidate == LastPort ? FirstEphemeralPort : candidate + 1;
    }
    return 0;
}

NativeLoopback::Socket* NativeLoopback::Allocate(uint32_t owner, State state) {
    if (nextHandle > 0x7fffffffU) return nullptr;
    for (auto& socket : sockets) {
        if (socket.state != Free) continue;
        socket.state = state;
        socket.owner = owner;
        socket.handle = nextHandle++;
        socket.peer = socket.listener = socket.backlog = 0;
        socket.localPort = socket.peerPort = socket.begin = socket.queued = 0;
        socket.writeClosed = false;
        return &socket;
    }
    return nullptr;
}

uint32_t NativeLoopback::Link(const Socket& socket) const {
    return static_cast<uint32_t>(&socket - sockets) + 1;
}

NativeLoopback::Socket* NativeLoopback::Get(uint32_t link) {
    return link ? &sockets[link - 1] : nullptr;
}

void NativeLoopback::Release(Socket& socket) {
    if (socket.state == Listening) {
        const uint32_t listener = Link(socket);
        for (auto& pending : sockets) {
            if (pending.state != Pending || pending.listener != listener) continue;
            if (Socket* peer = Get(pending.peer)) peer->peer = 0;
            pending.state = Free;
        }
    }
    if (Socket* peer = Get(socket.peer)) peer->peer = 0;
    socket.state = Free;
}

int32_t NativeLoopback::Bind(Socket& socket, uint32_t port) {
    if (socket.state != Unbound || port > LastPort) return GTOS_LOOPBACK_ERR_INVALID;
    if (port && PortInUse(port)) return GTOS_LOOPBACK_ERR_ADDRESS_IN_USE;
    if (!port) {
        port = FindEphemeralPort();
        if (!port) return GTOS_LOOPBACK_ERR_LIMIT;
        nextPort = port == LastPort ? FirstEphemeralPort : port + 1;
    }
    socket.localPort = port;
    socket.state = Bound;
    return 0;
}

int32_t NativeLoopback::Listen(Socket& socket, uint32_t backlog) {
    static_assert(GTOS_LOOPBACK_BACKLOG_LIMIT <= MaximumSockets);
    if (socket.state != Bound || !backlog || backlog > GTOS_LOOPBACK_BACKLOG_LIMIT)
        return GTOS_LOOPBACK_ERR_INVALID;
    socket.backlog = backlog;
    socket.state = Listening;
    return 0;
}

int32_t NativeLoopback::Connect(Socket& sender, uint32_t port) {
    if ((sender.state != Unbound && sender.state != Bound) || !port || port > LastPort)
        return GTOS_LOOPBACK_ERR_INVALID;
    Socket* listener = FindListener(port);
    if (!listener) return GTOS_LOOPBACK_ERR_REFUSED;
    const uint32_t listenerLink = Link(*listener);
    uint32_t pending = 0;
    for (const auto& socket : sockets)
        if (socket.state == Pending && socket.listener == listenerLink) ++pending;
    if (pending >= listener->backlog) return GTOS_LOOPBACK_ERR_REFUSED;
    const uint32_t localPort = sender.localPort ? sender.localPort : FindEphemeralPort();
    if (!localPort) return GTOS_LOOPBACK_ERR_LIMIT;
    Socket* peer = Allocate(listener->owner, Pending);
    if (!peer) return GTOS_LOOPBACK_ERR_LIMIT;
    if (!sender.localPort) nextPort = localPort == LastPort ? FirstEphemeralPort : localPort + 1;
    sender.state = Connected;
    sender.localPort = localPort;
    sender.peerPort = port;
    sender.peer = Link(*peer);
    peer->localPort = port;
    peer->peerPort = localPort;
    peer->peer = Link(sender);
    peer->listener = listenerLink;
    return 0;
}

int32_t NativeLoopback::Accept(Socket& listener) {
    if (listener.state != Listening) return GTOS_LOOPBACK_ERR_INVALID;
    const uint32_t link = Link(listener);
    Socket* oldest = nullptr;
    for (auto& socket : sockets)
        if (socket.state == Pending && socket.listener == link &&
            (!oldest || socket.handle < oldest->handle)) oldest = &socket;
    if (!oldest) return GTOS_LOOPBACK_ERR_AGAIN;
    oldest->state = Connected;
    oldest->listener = 0;
    return static_cast<int32_t>(oldest->handle);
}

int32_t NativeLoopback::Transfer(Socket& socket, ProcessAddressSpace& space,
    const GtosLoopbackTransferRequest& request, bool reading) {
    if (socket.state != Connected) return GTOS_LOOPBACK_ERR_NOT_CONNECTED;
    if (!request.bytes) return 0;
    if (request.bytes > BufferBytes) return GTOS_LOOPBACK_ERR_INVALID;
    if (!space.ValidateUserRange(request.buffer, request.bytes, reading))
        return GTOS_LOOPBACK_ERR_BAD_ADDRESS;
    Socket* peer = Get(socket.peer);
    if (reading) {
        const uint32_t count = Min(request.bytes, socket.queued);
        if (!count) return !peer || peer->writeClosed ? 0 : GTOS_LOOPBACK_ERR_AGAIN;
        for (uint32_t i = 0; i < count; ++i)
            bounce[i] = socket.receive[(socket.begin + i) % BufferBytes];
        if (!space.CopyToUser(request.buffer, bounce, count)) return GTOS_LOOPBACK_ERR_BAD_ADDRESS;
        socket.begin = (socket.begin + count) % BufferBytes;
        socket.queued -= count;
        return static_cast<int32_t>(count);
    }
    if (!peer || socket.writeClosed) return GTOS_LOOPBACK_ERR_BROKEN_PIPE;
    const uint32_t count = Min(request.bytes, BufferBytes - peer->queued);
    if (!count) return GTOS_LOOPBACK_ERR_AGAIN;
    if (!space.CopyFromUser(bounce, request.buffer, count)) return GTOS_LOOPBACK_ERR_BAD_ADDRESS;
    for (uint32_t i = 0; i < count; ++i)
        peer->receive[(peer->begin + peer->queued + i) % BufferBytes] = bounce[i];
    peer->queued += count;
    return static_cast<int32_t>(count);
}

int32_t NativeLoopback::Info(Socket& socket, ProcessAddressSpace& space, uint32_t output) {
    GtosLoopbackInfo info{socket.localPort, socket.peerPort, 0};
    if (socket.state == Listening) {
        const uint32_t link = Link(socket);
        for (const auto& pending : sockets) {
            if (pending.state == Pending && pending.listener == link) {
                info.ready |= GTOS_LOOPBACK_ACCEPTABLE;
                break;
            }
        }
    } else if (socket.state == Connected) {
        const Socket* peer = Get(socket.peer);
        if (socket.queued) info.ready |= GTOS_LOOPBACK_READABLE;
        if (!peer || peer->writeClosed) info.ready |= GTOS_LOOPBACK_END_OF_STREAM;
        if (!peer) info.ready |= GTOS_LOOPBACK_PEER_CLOSED;
        if (peer && !socket.writeClosed && peer->queued < BufferBytes)
            info.ready |= GTOS_LOOPBACK_WRITABLE;
    }
    return space.CopyToUser(output, &info, sizeof(info)) ? 0 : GTOS_LOOPBACK_ERR_BAD_ADDRESS;
}

int32_t NativeLoopback::Call(uint32_t owner, ProcessAddressSpace& space,
    uint32_t operation, uint32_t address, uint32_t bytes) {
    InterruptGuard guard;
    if (!owner) return GTOS_LOOPBACK_ERR_INVALID;
    switch (operation) {
    case GTOS_SYS_LOOPBACK_BIND:
    case GTOS_SYS_LOOPBACK_LISTEN:
    case GTOS_SYS_LOOPBACK_CONNECT:
    case GTOS_SYS_LOOPBACK_INFO: {
        GtosLoopbackOptionRequest request;
        const int32_t error = ReadRequest(space, address, bytes, request);
        if (error) return error;
        Socket* socket = Find(owner, request.handle);
        if (!socket) return GTOS_LOOPBACK_ERR_BAD_HANDLE;
        if (operation == GTOS_SYS_LOOPBACK_BIND) return Bind(*socket, request.value);
        if (operation == GTOS_SYS_LOOPBACK_LISTEN) return Listen(*socket, request.value);
        if (operation == GTOS_SYS_LOOPBACK_CONNECT) return Connect(*socket, request.value);
        return Info(*socket, space, request.value);
    }
    case GTOS_SYS_LOOPBACK_READ:
    case GTOS_SYS_LOOPBACK_WRITE: {
        GtosLoopbackTransferRequest request;
        const int32_t error = ReadRequest(space, address, bytes, request);
        if (error) return error;
        Socket* socket = Find(owner, request.handle);
        if (!socket) return GTOS_LOOPBACK_ERR_BAD_HANDLE;
        return Transfer(*socket, space, request, operation == GTOS_SYS_LOOPBACK_READ);
    }
    case GTOS_SYS_LOOPBACK_OPEN:
    case GTOS_SYS_LOOPBACK_ACCEPT:
    case GTOS_SYS_LOOPBACK_SHUTDOWN_WRITE:
    case GTOS_SYS_LOOPBACK_CLOSE: {
        GtosLoopbackControlRequest request;
        const int32_t error = ReadRequest(space, address, bytes, request);
        if (error) return error;
        if (operation == GTOS_SYS_LOOPBACK_OPEN) {
            if (request.handle) return GTOS_LOOPBACK_ERR_INVALID;
            Socket* socket = Allocate(owner, Unbound);
            return socket ? static_cast<int32_t>(socket->handle) : GTOS_LOOPBACK_ERR_LIMIT;
        }
        Socket* socket = Find(owner, request.handle);
        if (!socket) return GTOS_LOOPBACK_ERR_BAD_HANDLE;
        if (operation == GTOS_SYS_LOOPBACK_ACCEPT) return Accept(*socket);
        if (operation == GTOS_SYS_LOOPBACK_SHUTDOWN_WRITE) {
            if (socket->state != Connected) return GTOS_LOOPBACK_ERR_NOT_CONNECTED;
            socket->writeClosed = true;
        } else {
            Release(*socket);
        }
        return 0;
    }
    default:
        return GTOS_LOOPBACK_ERR_UNAVAILABLE;
    }
}

void NativeLoopback::ReclaimOwner(uint32_t owner) {
    InterruptGuard guard;
    for (auto& socket : sockets)
        if (socket.state != Free && socket.owner == owner) Release(socket);
}
