#include <net/arp.h>
#include <memory/criticalsection.h>

using namespace gtos;
using namespace gtos::net;
using gtos::memory::InterruptGuard;

namespace {
    constexpr uint64_t BroadcastMAC = 0xFFFFFFFFFFFFULL;
    constexpr uint16_t Request = 0x0100, Reply = 0x0200;

    bool UnicastIPv4(uint32_t ip) {
        return ip && ip != 0xFFFFFFFFU && (ip & 0xF0U) != 0xE0U;
    }
}

AddressResolutionProtocol::AddressResolutionProtocol(EtherFrameProvider* backend)
    : EtherFrameHandler(backend, 0x0806), neighbors{}, nextNeighbor(0) {}

AddressResolutionProtocol::~AddressResolutionProtocol() {}

AddressResolutionProtocol::Neighbor* AddressResolutionProtocol::Find(uint32_t ip) {
    for (auto& neighbor : neighbors)
        if (neighbor.ip == ip) return &neighbor;
    return 0;
}

bool AddressResolutionProtocol::OnEtherFrameReceived(uint8_t* payload, uint32_t size) {
    if (!payload || size < sizeof(AddressResolutionProtocolMessage)) return false;
    auto& arp = *reinterpret_cast<AddressResolutionProtocolMessage*>(payload);
    const uint32_t localIP = backend->GetIPAddress();
    const uint64_t localMAC = backend->GetMACAddress();
    if (!localIP || arp.dstIP != localIP || arp.hardwareType != 0x0100 ||
        arp.protocol != 0x0008 || arp.hardwareAddressSize != 6 ||
        arp.protocolAddressSize != 4 || !arp.srcMAC || (arp.srcMAC & 1U))
        return false;
    if (arp.command == Request && (!arp.srcIP || UnicastIPv4(arp.srcIP))) {
        arp.command = Reply;
        arp.dstIP = arp.srcIP;
        arp.dstMAC = arp.srcMAC;
        arp.srcIP = localIP;
        arp.srcMAC = localMAC;
        return true;
    }
    if (arp.command == Reply && arp.dstMAC == localMAC && UnicastIPv4(arp.srcIP)) {
        InterruptGuard guard;
        Neighbor* neighbor = Find(arp.srcIP);
        if (neighbor && !neighbor->mac) neighbor->mac = arp.srcMAC;
    }
    return false;
}

void AddressResolutionProtocol::RequestMACAddress(uint32_t ip) {
    AddressResolutionProtocolMessage arp = {};
    arp.hardwareType = 0x0100;
    arp.protocol = 0x0008;
    arp.hardwareAddressSize = 6;
    arp.protocolAddressSize = 4;
    arp.command = Request;
    arp.srcMAC = backend->GetMACAddress();
    arp.srcIP = backend->GetIPAddress();
    arp.dstIP = ip;
    Send(BroadcastMAC, reinterpret_cast<uint8_t*>(&arp), sizeof(arp));
}

bool AddressResolutionProtocol::Resolve(uint32_t ip, uint64_t& mac) {
    const uint32_t localIP = backend->GetIPAddress();
    if (!UnicastIPv4(ip) || !localIP || ip == localIP) return false;
    {
        InterruptGuard guard;
        Neighbor* neighbor = Find(ip);
        if (!neighbor) {
            neighbors[nextNeighbor] = {ip, 0};
            nextNeighbor = (nextNeighbor + 1) % CacheSize;
        } else if (neighbor->mac) {
            mac = neighbor->mac;
            return true;
        }
    }
    RequestMACAddress(ip);
    return false;
}
