#ifndef __GTOS__NET__ICMP_H
#define __GTOS__NET__ICMP_H

#include <common/types.h>
#include <net/ipv4.h>
#include <net/etherframe.h>
#include <net/arp.h>

namespace gtos
{
    namespace net
    {
        //icmp报头
        struct InternetControlMessageProtocolMessage
        {
            uint8_t type;
            uint8_t code;

            uint16_t checksum;
            uint32_t data; 
        } __attribute__((packed));

        class InternetControlMessageProtocol : InternetProtocolHandler
        {
        public:
            InternetControlMessageProtocol(InternetProtocolProvider* backend);
            ~InternetControlMessageProtocol();

            virtual bool OnInternetProtocolReceived(uint32_t srcIP_BE, uint32_t dstIP_BE,
                                            uint8_t* internetprotocolPayload, uint32_t size);

            void RequestEchoReply(uint32_t ip_be);
        };
    }
}

#endif