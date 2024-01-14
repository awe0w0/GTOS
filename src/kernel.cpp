#include <common/types.h>
#include <gdt.h>
#include <memorymanagement.h>
#include <hardwarecommunication/interrupts.h>
#include <hardwarecommunication/pci.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <drivers/driver.h>
#include <drivers/vga.h>
#include <gui/desktop.h>
#include <gui/window.h>
#include <multitasking.h>

// #define GRAPHICSMODE

using namespace gtos;
using namespace gtos::hardwarecommunication;
using namespace gtos::drivers;
using namespace gtos::gui;

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
            y = 0;
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

//b8000模式下的鼠标控制类
class MouseToConsole : public MouseEventHandler {
    int8_t x = 40, y = 12;
    //pre用来获取鼠标经过的上个地址的数据
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

void taskA() {
    while (true) printf("A");
}

void taskB() {
    while (true) printf("B");
}

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

    //grub的multiboot
    uint32_t* memupper = (uint32_t*)(((size_t)multiboot_structure) + 8);
    size_t heap = 10 * 1024 * 1024;
    MemoryManager memoryManager(heap, (*memupper) * 1024 - heap - 10 * 1024);

    printf("heap: 0x");
    printfHex((heap >> 24) & 0xFF);
    printfHex((heap >> 16) & 0xFF);
    printfHex((heap >>  8) & 0xFF);
    printfHex((heap >>  0) & 0xFF);

    void* allocated = memoryManager.malloc(1024);

    printf("\nallocated: 0x");
    printfHex(((size_t)allocated >> 24) & 0xFF);
    printfHex(((size_t)allocated >> 16) & 0xFF);
    printfHex(((size_t)allocated >>  8) & 0xFF);
    printfHex(((size_t)allocated >>  0) & 0xFF);
    printf("\n");

    TaskManager taskManager;
    // Task task1(&gdt, taskA);
    // Task task2(&gdt, taskB);
    // taskManager.AddTask(&task1);
    // taskManager.AddTask(&task2);
    InterruptsManager interrupts(0x20, &gdt, &taskManager);
    interrupts.Load(&taskManager);
    
#ifdef GRAPHICSMODE
    Desktop desktop(320, 200, 0x00, 0x00, 0xA8);
#endif
    printf("Initializing Hardware, Stage 1\n");

    DriverManager drvManager;

    #ifdef GRAPHICSMODE
        drivers::KeyboardDriver keyboard(&interrupts, &desktop);
    #else
        PrintfKeyboardEventHandler kbhandler;
        drivers::KeyboardDriver keyboard(&interrupts, &kbhandler);
    #endif
        drvManager.AddDriver(&keyboard);
        interrupts.Load(&keyboard, 0x21);


    #ifdef GRAPHICSMODE
        drivers::MouseDriver mouse(&interrupts, &desktop);
    #else
        MouseToConsole mousehandler;
        drivers::MouseDriver mouse(&interrupts, &mousehandler);
    #endif
        drvManager.AddDriver(&mouse);
        interrupts.Load(&mouse, 0x2C);

        PeripheralComponentInterconnectController PCIController;
        PCIController.SelectDrivers(&drvManager, &interrupts);

        VideoGraphicsArray vga;

    printf("Initializing Hardware, Stage 2\n");
        drvManager.ActivateAll();

    printf("Initializing Hardware, Stage 3\n");

#ifdef GRAPHICSMODE
    vga.SetMode(320, 200, 8);

    Window win1(&desktop, 10, 10, 20, 20, 0xA8, 0x00, 0x00);
    desktop.AddChild(&win1);

    Window win2(&desktop, 40, 15, 30, 30, 0x00, 0xA8, 0x00);
    desktop.AddChild(&win2);
#endif
    interrupts.Activate();


    while (true) {
        #ifdef GRAPHICSMODE
            desktop.Draw(&vga);
        #endif
    }    
}
