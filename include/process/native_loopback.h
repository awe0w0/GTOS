#ifndef GTOS_PROCESS_NATIVE_LOOPBACK_H
#define GTOS_PROCESS_NATIVE_LOOPBACK_H

#include <process/native_loopback_endpoint.h>

namespace gtos { namespace process {

class NativeLoopback final : public NativeLoopbackEndpoint {
public:
    static const uint32_t MaximumSockets = 16;
    NativeLoopback();
    int32_t Call(uint32_t owner, memory::ProcessAddressSpace& space,
        uint32_t operation, uint32_t address, uint32_t bytes) override;
    void ReclaimOwner(uint32_t owner) override;
private:
    enum State { Free, Unbound, Bound, Listening, Pending, Connected };
    struct Socket {
        State state;
        uint32_t owner, handle, peer, listener, backlog;
        uint32_t localPort, peerPort;
        uint32_t begin, queued;
        bool writeClosed;
        uint8_t receive[GTOS_LOOPBACK_TRANSFER_LIMIT];
    };
    Socket sockets[MaximumSockets];
    uint32_t nextHandle, nextPort;
    uint8_t bounce[GTOS_LOOPBACK_TRANSFER_LIMIT];

    Socket* Find(uint32_t owner, uint32_t handle);
    Socket* FindListener(uint32_t port);
    bool PortInUse(uint32_t port) const;
    uint32_t FindEphemeralPort() const;
    Socket* Allocate(uint32_t owner, State state);
    void Release(Socket& socket);
    uint32_t Link(const Socket& socket) const;
    Socket* Get(uint32_t link);
    int32_t Bind(Socket& socket, uint32_t port);
    int32_t Listen(Socket& socket, uint32_t backlog);
    int32_t Connect(Socket& sender, uint32_t port);
    int32_t Accept(Socket& listener);
    int32_t Transfer(Socket& socket, memory::ProcessAddressSpace& space,
        const GtosLoopbackTransferRequest& request, bool reading);
    int32_t Info(Socket& socket, memory::ProcessAddressSpace& space, uint32_t output);
};

} }

#endif
