#include <syscalls.h>
#include <process/native_runtime.h>

using namespace gtos;
using namespace gtos::hardwarecommunication;

void printf(char*);
void printfHex32(uint32_t);

SyscallHandler::SyscallHandler(InterruptsManager* interruptManager, uint8_t InterruptNumber) 
: InterruptHandler(interruptManager, InterruptNumber){
}

SyscallHandler::~SyscallHandler() {

}



uint32_t SyscallHandler::HandlerInterrupt(uint32_t esp) {
    CPUState* cpu = (CPUState*)esp;

    if ((cpu->cs & 3) == 3) {
        process::NativeRuntime* runtime = process::NativeRuntime::Active();
        CPUState* result = runtime ? runtime->HandleSyscall(cpu) : 0;
        if (result) return (uint32_t)result;
        cpu->eax = (uint32_t)GTOS_ERR_BAD_STATE;
        return esp;
    }
    // Historical self-test only: raw kernel pointers are NEVER a user ABI.
    switch(cpu->eax) {
        case 4:
            printf((char*)cpu->ebx);
            break;

        default:
            break;
    }

    return esp;
}