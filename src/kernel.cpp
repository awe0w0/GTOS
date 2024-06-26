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
#include <net/etherframe.h>
#include <net/arp.h>
#include <net/ipv4.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/tcp.h>

// #define GRAPHICSMODE

using namespace gtos;
using namespace gtos::hardwarecommunication;
using namespace gtos::drivers;
using namespace gtos::gui;
using namespace gtos::net;

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

//键盘驱动入口类
class PrintfKeyboardEventHandler : public KeyboardEventHandler {
    public:
    void OnKeyDown(char c) {
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

class PrintUDPHandler : public UserDatagramProtocolHandler {
    public:
    void HandleUserDatagramProtocolMessage(UserDatagramProtocolSocket* socket, uint8_t* data, uint16_t size) {
        char* foo = " ";
        for (int i = 0;i < size;i++) {
            foo[0] = data[i];
            printf(foo);
        }
    }
};

void sysprintf(char* str) {
    //0x80号中断，ax为4时调用sys_write bx为str
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
    // Task task1(&gdt, taskA);
    // Task task2(&gdt, taskB);
    // taskManager.AddTask(&task1);
    // taskManager.AddTask(&task2);
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
    uint8_t ip1 = 10, ip2 = 0, ip3 = 2, ip4 =15;
    uint32_t ip_be = ((uint32_t)ip4 << 24)
                    | ((uint32_t)ip3 << 16)
                    | ((uint32_t)ip2 << 8)
                    | ((uint32_t)ip1);

    amd_am79c973* eth0 = (amd_am79c973*)(drvManager.drivers[2]);

    eth0->SetIPAddress(ip_be);

    EtherFrameProvider etherframe(eth0);
    //eth0->SetHandler(&etherframe);
    AddressResolutionProtocol arp(&etherframe);
    
    uint8_t gip1 = 10, gip2 = 0, gip3 = 2, gip4 = 2;
    uint32_t gip_be = ((uint32_t)gip4 << 24)
                    | ((uint32_t)gip3 << 16)
                    | ((uint32_t)gip2 << 8)
                    | ((uint32_t)gip1);

    uint8_t subnet1 = 255, subnet2 = 255, subnet3 = 255, subnet4 = 0;
    uint32_t subnet_be = ((uint32_t)subnet4 << 24)
                    | ((uint32_t)subnet3 << 16)
                    | ((uint32_t)subnet2 << 8)
                    | ((uint32_t)subnet1);    


    InternetProtocolProvider ipv4(&etherframe, &arp, gip_be, subnet_be);
    // etherframe.Send(0xFFFFFFFFFFFF, 0x0608, (uint8_t*)"F00", 3);
    //eth0->Send((uint8_t*)"Hello Network", 13);
    InternetControlMessageProtocol icmp(&ipv4);
    UserDatagramProtocolProvider udp(&ipv4);
    TransmissionControlProtocolProvider tcp(&ipv4);

    // arp.Resolve(gip_be);
    //发送前会进行arp广播
    // ipv4.Send(gip_be, 0x0008, (uint8_t*)"foobar", 6);

    //激活handlers数组中的中断
    interrupts.Activate();
    printf("\n\n\n\n");

    //arp.BroadcastMACAddress(gip_be);
    tcp.Connect(gip_be, 1234);

    //icmp.RequestEchoReply(gip_be);

    //PrintUDPHandler udphandler;
    // UserDatagramProtocolSocket* udpsocket = udp.Connect(gip_be, 1234);
    // udp.Bind(udpsocket, &udphandler);
    // udpsocket->Send((uint8_t*)"Hello UDP!", 10);

    //UserDatagramProtocolSocket* udpsocket = udp.Listen(1234);
    //udp.Bind(udpsocket, &udphandler);

    while (true) {
        #ifdef GRAPHICSMODE
            desktop.Draw(&vga);
        #endif
    }    
}
