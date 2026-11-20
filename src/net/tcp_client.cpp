#include <net/tcp_client.h>
#include <memory/criticalsection.h>

namespace gtos { namespace net {
namespace {
    const unsigned HeaderBytes = 20;
    const uint8_t Fin = 0x01, Syn = 0x02, Rst = 0x04, Psh = 0x08, Ack = 0x10;
    const unsigned MaximumAttempts = 7;

    uint16_t Read16(const uint8_t* data) {
        return (static_cast<uint16_t>(data[0]) << 8) | data[1];
    }

    uint32_t Read32(const uint8_t* data) {
        return (static_cast<uint32_t>(Read16(data)) << 16) | Read16(data + 2);
    }

    void Write16(uint8_t* data, uint16_t value) {
        data[0] = static_cast<uint8_t>(value >> 8);
        data[1] = static_cast<uint8_t>(value);
    }

    void Write32(uint8_t* data, uint32_t value) {
        Write16(data, static_cast<uint16_t>(value >> 16));
        Write16(data + 2, static_cast<uint16_t>(value));
    }

    uint16_t Checksum(uint32_t source, uint32_t destination, const uint8_t* packet, unsigned bytes) {
        uint8_t pseudo[12] = {};
        for (unsigned i = 0; i < 4; ++i) {
            pseudo[i] = static_cast<uint8_t>(source >> (i * 8));
            pseudo[i + 4] = static_cast<uint8_t>(destination >> (i * 8));
        }
        pseudo[9] = 6;
        Write16(pseudo + 10, static_cast<uint16_t>(bytes));
        uint32_t sum = 0;
        for (unsigned i = 0; i < sizeof(pseudo); i += 2) sum += Read16(pseudo + i);
        for (unsigned i = 1; i < bytes; i += 2) sum += Read16(packet + i - 1);
        if (bytes & 1) sum += static_cast<uint16_t>(packet[bytes - 1]) << 8;
        while (sum >> 16) sum = (sum & 0xffffU) + (sum >> 16);
        return static_cast<uint16_t>(~sum);
    }

    // Values are bounded by the short timer intervals used by Poll.
    bool Due(uint32_t now, uint32_t deadline) {
        return now - deadline < 0x80000000U;
    }

