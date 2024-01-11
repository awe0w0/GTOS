#include <common/types.h>
#include <gdt.h>
#include <hardwarecommunication/interrupts.h>
#include <hardwarecommunication/pci.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <drivers/driver.h>
#include <drivers/vga.h>

using namespace gtos;
using namespace gtos::hardwarecommunication;
using namespace gtos::drivers;

void printf(char* str) {
    static uint16_t* VideoMemory = (uint16_t*) 0xb8000;

    static uint8_t x = 0, y = 0;

    for (int i = 0;str[i] != '\0';i++) {
        switch(str[i]) {
            case '\n':
                y++;
                x = 0;
                break;
            default:
                VideoMemory[80 * y + x] = (VideoMemory[80 * y + x] & 0xFF00) | str[i];
                x++;
                break;
        }
         if (x >= 80) {
            y++;
            x = 0;
         }
         if (y >= 25) {
            for (y = 0;y < 25;y++)
                for (x = 0;x < 80;x++)
                    VideoMemory[80 * y + x] = (VideoMemory[80 * y + x] & 0xFF00) | ' ';
         }
    }
}

void printfHex(uint8_t key) {
    char* foo = "00";
    char* hex = "0123456789ABCDEF";
    foo[0] = hex[(key >> 4) & 0x0F];
    foo[1] = hex[key & 0x0F];

    printf(foo);
}

class PrintfKeyboardEventHandler : public KeyboardEventHandler {
    public:
    void OnKeuDown(char c) {
        char* foo = " ";
        foo[0] = c;
        printf(foo);
    }
};

class MouseToConsole : public MouseEventHandler {
    int8_t x = 40, y = 12;
    uint16_t pre;
public:
    MouseToConsole() {
        static uint16_t* VideoMemory = (uint16_t*)0xb8000;

        VideoMemory[80 * 12 + 40] = (VideoMemory[80 * 12 + 40] & 0xF000 >> 4)
                                | ((VideoMemory[80 * 12 + 40] & 0x0F00) << 4)
                                | (VideoMemory[80 * 12 + 40] & 0x00FF);
    }

    void OnMouseMove(int8_t xoffset,int8_t yoffset) {
            static uint16_t* VideoMemory = (uint16_t*)0xb8000;

            VideoMemory[80 * y + x] = pre;

            x += xoffset;
            if (x < 0) x = 0;
            if (x >= 80) x = 79;
            
            y += yoffset;
            if (y < 0) y = 0;
            if (y >= 25) y = 24;
            
            pre = VideoMemory[80 * y + x]; 
            VideoMemory[80 * y + x] = (VideoMemory[80 * y + x] & 0xF000 >> 4)
                                    | ((VideoMemory[80 * y + x] & 0x0F00) << 4)
                                    | (VideoMemory[80 * y + x] & 0x00FF);
    }
};

typedef void (*constructor)();
extern "C" constructor start_ctors;
extern "C" constructor end_ctors;
extern "C" void callConstructors() {
    for (constructor* i = &start_ctors;i != &end_ctors;i++)
        (*i)();
}

extern "C" void kernelMain (void* multiboot_structure, uint32_t magicnumber) {
    printf("NOW_LODING...\n");
    GlobalDescriptorTable gdt;
    InterruptsManager interrupts(0x20, &gdt);

    printf("Initializing Hardware, Stage 1\n");

    DriverManager drvManager;
        PrintfKeyboardEventHandler kbhandler;
        drivers::KeyboardDriver keyboard(&interrupts, &kbhandler);
        drvManager.AddDriver(&keyboard);
        interrupts.load(&keyboard, 0x21);

        MouseToConsole mousehandler;
        drivers::MouseDriver mouse(&interrupts, &mousehandler);
        drvManager.AddDriver(&mouse);
        interrupts.load(&mouse, 0x2C);

        PeripheralComponentInterconnectController PCIController;
        PCIController.SelectDrivers(&drvManager, &interrupts);

        VideoGraphicsArray vga;

    printf("Initializing Hardware, Stage 2\n");
        drvManager.ActivateAll();

    printf("Initializing Hardware, Stage 3\n");
    interrupts.Activate();

    vga.SetMode(320, 200, 8);

    for (uint8_t y = 0;y < 200;y++) {
        for (uint8_t x = 0;x < 320;x++) 
            vga.PutPixel(x, y,0x00, 0x00, 0xA8);
    }

    while (true);
}