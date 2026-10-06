#include <hardwarecommunication/interrupts.h>
#include <process/native_runtime.h>

using namespace gtos;
using namespace gtos::hardwarecommunication;

void printf(char* str);
void printfHex(uint8_t);
void printfHex32(uint32_t);

InterruptHandler::InterruptHandler(InterruptsManager* interruptsManager, uint8_t InterruptNumber) {
    this->interruptNumber = InterruptNumber;
    this->interruptManager = interruptsManager;
    interruptsManager->handlers[interruptNumber] = this;
}



InterruptHandler::~InterruptHandler() {
    if (interruptManager->handlers[interruptNumber] == this) {
        interruptManager->handlers[interruptNumber] = 0;
    }
}

uint32_t InterruptHandler::HandlerInterrupt(uint32_t esp) {

    return esp;
}

InterruptsManager::GateDescriptor InterruptsManager::interruptDescriptorTable[256];

InterruptsManager* InterruptsManager::ActivateInterruptsManager = 0;

//请求中断时包含的数据
void InterruptsManager::SetInterruptDescriptorTableEntry(
    uint8_t interruptNumber,
    uint16_t codeSegmentSelectorOffset,
    void (*handler)(),
    uint8_t DescriptorPrivilegeLevel,
    uint8_t DescriptorType
) {
    

    interruptDescriptorTable[interruptNumber].handlerAddressLowBits = ((uint32_t)handler) & 0xFFFF;
    interruptDescriptorTable[interruptNumber].handlerAddressHighBits = (((uint32_t)handler) >> 16) & 0xFFFF;
    interruptDescriptorTable[interruptNumber].gdt_codeSegmentSelector = codeSegmentSelectorOffset;
    
    const uint8_t IDT_DESC_PRESENT = 0x80;
    interruptDescriptorTable[interruptNumber].access = IDT_DESC_PRESENT | DescriptorType | ((DescriptorPrivilegeLevel&3) << 5);
    interruptDescriptorTable[interruptNumber].reserved = 0;
}

