
#include <net/icmp.h>

using namespace gtos;
using namespace gtos::net;
            
InternetControlMessageProtocol::InternetControlMessageProtocol(InternetProtocolProvider* backend)
:   InternetProtocolHandler(backend, 0x01)
{

}

InternetControlMessageProtocol::~InternetControlMessageProtocol()
{

}

void printf(char*);
void printfHex(uint8_t);

bool InternetControlMessageProtocol::OnInternetProtocolReceived(uint32_t srcIP_BE, uint32_t,
                                            uint8_t* internetprotocolPayload, uint32_t size)
{
    if (!internetprotocolPayload || size < sizeof(InternetControlMessageProtocolMessage)
        || InternetProtocolProvider::Checksum(internetprotocolPayload, size))
        return false;
    
    InternetControlMessageProtocolMessage* msg = (InternetControlMessageProtocolMessage*)internetprotocolPayload;
    if (msg->code) return false;

    switch(msg->type)
    {
    case 0:
        printf("ping response from ");printfHex(srcIP_BE & 0xFF);
        printf(".");printfHex((srcIP_BE >> 8) & 0xFF);
        printf(".");printfHex((srcIP_BE >> 16) & 0xFF);
        printf(".");printfHex((srcIP_BE >> 24) & 0xFF);
        printf("\n");
        break;

    case 8:
        msg->type = 0;
        msg->checksum = 0;
        msg->checksum = InternetProtocolProvider::Checksum(msg, size);
        return true;
        break;
    }

    return false;
}
// ping
void InternetControlMessageProtocol::RequestEchoReply(uint32_t ip_be)
{
    InternetControlMessageProtocolMessage icmp;
    icmp.type = 8; // ping
    icmp.code = 0;
    icmp.data = 0x3713; // 1337
    icmp.checksum = 0;
    icmp.checksum = InternetProtocolProvider::Checksum(&icmp, sizeof(icmp));
    
    InternetProtocolHandler::Send(ip_be, (uint8_t*)&icmp, sizeof(InternetControlMessageProtocolMessage));
}
