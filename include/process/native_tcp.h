#ifndef GTOS_PROCESS_NATIVE_TCP_H
#define GTOS_PROCESS_NATIVE_TCP_H

#include <multitasking.h>
#include <net/tcp_client.h>
#include <process/native_tcp_endpoint.h>

namespace gtos { namespace process {

class NativeTcp final : public NativeTcpEndpoint {
public:
    NativeTcp(net::TcpClient& client, TaskManager& tasks, uint32_t localAddress,
              uint32_t peerAddress, uint16_t peerPort, uint32_t sequenceSeed);
    int32_t Call(uint32_t owner, memory::ProcessAddressSpace& space,
        uint32_t operation, uint32_t address, uint32_t bytes) override;
    void ReclaimOwner(uint32_t owner) override;
    uint32_t OpenedSockets() const { return nextPort - 49152; }
    unsigned ActiveSockets() const;

private:
    struct Socket {
        uint32_t owner;
        int handle;
        uint16_t port;
    };
    net::TcpClient& client;
    TaskManager& tasks;
    const uint32_t localAddress, peerAddress;
    const uint16_t peerPort;
    Socket sockets[net::TcpClient::MaximumConnections];
    uint32_t nextPort, sequence;
    uint8_t bounce[GTOS_TCP_TRANSFER_LIMIT];

    Socket* Find(uint32_t owner, uint32_t handle);
    int32_t Connect(uint32_t owner, const GtosTcpConnectRequest& request);
    void Release(Socket& socket);
};

} }

#endif