//初始化中断表，初始化中断入口函数
//初始化pic可编程中断硬件
//向cpu挂载idt表
InterruptsManager::InterruptsManager(uint16_t hardwareInterruptOffset, GlobalDescriptorTable* gdt, TaskManager* taskmanager) 
    :picMasterCommand(0x20),
    picMasterData(0x21),
    picSlaveCommand(0xA0),
    picSlaveData(0xA1)
{
    //挂载在中断管理上时，中断管理的构造函数赋不了值
    this->taskManager = taskmanager;

    
    this->hardwareInterruptOffset = hardwareInterruptOffset;
    uint32_t CodeSegment = gdt->CodeSegmentSelector();

    const uint8_t IDT_INTERRUPT_GATE = 0xE;
    for(uint8_t i = 255; i > 0; --i)
    {
        SetInterruptDescriptorTableEntry(i, CodeSegment, &InterruptIgnore, 0, IDT_INTERRUPT_GATE);
        handlers[i] = 0;
    }
    SetInterruptDescriptorTableEntry(0, CodeSegment, &InterruptIgnore, 0, IDT_INTERRUPT_GATE);
    handlers[0] = 0;

    SetInterruptDescriptorTableEntry(0x00, CodeSegment, &HandleException0x00, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x01, CodeSegment, &HandleException0x01, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x02, CodeSegment, &HandleException0x02, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x03, CodeSegment, &HandleException0x03, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x04, CodeSegment, &HandleException0x04, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x05, CodeSegment, &HandleException0x05, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x06, CodeSegment, &HandleException0x06, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x07, CodeSegment, &HandleException0x07, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x08, CodeSegment, &HandleException0x08, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x09, CodeSegment, &HandleException0x09, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x0A, CodeSegment, &HandleException0x0A, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x0B, CodeSegment, &HandleException0x0B, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x0C, CodeSegment, &HandleException0x0C, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x0D, CodeSegment, &HandleException0x0D, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x0E, CodeSegment, &HandleException0x0E, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x0F, CodeSegment, &HandleException0x0F, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x10, CodeSegment, &HandleException0x10, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x11, CodeSegment, &HandleException0x11, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x12, CodeSegment, &HandleException0x12, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x13, CodeSegment, &HandleException0x13, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x14, CodeSegment, &HandleException0x14, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x15, CodeSegment, &HandleException0x15, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x16, CodeSegment, &HandleException0x16, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x17, CodeSegment, &HandleException0x17, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x18, CodeSegment, &HandleException0x18, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x19, CodeSegment, &HandleException0x19, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x1A, CodeSegment, &HandleException0x1A, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x1B, CodeSegment, &HandleException0x1B, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x1C, CodeSegment, &HandleException0x1C, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x1D, CodeSegment, &HandleException0x1D, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x1E, CodeSegment, &HandleException0x1E, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(0x1F, CodeSegment, &HandleException0x1F, 0, IDT_INTERRUPT_GATE);

    
    SetInterruptDescriptorTableEntry(hardwareInterruptOffset + 0x00, CodeSegment, &HandleInterruptRequest0x00, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(hardwareInterruptOffset + 0x01, CodeSegment, &HandleInterruptRequest0x01, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(hardwareInterruptOffset + 0x02, CodeSegment, &HandleInterruptRequest0x02, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(hardwareInterruptOffset + 0x03, CodeSegment, &HandleInterruptRequest0x03, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(hardwareInterruptOffset + 0x04, CodeSegment, &HandleInterruptRequest0x04, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(hardwareInterruptOffset + 0x05, CodeSegment, &HandleInterruptRequest0x05, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(hardwareInterruptOffset + 0x06, CodeSegment, &HandleInterruptRequest0x06, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(hardwareInterruptOffset + 0x07, CodeSegment, &HandleInterruptRequest0x07, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(hardwareInterruptOffset + 0x08, CodeSegment, &HandleInterruptRequest0x08, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(hardwareInterruptOffset + 0x09, CodeSegment, &HandleInterruptRequest0x09, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(hardwareInterruptOffset + 0x0A, CodeSegment, &HandleInterruptRequest0x0A, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(hardwareInterruptOffset + 0x0B, CodeSegment, &HandleInterruptRequest0x0B, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(hardwareInterruptOffset + 0x0C, CodeSegment, &HandleInterruptRequest0x0C, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(hardwareInterruptOffset + 0x0D, CodeSegment, &HandleInterruptRequest0x0D, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(hardwareInterruptOffset + 0x0E, CodeSegment, &HandleInterruptRequest0x0E, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(hardwareInterruptOffset + 0x0F, CodeSegment, &HandleInterruptRequest0x0F, 0, IDT_INTERRUPT_GATE);
    SetInterruptDescriptorTableEntry(hardwareInterruptOffset + 0x31, CodeSegment, &HandleInterruptRequest0x31, 0, IDT_INTERRUPT_GATE);

    SetInterruptDescriptorTableEntry(                          0x80, CodeSegment, &HandleInterruptRequest0x80, 3, IDT_INTERRUPT_GATE);

    picMasterCommand.Write(0x11);
    picSlaveCommand.Write(0x11);
    
    picMasterData.Write(hardwareInterruptOffset);
    picSlaveData.Write(hardwareInterruptOffset + 8);

    picMasterData.Write(0x04);
    picSlaveData.Write(0x02);

    picMasterData.Write(0x01);
    picSlaveData.Write(0x01);

    // Timer, keyboard, cascade and mouse only; ATA uses polling with nIEN.
    picMasterData.Write(0xF8);
    picSlaveData.Write(0xEF);

    InterruptDescriptorTablePointer idt_pointer;
    idt_pointer.size  = 256*sizeof(GateDescriptor) - 1;
    idt_pointer.base  = (uint32_t)interruptDescriptorTable;
    asm volatile("lidt %0" : : "m" (idt_pointer) : "memory");
}

InterruptsManager::~InterruptsManager() {
    Deactivate();
}

uint16_t InterruptsManager::HardwareInterruptOffset()
{
    return hardwareInterruptOffset;
}

//激活idt
void InterruptsManager::Activate() {
    if (ActivateInterruptsManager != 0)
        ActivateInterruptsManager->Deactivate();
    ActivateInterruptsManager = this;
    asm volatile("sti" ::: "memory");
}

void InterruptsManager::Deactivate() {
    if (ActivateInterruptsManager == this) {
        asm volatile("cli" ::: "memory");
        ActivateInterruptsManager = 0;
    }
}

//汇编调用的入口函数
uint32_t InterruptsManager::handleInterrupt(uint8_t interruptNumber, uint32_t esp) {
    if (ActivateInterruptsManager != 0) {
        return ActivateInterruptsManager->DoHandleInterrupt(interruptNumber, esp);
    }

    return esp;
}

uint32_t InterruptsManager::DoHandleInterrupt(uint8_t interruptNumber, uint32_t esp) {
    if (interruptNumber < 0x20) {
        CPUState* state = (CPUState*)esp;
        uint32_t cr2; asm volatile("mov %%cr2,%0" : "=r"(cr2));
        process::NativeRuntime* runtime = process::NativeRuntime::Active();
        CPUState* next = runtime ? runtime->HandleFault(state, interruptNumber == 14 ? cr2 : 0) : 0;
        if (next) return (uint32_t)next;
        printf("\nPANIC EXCEPTION vector="); printfHex(interruptNumber);
        printf(" error="); printfHex32(state->error);
        printf(" eip="); printfHex32(state->eip);
        printf(" cr2="); printfHex32(cr2); printf("\n");
        for (;;) asm volatile("cli; hlt");
    }
    if (handlers[interruptNumber] != 0) {
        esp = handlers[interruptNumber]->HandlerInterrupt(esp);
        
    }
    else if (interruptNumber != hardwareInterruptOffset) {
        printf("UNHANDLED INTERRUPT 0x");
        printfHex(interruptNumber);
    }
    
    if (interruptNumber == hardwareInterruptOffset && taskManager != 0) {
        esp = (uint32_t)taskManager->Schedule((CPUState*)esp);
        
    }

    if (hardwareInterruptOffset <= interruptNumber && interruptNumber < hardwareInterruptOffset + 16) {
        if (hardwareInterruptOffset + 8 <= interruptNumber) picSlaveCommand.Write(0x20);
        picMasterCommand.Write(0x20);
    }
    CPUState* returning = (CPUState*)esp;
    if ((returning->cs & 3) == 3)
        returning->eflags = (returning->eflags & 0xCD5U) | 0x202U;
    return esp;
}

void InterruptsManager::Load(InterruptHandler* handler,uint8_t interruptNumber) {
    this->handlers[interruptNumber] = handler;
}

void InterruptsManager::Load(TaskManager* taskManager) {
    this->taskManager = taskManager;
}

// void InterruptsManager::Load(uint8_t InterruptNumber, InterruptsManager* InterruptManager) {
//     InterruptManager->handlers[InterruptNumber] = this;
// }