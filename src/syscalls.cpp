#include <syscalls.h>

using namespace gtos;
using namespace gtos::hardwarecommunication;

void printf(char*);
void printfHex32(uint32_t);

SyscallHandler::SyscallHandler(InterruptsManager* interruptManager, uint8_t InterruptNumber) 
: InterruptHandler(interruptManager, interruptNumber + interruptManager->HardwareInterruptOffset()){
    interruptManager->Load(this,0x80 + interruptManager->HardwareInterruptOffset());
}

SyscallHandler::~SyscallHandler() {

}



uint32_t SyscallHandler::HandlerInterrupt(uint32_t esp) {
    CPUState* cpu = (CPUState*)esp;

    switch(cpu->eax) {
        case 4:
            printf((char*)cpu->ebx);
            break;

        default:
            break;
    }

    return esp;
}