    bool ParseMss(const uint8_t* options, unsigned bytes, uint16_t& mss) {
        unsigned offset = 0;
        while (offset < bytes) {
            const unsigned kind = options[offset++];
            if (kind == 0) break;
            if (kind == 1) continue;
            if (offset == bytes) return false;
            const unsigned length = options[offset++];
            if (length < 2 || length - 2 > bytes - offset) return false;
            if (kind == 2) {
                if (length != 4 || !Read16(options + offset)) return false;
                const auto value = Read16(options + offset);
                if (value < mss) mss = value;
            }
            offset += length - 2;
        }
        return true;
    }
}

TcpClient::TcpClient(InternetProtocolProvider& ip)
    : InternetProtocolHandler(&ip, 6), now(0), nextHandle(1) {
    for (auto& connection : connections) connection.phase = Free;
}

const TcpClient::Connection* TcpClient::Find(int handle) const {
    for (const auto& connection : connections)
        if (connection.phase != Free && connection.handle == handle) return &connection;
    return nullptr;
}

TcpClient::Connection* TcpClient::Find(int handle) {
    return const_cast<Connection*>(static_cast<const TcpClient*>(this)->Find(handle));
}

void TcpClient::SendSegment(const Connection& connection, uint32_t sequence, uint8_t flags,
                            const uint8_t* data, unsigned bytes) {
    const unsigned header = HeaderBytes + ((flags & Syn) ? 4 : 0);
    uint8_t packet[HeaderBytes + 4 + SendMssLimit];
    for (unsigned i = 0; i < header; ++i) packet[i] = 0;
    Write16(packet, connection.localPort);
    Write16(packet + 2, connection.remotePort);
    Write32(packet + 4, sequence);
    if (flags & Ack) Write32(packet + 8, connection.receiveNext);
    packet[12] = static_cast<uint8_t>((header / 4) << 4);
    packet[13] = flags;
    Write16(packet + 14, ReceiveBytes - connection.received);
    if (flags & Syn) {
        packet[HeaderBytes] = 2;
        packet[HeaderBytes + 1] = 4;
        // On the directly connected Ethernet, receive a full frame's worth.
        // Keep the IPv4 default on routes that may have a smaller path MTU.
        const unsigned receiveMss = backend->IsOnLink(connection.remoteIP)
            ? InternetProtocolProvider::LinkMtu - sizeof(InternetProtocolV4Message) - HeaderBytes
            : SendMssLimit;
        Write16(packet + HeaderBytes + 2, receiveMss);
    }
    for (unsigned i = 0; i < bytes; ++i) packet[header + i] = data[i];
    Write16(packet + 16, Checksum(backend->GetIPAddress(), connection.remoteIP, packet, header + bytes));
    Send(connection.remoteIP, packet, header + bytes);
}

void TcpClient::SendPending(Connection& connection) {
    SendSegment(connection, connection.transmitUnacked, connection.pendingFlags,
                connection.transmit, connection.transmitBytes);
    const unsigned shift = connection.retries < 3 ? connection.retries : 3;
    connection.retryAt = now + (250U << shift);
    ++connection.retries;
}

int TcpClient::Open(uint32_t remoteIP, uint16_t remotePort, uint16_t localPort, uint32_t initialSequence) {
    memory::InterruptGuard guard;
    const uint8_t first = static_cast<uint8_t>(remoteIP);
    if (!remotePort || !localPort || !first || first == 127 || first >= 224
        || !backend->GetIPAddress() || remoteIP == backend->GetIPAddress()) return Invalid;
    Connection* available = nullptr;
    for (auto& connection : connections) {
        if (connection.phase == Free) {
            if (!available) available = &connection;
        } else if (connection.localPort == localPort && connection.phase != Failed) {
            return Invalid;
        }
    }
    if (!available || nextHandle > 0x7fffffffU) return Exhausted;

    available->remoteIP = remoteIP;
    available->remotePort = remotePort;
    available->localPort = localPort;
    available->peerWindow = available->peerMss = 0;
    available->handle = static_cast<int>(nextHandle++);
    available->transmitUnacked = initialSequence;
    available->transmitNext = initialSequence + 1;
    available->receiveNext = 0;
    available->transmitBytes = available->receiveStart = available->received = 0;
    available->peerFinished = available->writeFinished = false;
    available->error = 0;
    available->retries = 0;
    available->pendingFlags = Syn;
    available->phase = Connecting;
    SendPending(*available);
    return available->handle;
}

int TcpClient::Status(int handle) const {
    unsigned readable;
    bool endOfStream;
    return Describe(handle, readable, endOfStream);
}

int TcpClient::Describe(int handle, unsigned& readable, bool& endOfStream) const {
    memory::InterruptGuard guard;
    const auto* connection = Find(handle);
    if (!connection) return Invalid;
    readable = connection->received;
    endOfStream = connection->peerFinished && !connection->received;
    if (connection->phase == Failed) return connection->error;
    return connection->phase == Established ? 0 : Again;
}

int TcpClient::Write(int handle, const uint8_t* data, unsigned bytes) {
    memory::InterruptGuard guard;
    auto* connection = Find(handle);
    if (!connection || (bytes && !data)) return Invalid;
    if (connection->phase == Failed) return connection->error;
    if (connection->phase != Established || connection->pendingFlags) return Again;
    if (connection->writeFinished) return Closed;
    if (!bytes) return 0;

    // One byte probes a closed peer window. It remains unacknowledged and is
    // retransmitted by the same bounded timer until the receiver has room.
    const unsigned window = connection->peerWindow ? connection->peerWindow : 1;
    if (bytes > window) bytes = window;
    if (bytes > connection->peerMss) bytes = connection->peerMss;
    for (unsigned i = 0; i < bytes; ++i) connection->transmit[i] = data[i];
    connection->transmitBytes = static_cast<uint16_t>(bytes);
    connection->transmitNext += bytes;
    connection->pendingFlags = Ack | Psh;
    connection->retries = 0;
    SendPending(*connection);
    return static_cast<int>(bytes);
}

int TcpClient::Read(int handle, uint8_t* data, unsigned bytes) {
    memory::InterruptGuard guard;
    auto* connection = Find(handle);
    if (!connection || (bytes && !data)) return Invalid;
    if (!bytes) return 0;
    if (!connection->received) {
        if (connection->phase == Failed) return connection->error;
        return connection->peerFinished ? 0 : Again;
    }
    if (bytes > connection->received) bytes = connection->received;
    for (unsigned i = 0; i < bytes; ++i)
        data[i] = connection->receive[(connection->receiveStart + i) % ReceiveBytes];
    connection->receiveStart = (connection->receiveStart + bytes) % ReceiveBytes;
    connection->received -= bytes;
    if (connection->phase == Established)
        SendSegment(*connection, connection->transmitNext, Ack, nullptr, 0);
    return static_cast<int>(bytes);
}

int TcpClient::ShutdownWrite(int handle) {
    memory::InterruptGuard guard;
    auto* connection = Find(handle);
    if (!connection) return Invalid;
    if (connection->phase == Failed) return connection->error;
    if (connection->phase != Established || connection->pendingFlags) return Again;
    if (connection->writeFinished) return 0;

    connection->writeFinished = true;
    ++connection->transmitNext;
    connection->pendingFlags = Fin | Ack;
    connection->retries = 0;
    SendPending(*connection);
    return Again;
}

bool TcpClient::Close(int handle) {
    memory::InterruptGuard guard;
    auto* connection = Find(handle);
    if (!connection) return false;
    if (connection->phase == Established &&
        (!connection->writeFinished || !connection->peerFinished || connection->pendingFlags))
        SendSegment(*connection, connection->transmitNext, Rst | Ack, nullptr, 0);
    connection->phase = Free;
    return true;
}

void TcpClient::Poll(uint32_t milliseconds) {
    memory::InterruptGuard guard;
    now = milliseconds;
    for (auto& connection : connections) {
        if ((connection.phase != Connecting && connection.phase != Established) ||
            !connection.pendingFlags || !Due(now, connection.retryAt)) continue;
        if (connection.retries >= MaximumAttempts) {
            connection.phase = Failed;
            connection.error = TimedOut;
        } else {
            SendPending(connection);
        }
    }
}

bool TcpClient::OnInternetProtocolReceived(uint32_t source, uint32_t destination,
                                          uint8_t* packet, uint32_t bytes) {
    memory::InterruptGuard guard;
    if (!packet || bytes < HeaderBytes || bytes > 65515 || (packet[12] & 15)
        || (packet[13] & 0xe0) || Checksum(source, destination, packet, bytes)) return false;
    const unsigned header = (packet[12] >> 4) * 4;
    if (header < HeaderBytes || header > bytes) return false;
    const uint16_t remote = Read16(packet), local = Read16(packet + 2);
    const uint32_t sequence = Read32(packet + 4), acknowledgment = Read32(packet + 8);
    const uint8_t flags = packet[13];
    const unsigned payload = bytes - header;

    for (auto& connection : connections) {
        if ((connection.phase != Connecting && connection.phase != Established) ||
            connection.remoteIP != source || destination != backend->GetIPAddress() ||
            connection.remotePort != remote || connection.localPort != local) continue;

        if (connection.phase == Connecting) {
            if (!(flags & Ack) || acknowledgment != connection.transmitNext) return false;
            if (flags & Rst) {
                connection.phase = Failed;
                connection.error = Refused;
                return false;
            }
            uint16_t mss = SendMssLimit;
            if (flags != (Syn | Ack) || payload || !ParseMss(packet + HeaderBytes, header - HeaderBytes, mss))
                return false;
            connection.phase = Established;
            connection.receiveNext = sequence + 1;
            connection.transmitUnacked = acknowledgment;
            connection.peerWindow = Read16(packet + 14);
            connection.peerMss = mss;
            connection.pendingFlags = 0;
            SendSegment(connection, connection.transmitNext, Ack, nullptr, 0);
            return false;
        }

        if (flags & Rst) {
            if (sequence == connection.receiveNext) {
                connection.phase = Failed;
                connection.error = Reset;
            }
            return false;
        }
        if (!(flags & Ack)) return false;
        const uint32_t advance = acknowledgment - connection.transmitUnacked;
        if ((flags & Syn) || sequence != connection.receiveNext ||
            advance > connection.transmitNext - connection.transmitUnacked) {
            SendSegment(connection, connection.transmitNext, Ack, nullptr, 0);
            return false;
        }

        if (advance) {
            connection.transmitUnacked = acknowledgment;
            if (acknowledgment == connection.transmitNext) {
                connection.transmitBytes = 0;
                connection.pendingFlags = 0;
            } else {
                connection.transmitBytes -= advance;
                for (unsigned i = 0; i < connection.transmitBytes; ++i)
                    connection.transmit[i] = connection.transmit[i + advance];
                connection.retryAt = now + 250;
                connection.retries = 0;
            }
        }
        connection.peerWindow = Read16(packet + 14);

        if (!payload && !(flags & Fin)) return false;
        if (!connection.peerFinished && payload <= ReceiveBytes - connection.received) {
            const unsigned end = (connection.receiveStart + connection.received) % ReceiveBytes;
            for (unsigned i = 0; i < payload; ++i)
                connection.receive[(end + i) % ReceiveBytes] = packet[header + i];
            connection.received += payload;
            connection.receiveNext += payload;
            if (flags & Fin) {
                ++connection.receiveNext;
                connection.peerFinished = true;
            }
        }
        SendSegment(connection, connection.transmitNext, Ack, nullptr, 0);
        return false;
    }
    return false;
}

} }
