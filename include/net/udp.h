#ifndef __GTOS__NET__UDP_H
#define __GTOS__NET__UDP_H

#include <common/types.h>
#include <net/ipv4.h>
#include <memorymanagement.h> 

namespace gtos{
    namespace net{
        struct UserDatagramProtocolHeader{
            uint16_t srcPort;
            uint16_t dstPort;
            uint16_t length;
            uint16_t checksum;
        } __attribute((packed));

        class UserDatagramProtocolSocket;

        class UserDatagramProtocolProvider;

        class UserDatagramProtocolHandler {
        friend class UserDatagramProtocolProvider;
        public:
            UserDatagramProtocolHandler();
            ~UserDatagramProtocolHandler();
            virtual void HandleUserDatagramProtocolMessage(UserDatagramProtocolSocket* socket, uint8_t* data, uint16_t size);
        };

        class UserDatagramProtocolSocket{
            friend class UserDatagramProtocolProvider;
        protected:
            uint16_t remotePort;
            uint32_t remoteIP;
            uint16_t localPort;
            uint32_t localIP;
            UserDatagramProtocolProvider* backend;
            UserDatagramProtocolHandler* handler;
            bool Listening;
        public:
            UserDatagramProtocolSocket(UserDatagramProtocolProvider* backend);
            ~UserDatagramProtocolSocket();
            virtual bool HandleUserDatagramProtocolMessage(uint8_t* data, uint32_t size);
            virtual bool Send(uint8_t* data, uint32_t size);
            virtual void Disconnect();
        };

        //继承iphander类方便挂在在ip协议中
        class UserDatagramProtocolProvider : InternetProtocolHandler {
        friend class UserDatagramProtocolSocket;
        protected:
            UserDatagramProtocolSocket* sockets[65535];
            uint16_t numSockets;
            uint16_t freePort;
        public:
            UserDatagramProtocolProvider(InternetProtocolProvider* backend);
            ~UserDatagramProtocolProvider();
            virtual bool OnInternetProtocolReceived(uint32_t srcIP_BE, uint32_t dstIP_BE, uint8_t* internetprotocolPayload, uint32_t size);
            
            virtual UserDatagramProtocolSocket* Connect(uint32_t ip, uint16_t port);
            virtual UserDatagramProtocolSocket* Listen(uint16_t port);
            virtual void Disconnect(UserDatagramProtocolSocket* socket);
            virtual void Send(UserDatagramProtocolSocket* socket, uint8_t* data, uint16_t size);
            virtual void Bind(UserDatagramProtocolSocket* socket, UserDatagramProtocolHandler* handler);
        };
    }
}

#endif