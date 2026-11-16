#include <hardwarecommunication/pci.h>
#include <drivers/amd_am79c973.h>

using namespace gtos::hardwarecommunication;
using namespace gtos::drivers;

PeripheralComponentInterconnectDeviceDescriptor::PeripheralComponentInterconnectDeviceDescriptor() {

}

PeripheralComponentInterconnectDeviceDescriptor::~PeripheralComponentInterconnectDeviceDescriptor() {

}

PeripheralComponentInterconnectController::PeripheralComponentInterconnectController()
: dataPort(0xCFC), 
commandPort(0xCF8) {

}

PeripheralComponentInterconnectController::~PeripheralComponentInterconnectController() {
    
}

uint32_t PeripheralComponentInterconnectController::Read(uint16_t bus, uint16_t device, uint16_t function, uint32_t registeroffset) {
    uint32_t id = 
        1u << 31
        | ((bus & 0xFF) << 16)
        | ((device & 0x1F) << 11)
        | ((function & 0x07) << 8)
        | (registeroffset & 0xFC);
    commandPort.Write(id);
    uint32_t result = dataPort.Read();

    return result >> (8 * (registeroffset % 4));
}

void PeripheralComponentInterconnectController::Write(uint16_t bus, uint16_t device, uint16_t function, uint32_t registeroffset, uint32_t value) {
    uint32_t id = 
        1u << 31
        | ((bus & 0xFF) << 16)
        | ((device & 0x1F) << 11)
        | ((function & 0x07) << 8)
        | (registeroffset & 0xFC);
    commandPort.Write(id);
    dataPort.Write(value);
}

bool PeripheralComponentInterconnectController::DeviceHasFunctions(uint16_t bus, uint16_t device) {
    return Read(bus, device, 0, 0x0E) & (1 << 7);
}

bool PeripheralComponentInterconnectController::ProbeMemoryBar32(
    const PeripheralComponentInterconnectDeviceDescriptor& d, uint8_t bar,
    uint32_t& address, uint32_t& bytes) {
    address = bytes = 0;
    if (bar >= 6 || (Read(d.bus, d.device, d.function, 0x0E) & 0x7F) != 0)
        return false;

    const uint32_t offset = 0x10 + 4 * bar;
    const uint32_t low = Read(d.bus, d.device, d.function, offset);
    const uint32_t barType = low & 7;
    const bool wide = barType == 4;
    if ((barType != 0 && !wide) || (wide && bar == 5) || !(low & ~0xFu))
        return false;
    const uint32_t high = wide ? Read(d.bus, d.device, d.function, offset + 4) : 0;
    if (high)
        return false;

    const uint16_t command = Read(d.bus, d.device, d.function, 4);
    // Write zero to the adjacent write-one-to-clear Status bits.
    Write(d.bus, d.device, d.function, 4, command & ~7u);
    if (Read(d.bus, d.device, d.function, 4) & 7) {
        Write(d.bus, d.device, d.function, 4, command);
        return false;
    }
    Write(d.bus, d.device, d.function, offset, 0xFFFFFFFFu);
    if (wide)
        Write(d.bus, d.device, d.function, offset + 4, 0xFFFFFFFFu);
    const uint32_t lowMask = Read(d.bus, d.device, d.function, offset) & ~0xFu;
    const uint32_t highMask = wide ? Read(d.bus, d.device, d.function, offset + 4) : 0xFFFFFFFFu;
    Write(d.bus, d.device, d.function, offset, low);
    if (wide)
        Write(d.bus, d.device, d.function, offset + 4, high);
    Write(d.bus, d.device, d.function, 4, command);

    const uint64_t mask = ((uint64_t)highMask << 32) | lowMask;
    const uint64_t length = ~mask + 1;
    const uint32_t base = low & ~0xFu;
    if (!length || length > 0xFFFFFFFFu || (length & (length - 1)) ||
        (base & (length - 1)) || (uint64_t)base + length > 0x100000000ULL)
        return false;
    address = base;
    bytes = (uint32_t)length;
    return true;
}

void printf(char*);
void printfHex(uint8_t);

