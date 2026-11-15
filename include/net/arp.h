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
            enum { CacheSize = 128 };
            struct Neighbor {
                uint32_t ip;
                uint64_t mac;
            };
            Neighbor neighbors[CacheSize];
            uint32_t nextNeighbor;
            Neighbor* Find(uint32_t ip);
            void RequestMACAddress(uint32_t ip);
        public:
            AddressResolutionProtocol(EtherFrameProvider* backend);
            ~AddressResolutionProtocol();

            bool OnEtherFrameReceived(uint8_t* etherframePayload, uint32_t size) override;
            // An unresolved address triggers one request. The caller may retry later.
            bool Resolve(uint32_t ip, uint64_t& mac);
        };
    }
}

#endif
