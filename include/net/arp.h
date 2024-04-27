#ifndef __GTOS__NET__ARP_H
#define __GTOS__NET__ARP_H

#include <common/types.h>
#include <net/etherframe.h>

namespace gtos {
    namespace net {
        struct AddressResolutionProtocolMessage {
            uint16_t hardwareType;
            uint16_t protocol;
            uint8_t hardwareAddressSize; //6
            uint8_t protocolAddressSize; //4
            uint16_t command;

            uint64_t srcMAC : 48;
            uint32_t srcIP;
            uint64_t dstMAC : 48;
            uint32_t dstIP;
        } __attribute__((packed));

        class AddressResolutionProtocol : public EtherFrameHandler {
            uint32_t IPcache[128];
            uint64_t MACcache[128];
            int numCacheEntries;
        public:
            AddressResolutionProtocol(EtherFrameProvider* backend);
            ~AddressResolutionProtocol();

            bool OnEtherFrameReceived(uint8_t* etherframePayload, uint32_t size);

            void RequestMACAddress(uint32_t IP_BE);
            uint64_t GetMACFromCache(uint32_t IP_BE);
            uint64_t Resolve(uint32_t IP_BE);
        };
    }
}

#endif