//遍历每个pci总线里的每个设备的每个函数
void PeripheralComponentInterconnectController::SelectDrivers(DriverManager* driverManager, InterruptsManager* interrupts) {
    for (int bus = 0;bus < 8;bus++) {
        for (int device = 0;device < 32;device++) {
            int numFunctions = DeviceHasFunctions(bus, device) ? 8 : 1;
            for (int function = 0;function < numFunctions;function++) {
                PeripheralComponentInterconnectDeviceDescriptor dev = GetDeviceDescriptor(bus, device, function);

                if (dev.vendor_id == 0x0000 || dev.vendor_id == 0xFFFF) 
                    continue;

                for (int barNum = 0;barNum < 6;barNum++) {
                    BaseAddressRegister bar = GetBaseAddressRegister(bus, device, function, barNum);
                    if (bar.address && (bar.type == InputOutput)) {
                        dev.portBase = (uint32_t)bar.address;
                    }

                }

                Driver* driver = GetDriver(dev, interrupts);
                if (driver != 0) {
                    driverManager->AddDriver(driver);
                }
                printf("PCI BUS ");
                printfHex(bus & 0xFF);
                printf(",DEVICE ");
                printfHex(device & 0xFF);
                printf(",FUNCTION ");
                printfHex(function & 0XFF);
                printf("= VENDOR ");
                printfHex((dev.vendor_id & 0XFF00) >>8);
                printfHex(dev.vendor_id & 0XFF);
                printf(",DEVICE ");
                printfHex((dev.device_id & 0xFF00) >> 8);
                printfHex(dev.device_id & 0xFF);
                printf("\n");
            }
        }
    }
}


PeripheralComponentInterconnectDeviceDescriptor PeripheralComponentInterconnectController::GetDeviceDescriptor(uint16_t bus, uint16_t device, uint16_t function) {
    PeripheralComponentInterconnectDeviceDescriptor result;

    result.bus = bus;
    result.device = device;
    result.function = function;
    result.portBase = 0;

    result.vendor_id = Read(bus, device, function, 0x00);
    result.device_id = Read(bus, device, function, 0x02);

    result.class_id = Read(bus, device, function, 0x0b);
    result.subclass_id = Read(bus, device, function, 0x0a);
    result.interface_id = Read(bus, device, function, 0x09);

    result.revision = Read(bus, device, function, 0x08);
    result.interrupt = Read(bus, device, function, 0x3c);

    return result;
}

BaseAddressRegister PeripheralComponentInterconnectController::GetBaseAddressRegister(uint16_t bus, uint16_t device, uint16_t function, uint16_t bar) {
    BaseAddressRegister result = {};

    uint32_t headertype = Read(bus, device, function, 0x0E) & 0x7F;
    int maxBARS = 6 - (4 * headertype);
    if (bar >= maxBARS) return result;

    uint32_t bar_value = Read(bus, device, function, 0x10 + 4 * bar);
    result.type = (bar_value & 0x1) ? InputOutput : MemoryMapping;

    if (result.type == MemoryMapping) {
        result.prefetchable = ((bar_value >> 3) & 0x1) == 0x1;
        switch ((bar_value >> 1) & 0x3)
        {
        case 0: //32BIT
        case 1: //20BIT
        case 2: //64BIT
            break;
        }

    } else {
        result.address = (uint8_t*)(bar_value & ~0x3);
        result.prefetchable = false;
    }

    return result;
}

Driver* PeripheralComponentInterconnectController::GetDriver(PeripheralComponentInterconnectDeviceDescriptor dev, InterruptsManager* interrupts) {
    Driver* driver = 0;
    switch (dev.vendor_id) {
        case 0x1022 : //amd
            switch (dev.device_id) {
                case 0x2000: { //am79c973
                    dev.interrupt &= 0xffU;
                    if (!dev.portBase || dev.portBase > 0xffe0U
                        || dev.interrupt < 3 || dev.interrupt >= 16
                        || !MemoryManager::activeMemoryManager) return 0;
                    driver = (Driver*)MemoryManager::activeMemoryManager->malloc(sizeof(amd_am79c973));
                    if (!driver) return 0;
                    // Enable I/O decoding and DMA bus mastering for the PCnet adapter.
                    Write(dev.bus, dev.device, dev.function, 0x04,
                          (Read(dev.bus, dev.device, dev.function, 0x04) & 0xFFFF) | 0x05);
                    new (driver)amd_am79c973(&dev, interrupts);
                    printf("AMD am79c973\n");
                    return driver;
                }
            }
        break;
        case 0x8086: //intel
            break;
    }
    

    switch (dev.class_id) {
        case 0x03: //graphics
            switch (dev.subclass_id) {
                case 0x00: //vga
                    printf("vga");
                    break;
            }
            break;
    }

    return driver;
}
