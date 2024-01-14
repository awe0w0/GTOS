#include <hardwarecommunication/interrupts.h>

using namespace gtos;
using namespace gtos::hardwarecommunication;

void printf(char* str);
void printfHex(uint8_t);

InterruptHandler::InterruptHandler(uint8_t InterruptNumber, InterruptsManager* interruptsManager) {
    this->interruptNumber = interruptNumber;
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

        InterruptsManager::InterruptsManager(uint16_t hardwareInterruptOffset, GlobalDescriptorTable* gdt, TaskManager* taskmanager) 
            :picMasterCommand(0x20),
            picMasterData(0x21),
            picSlaveCommand(0xA0),
            picSlaveData(0xA1)
        {
            //挂载在中断管理上时，中断管理的默认函数赋不了值
            this->taskManager = taskManager;
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

            picMasterCommand.Write(0x11);
            picSlaveCommand.Write(0x11);
            
            picMasterData.Write(hardwareInterruptOffset);
            picSlaveData.Write(hardwareInterruptOffset + 8);

            picMasterData.Write(0x04);
            picSlaveData.Write(0x02);

            picMasterData.Write(0x01);
            picSlaveData.Write(0x01);

            picMasterData.Write(0x00);
            picSlaveData.Write(0x00);

            InterruptDescriptorTablePointer idt_pointer;
            idt_pointer.size  = 256*sizeof(GateDescriptor) - 1;
            idt_pointer.base  = (uint32_t)interruptDescriptorTable;
            asm volatile("lidt %0" : : "m" (idt_pointer));
        }

InterruptsManager::~InterruptsManager() {

}

//激活idt
void InterruptsManager::Activate() {
    if (ActivateInterruptsManager != 0)
        ActivateInterruptsManager->Deactivate();
    ActivateInterruptsManager = this;
    asm("sti");
}

void InterruptsManager::Deactivate() {
    if (ActivateInterruptsManager == this) {
        ActivateInterruptsManager = 0;
        asm("cli");
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
    if (handlers[interruptNumber] != 0) {
        esp = handlers[interruptNumber]->HandlerInterrupt(esp);
        
    }
    else if (interruptNumber != hardwareInterruptOffset) {
        printf("UNHANDLED INTERRUPT 0x");
        printfHex(interruptNumber);
    }
    
    if (interruptNumber == hardwareInterruptOffset) {
        esp = (uint32_t)taskManager->Schedule((CPUState*)esp);
        
    }

    if (hardwareInterruptOffset <= interruptNumber && interruptNumber < hardwareInterruptOffset + 16) {
        picMasterCommand.Write(0x20);
        if (hardwareInterruptOffset + 8 <= interruptNumber) {
            picSlaveCommand.Write(0x20);
        }
    }
    return esp;
}

void InterruptsManager::Load(InterruptHandler* handler,uint8_t interruptNumber) {
    this->handlers[interruptNumber] = handler;
}

void InterruptsManager::Load(TaskManager* taskManager) {
    this->taskManager = taskManager;
}