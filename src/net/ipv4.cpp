#include <net/ipv4.h>

using namespace gtos;
using namespace gtos::net;

namespace {
    uint16_t Network16(uint16_t value) {
        return (value >> 8) | (value << 8);
    }
}

InternetProtocolHandler::InternetProtocolHandler(InternetProtocolProvider* backend, uint8_t protocol) {
    this->backend = backend;
    this->ip_protocol = protocol;
    backend->handlers[protocol] = this;
}

InternetProtocolHandler::~InternetProtocolHandler() {
    if (backend->handlers[ip_protocol] == this) backend->handlers[ip_protocol] = 0;
}

bool InternetProtocolHandler::OnInternetProtocolReceived(uint32_t srcIP_BE, uint32_t dstIP_BE, uint8_t* internetprotocolPayload, uint32_t size) {
    return false;
}

void InternetProtocolHandler::Send(uint32_t dstIP_BE, uint8_t* internetprotocolPayload, uint32_t size) {
    backend->Send(dstIP_BE, ip_protocol, internetprotocolPayload, size);
}

InternetProtocolProvider::InternetProtocolProvider(EtherFrameProvider* backend, AddressResolutionProtocol* arp, uint32_t gatewayIP, uint32_t subnetMask) 
: EtherFrameHandler(backend, 0x800) {
    //backend->SetHandlers(this, 0x0800);
    for (auto& handler : handlers) handler = 0;
    this->arp = arp;
    this->gatewayIP = gatewayIP;
    this->subnetMask = subnetMask;
}
InternetProtocolProvider::~InternetProtocolProvider() {

}

//接受到数据后判断是否为发到本地ip的数据包
//然后根据协议号调用对应类的on_ip_received函数
bool InternetProtocolProvider::OnEtherFrameReceived(uint8_t* etherframePayload, uint32_t size) {
    if (!etherframePayload || size < sizeof(InternetProtocolV4Message)) return false;
    InternetProtocolV4Message* ipmessage = (InternetProtocolV4Message*)etherframePayload;
    const uint32_t headerBytes = 4U * ipmessage->headerLength;
    const uint32_t length = Network16(ipmessage->totalLength);
    // The stack has no reassembly. Only the Don't Fragment flag may be set.
    if (ipmessage->version != 4 || headerBytes < sizeof(InternetProtocolV4Message) ||
        headerBytes > length || length > size || !ipmessage->timeToLive ||
        (Network16(ipmessage->flagsAndOffset) & 0xBFFFU) || Checksum(ipmessage, headerBytes))
        return false;
    if (ipmessage->dstIP != backend->GetIPAddress()) return false;
    InternetProtocolHandler* handler = handlers[ipmessage->protocol];
    if (!handler) return false;
    const bool sendBack = handler->OnInternetProtocolReceived(ipmessage->srcIP, ipmessage->dstIP,
        etherframePayload + headerBytes, length - headerBytes);

    if (sendBack) {
        uint32_t temp = ipmessage->dstIP;
        ipmessage->dstIP = ipmessage->srcIP;
        ipmessage->srcIP = temp;

        ipmessage->timeToLive = 0x40;
        ipmessage->checksum = 0;
        ipmessage->checksum = Checksum(ipmessage, headerBytes);
    }
    return sendBack;
}

void InternetProtocolProvider::Send(uint32_t dstIP_BE, uint8_t protocol, uint8_t* data, uint32_t size) {
    constexpr uint32_t mtu = 1500;
    auto* memory = MemoryManager::activeMemoryManager;
    if (!memory || !arp || (size && !data) || size > mtu - sizeof(InternetProtocolV4Message)) return;
    const uint32_t sourceIP = backend->GetIPAddress();
    const uint32_t route = (dstIP_BE & subnetMask) == (sourceIP & subnetMask) ? dstIP_BE : gatewayIP;
    uint64_t destinationMAC = 0;
    if (!arp->Resolve(route, destinationMAC)) return;
    //构建ipv4表头
    //创建一个缓冲区，内容为ipv4表头加上调用ip::send函数时传来的data参数
    uint8_t* buffer = (uint8_t*)memory->malloc(sizeof(InternetProtocolV4Message) + size);
    if (!buffer) return;
    InternetProtocolV4Message* message = (InternetProtocolV4Message*)buffer;
    message->version = 4;
    message->headerLength = sizeof(InternetProtocolV4Message) / 4;
    message->tos = 0;
    message->totalLength = Network16(size + sizeof(InternetProtocolV4Message));
    message->ident = 0x0100;
    message->flagsAndOffset = 0x0040;
    message->timeToLive = 0x40;
    message->protocol = protocol;

    message->dstIP = dstIP_BE;
    message->srcIP = sourceIP;

    message->checksum = 0;
    message->checksum = Checksum(message, sizeof(InternetProtocolV4Message));
    
    //去除表头后填入要传输的数据
    uint8_t* databuffer = buffer + sizeof(InternetProtocolV4Message); 
    for (uint32_t i = 0; i < size; ++i) databuffer[i] = data[i];

    backend->Send(destinationMAC, this->etherType_BE, buffer, sizeof(InternetProtocolV4Message) + size);

    memory->free(buffer);
}

uint16_t InternetProtocolProvider::Checksum(const void* data, uint32_t lengthInBytes) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t sum = 0;
    while (lengthInBytes >= 2) {
        sum += (uint32_t(bytes[0]) << 8) | bytes[1];
        sum = (sum & 0xFFFFU) + (sum >> 16);
        bytes += 2;
        lengthInBytes -= 2;
    }
    if (lengthInBytes) sum += uint32_t(*bytes) << 8;
    sum = (sum & 0xFFFFU) + (sum >> 16);
    return Network16(~sum);
}
