#include <net/arp.h>

using namespace gtos;
using namespace gtos::net;
using namespace gtos::drivers;

void printf(char*);
void printfHex16(uint16_t);
void printfHex(uint8_t);

AddressResolutionProtocol::AddressResolutionProtocol(EtherFrameProvider* backend)
: EtherFrameHandler(backend, 0x806) {
    // backend->SetHandlers(this, 0x0806);
    numCacheEntries = 0;
}

AddressResolutionProtocol::~AddressResolutionProtocol() {

}

bool AddressResolutionProtocol::OnEtherFrameReceived(uint8_t* etherframePayload, uint32_t size) {
    if (size < sizeof(AddressResolutionProtocolMessage)) return false;
    // printf("!!!!!!!!!!!!!!!!!!");

    //接受到的数据去除表头后为arpMessage
    AddressResolutionProtocolMessage* arp = (AddressResolutionProtocolMessage*)etherframePayload;
    // printf("!!!!!!!!!!!!!!!!!!");
    // printfHex16(arp->hardwareType);
    // printf("\n");
    // printf("\n");
    // for(int i = 0; i < (size>64?64:size); i++){
    //     printfHex(((uint8_t*)etherframePayload)[i]);
    //     printf(" ");
    // }
    if (arp->hardwareType == 0x0100) {
        if (arp->protocol == 0x0008 //ipv4协议
        && arp->hardwareAddressSize == 6 && arp->protocolAddressSize == 4 
        && arp->dstIP == backend->GetIPAddress()) {
            switch (arp->command) {
                case 0x0100: // request
                    arp->command = 0x0200;
                    arp->dstIP = arp->srcIP;
                    arp->dstMAC = arp->srcMAC;
                    arp->srcIP = backend->GetIPAddress();
                    arp->srcMAC = backend->GetMACAddress();
                    
                    return true;
                    break;
                case 0x0200: //response
                    if (numCacheEntries < 128) {
                        IPcache[numCacheEntries] = arp->srcIP;
                        MACcache[numCacheEntries] = arp->srcMAC;
                        numCacheEntries++;
                    }
                    
                    break;
            }
        }
    }
    return false;
}

//arp广播获取其他设备MAC地址
void AddressResolutionProtocol::RequestMACAddress(uint32_t IP_BE) {
    AddressResolutionProtocolMessage arp;
    arp.hardwareType = 0x0100; // ethernet
    arp.protocol = 0x0008; //ipv4
    arp.hardwareAddressSize = 6; // mac
    arp.protocolAddressSize = 4; // ipv4
    arp.command = 0x0100; //request

    arp.srcMAC = backend->GetMACAddress();
    arp.srcIP = backend->GetIPAddress();
    arp.dstMAC = 0xFFFFFFFFFFFF; // broadcast
    arp.dstIP = IP_BE;

    this->Send(arp.dstMAC, (uint8_t*)&arp, sizeof(AddressResolutionProtocolMessage));
}

uint64_t AddressResolutionProtocol::GetMACFromCache(uint32_t IP_BE) {
    for (int i = 0;i < numCacheEntries;i++) if (IPcache[i] == IP_BE) return MACcache[i];
    return 0xFFFFFFFFFFFF;
}

uint64_t AddressResolutionProtocol::Resolve(uint32_t IP_BE) {
    uint64_t result = GetMACFromCache(IP_BE);
    if (result == 0xFFFFFFFFFFFF) RequestMACAddress(IP_BE);

    while (result == 0xFFFFFFFFFFFF) //possible infinite loop
        result = GetMACFromCache(IP_BE);

    return result;
}