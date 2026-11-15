#ifndef GTOS_NET_TCP_CLIENT_H
#define GTOS_NET_TCP_CLIENT_H

#include <net/ipv4.h>

namespace gtos { namespace net {

// Bounded, nonblocking IPv4 active opens. The BSP calls Poll with a
// monotonic millisecond counter. The caller owns source port and sequence
// selection and keeps the adapter and IPv4 route alive.
class TcpClient final : private InternetProtocolHandler {
public:
    enum Error {
        Again = -11,
        Invalid = -22,
        Exhausted = -24,
        Closed = -32,
        Reset = -104,
        TimedOut = -110,
        Refused = -111,
    };
    static const unsigned MaximumConnections = 8;

    explicit TcpClient(InternetProtocolProvider& ip);
    int Open(uint32_t remoteIP, uint16_t remotePort, uint16_t localPort, uint32_t initialSequence);
    int Status(int handle) const;
    int Write(int handle, const uint8_t* data, unsigned bytes);
    int Read(int handle, uint8_t* data, unsigned bytes);
    int ShutdownWrite(int handle);
    bool Close(int handle);
    void Poll(uint32_t milliseconds);

private:
    enum Phase { Free, Connecting, Established, Failed };
    static const unsigned MSS = 536;
    static const unsigned ReceiveBytes = 4096;
    struct Connection {
        uint32_t remoteIP;
        uint16_t remotePort, localPort, peerWindow;
        uint16_t peerMss;
        uint32_t transmitNext, transmitUnacked, receiveNext;
        uint32_t retryAt;
        uint16_t transmitBytes, receiveStart, received;
        uint8_t pendingFlags, retries;
        bool peerFinished, writeFinished;
        int handle, error;
        Phase phase;
        uint8_t transmit[MSS];
        uint8_t receive[ReceiveBytes];
    };

    Connection connections[MaximumConnections];
    uint32_t now, nextHandle;

    Connection* Find(int handle);
    const Connection* Find(int handle) const;
    void SendSegment(const Connection&, uint32_t sequence, uint8_t flags,
                     const uint8_t* data, unsigned bytes);
    void SendPending(Connection&);
    bool OnInternetProtocolReceived(uint32_t source, uint32_t destination,
                                    uint8_t* packet, uint32_t bytes) override;
};

} }

#endif
