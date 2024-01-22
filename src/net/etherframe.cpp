#include <net/etherframe.h>

using namespace gtos;
using namespace gtos::drivers;
using namespace gtos::net;

void printf(char*);
void printfHex(uint8_t);
void printfHex16(uint16_t);
void printfHex32(uint32_t);

EtherFrameHandler::EtherFrameHandler(EtherFrameProvider* backend, uint16_t etherType) {
    this->etherType_BE = ((etherType & 0x00FF) << 8) | ((etherType & 0xFF00) >> 8);
    this->backend = backend;

    backend->handlers[etherType_BE] = this;
}

EtherFrameHandler::~EtherFrameHandler() {
    if (backend->handlers[etherType_BE] == this)
        backend->handlers[etherType_BE] = 0;
}

bool EtherFrameHandler::OnEtherFrameReceived(uint8_t* etherframePayload, uint32_t size) {
    
    return false;
}

void EtherFrameHandler::Send(uint64_t dstMAC_BE, uint8_t* data, uint32_t size) {
    backend->Send(dstMAC_BE, etherType_BE, data, size);
}

uint32_t EtherFrameHandler::GetIPAddress() {
    return backend->GetIPAddress();
}
EtherFrameProvider::EtherFrameProvider(amd_am79c973* backend) 
: RawDataHandler(backend) {
    for (uint32_t i = 0;i < 65535;i++) {
        handlers[i] = 0;
    }

}

EtherFrameProvider::~EtherFrameProvider() {

}

//处理接收数据函数
bool EtherFrameProvider::OnRawDataReceived(uint8_t* buffer, uint32_t size) {
    //printf("\n");
    if (size < sizeof(EtherFrameHeader)) return false;

    EtherFrameHeader* frame = (EtherFrameHeader*)buffer;
    // for (int i = 0;i < size;i++) {
    //     printfHex(((uint8_t*)frame)[i]);
    //     printf(" ");
    // }
    bool sendBack = false;

    if (frame->dstMAC_BE == 0xFFFFFFFFFFFF || frame->dstMAC_BE == backend->GetMACAddress()) {
        //OnEtherFramReceived函数执行时出现问题
        // printf("!!!!!!!!!!!!!!!!");
        // printfHex((uint8_t)(frame->etherType_BE & 0x00FF));
        // printfHex((uint8_t)((frame->etherType_BE & 0xFF00 )>> 8));


        //根据数据帧里的协议类型（frame->etherType_BE）选择不同类的发送函数
        if (handlers[frame->etherType_BE] != 0) sendBack = handlers[frame->etherType_BE]->OnEtherFrameReceived(buffer + sizeof(EtherFrameHeader), size - sizeof(EtherFrameHeader));
    }

    if (sendBack) {
        frame->dstMAC_BE = frame->srcMAC_BE;
        frame->srcMAC_BE = backend->GetMACAddress();
    }

    return sendBack;
}

void EtherFrameProvider::Send(uint64_t dstMAC_BE, uint16_t etherType_BE, uint8_t* buffer, uint32_t size) {


    //将buffer复制到buffer2
    uint8_t* buffer2 = (uint8_t*)MemoryManager::activeMemoryManager->malloc(sizeof(EtherFrameHeader) + size);
    EtherFrameHeader* frame = (EtherFrameHeader*)buffer2;

    frame->dstMAC_BE = dstMAC_BE;

    //获取本机MAC
    frame->srcMAC_BE = backend->GetMACAddress();
    frame->etherType_BE = etherType_BE;

    uint8_t* src = buffer;
    uint8_t* dst = buffer2 + sizeof(EtherFrameHeader);
    for (uint32_t i = 0;i < size;i++) {
        dst[i] = src[i];
		}
    backend->Send(buffer2, size + sizeof(EtherFrameHeader));

    MemoryManager::activeMemoryManager->free(buffer2);
}

uint32_t EtherFrameProvider::GetIPAddress() {
    return backend->GetIPAddress();
}


uint64_t EtherFrameProvider::GetMACAddress()
{
    return backend->GetMACAddress();
}

void EtherFrameProvider::SetHandlers(EtherFrameHandler* handler, uint16_t etherType) {
    this->handlers[((etherType & 0x00FF) << 8) | ((etherType & 0xFF00) >> 8)] = handler;
}