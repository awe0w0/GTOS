#include <common/types.h>
#include <gdt.h>
#include <memorymanagement.h>
#include <hardwarecommunication/interrupts.h>
#include <hardwarecommunication/pci.h>
#include <syscalls.h>
#include <drivers/keyboard.h>
#include <drivers/mouse.h>
#include <drivers/driver.h>
#include <drivers/vga.h>
#include <gui/desktop.h>
#include <gui/window.h>
#include <multitasking.h>
#include <drivers/amd_am79c973.h>
#include <drivers/ata.h>

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
			for(y = 0; y < 24; y++)
				for(x = 0; x < 80; x++)
					VideoMemory[80*y+x] = VideoMemory[80*(y+1)+x];
			for(x = 0; x < 80; x++)
				VideoMemory[80*24+x] = (VideoMemory[80*24+x] & 0xFF00) | ' ';
			y = 24;
			x = 0;
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

void printfHex16(uint16_t key)
{
    printfHex((key >> 8) & 0xFF);
    printfHex( key & 0xFF);
}
void printfHex32(uint32_t key)
{
    printfHex((key >> 24) & 0xFF);
    printfHex((key >> 16) & 0xFF);
    printfHex((key >> 8) & 0xFF);
    printfHex( key & 0xFF);
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

void sysprintf(char* str) {
    //0x80号中断，ax为4 调用sys_write bx为str
    asm("int $0x80" : : "a" (4), "b" (str));
}

void taskA() {
    while (true) sysprintf("A");
}

void taskB() {
    while (true) sysprintf("B");
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
    //内存动态分配
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

    //初始化多线程
    TaskManager taskManager;
    Task task1(&gdt, taskA);
    Task task2(&gdt, taskB);
    taskManager.AddTask(&task1);
    taskManager.AddTask(&task2);
    InterruptsManager interrupts(0x20, &gdt, &taskManager);
    //中断管理类构造函数赋不上值，另写个Load直接赋值
    interrupts.Load(&taskManager);
    SyscallHandler syscalls(&interrupts, 0x80);
    
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

        // pci设备初始化
        PeripheralComponentInterconnectController PCIController;
        //从pci端口读取数据
        PCIController.SelectDrivers(&drvManager, &interrupts);

        VideoGraphicsArray vga;

    printf("Initializing Hardware, Stage 2\n");
        drvManager.ActivateAll();

    printf("Initializing Hardware, Stage 3\n");

#ifdef GRAPHICSMODE
    //开启vga模式
    vga.SetMode(320, 200, 8);

    Window win1(&desktop, 10, 10, 20, 20, 0xA8, 0x00, 0x00);
    desktop.AddChild(&win1);

    Window win2(&desktop, 40, 15, 30, 30, 0x00, 0xA8, 0x00);
    desktop.AddChild(&win2);
#endif

// 14号中断
AdvancedTechnologyAttachment ata0m(0x1F0, true);
printf("ATA Primary Master:");
ata0m.Identify();

AdvancedTechnologyAttachment ata0s(0x1F0, false);
printf("ATA Primary Slave:");
ata0s.Identify();

char* atabuffer = "https://awe0w0.top";
ata0s.Write28(0,(uint8_t*)atabuffer,18);
ata0s.Flush();

ata0s.Read28(0, (uint8_t*)atabuffer, 18);

//15号中断
AdvancedTechnologyAttachment ata1m(0x170, true);
AdvancedTechnologyAttachment ata1s(0x170, false);

//third: 0x1E8
//fourth: 0x168

// amd_am79c973* eth0 = (amd_am79c973*)(drvManager.drivers[2]);
// eth0->Send((uint8_t*)"Hello Network", 13);

    //激活handlers数组中的中断
    interrupts.Activate();


    while (true) {
        #ifdef GRAPHICSMODE
            desktop.Draw(&vga);
        #endif
    }    
}
