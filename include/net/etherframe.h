#ifndef __GTOS__NET__ETHERFRAME_H
#define __GTOS__NET__ETHERFRAME_H

#include <common/types.h>
#include <drivers/amd_am79c973.h>
#include <multitasking.h>

using namespace gtos::drivers;

namespace gtos {
    namespace net {
        struct EtherFrameHeader {
            uint64_t dstMAC_BE : 48;
            uint64_t srcMAC_BE : 48;
            uint16_t etherType_BE;
        }__attribute__((packed));

        typedef uint32_t EtherFrameFooter;

        class EtherFrameProvider;

        class EtherFrameHandler {
        protected:
            EtherFrameProvider* backend;
            uint16_t etherType_BE;
        public:
            EtherFrameHandler(EtherFrameProvider* backend, uint16_t etherType_BE);
            ~EtherFrameHandler();

            virtual bool OnEtherFrameReceived(uint8_t* etherframePayload, uint32_t size);
            void Send(uint64_t dstMAC_BE, uint8_t* etherframePayload, uint32_t size);
        };

        class EtherFrameProvider : public RawDataHandler {
        friend class EtherFrameHandler;
        public:
        protected:
            EtherFrameHandler* handlers[65535];
        public:
            EtherFrameProvider(amd_am79c973* backend);
            ~EtherFrameProvider();

            bool OnRawDataReceived(uint8_t* buffer, uint32_t size);
            void Send(uint64_t dstMAC_BE, uint16_t etherType_BE, uint8_t* buffer, uint32_t size);

            uint64_t GetMACAddress();
            uint32_t GetIPAddress();
        };
    }
}

#endif