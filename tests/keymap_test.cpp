#include <drivers/keymap.h>
using namespace gtos::drivers;
static int failures;
static void Check(bool ok) { if (!ok) ++failures; }
static void Event(Set1Keymap& map,uint8_t scan,uint8_t expected,bool expectedDown) {
    uint8_t key; bool down;
    Check(map.Feed(scan,key,down)&&key==expected&&down==expectedDown);
}
static void Ignore(Set1Keymap& map,uint8_t scan) { uint8_t key;bool down;Check(!map.Feed(scan,key,down)); }
extern "C" int Main() {
    Set1Keymap m;
    Event(m,0x05,'4',true);Ignore(m,0x2A);Event(m,0x05,'4',true);
    Event(m,0x85,'4',false);Ignore(m,0xAA);Event(m,0x05,'4',true);Event(m,0x85,'4',false);
    Ignore(m,0x2A);Event(m,0x05,'$',true);Ignore(m,0xAA);Event(m,0x85,'$',false);
    Event(m,0x1A,'[',true);Ignore(m,0x36);Event(m,0x9A,'[',false);Ignore(m,0xB6);
    Ignore(m,0x3A);Ignore(m,0x3A);Ignore(m,0xBA);Event(m,0x1E,'A',true);Event(m,0x9E,'A',false);
    Ignore(m,0x2A);Event(m,0x1E,'a',true);Ignore(m,0xAA);Event(m,0x9E,'a',false);
    Ignore(m,0xE0);Event(m,0x4B,0x81,true);Event(m,0x4B,0x81,true);
    Ignore(m,0xE0);Event(m,0xCB,0x81,false);Event(m,0xCB,0x81,false);
    Ignore(m,0xE0);Event(m,0x1C,'\n',true);Ignore(m,0xE0);Event(m,0x9C,'\n',false);
    const uint8_t pause[]={0xE1,0x1D,0x45,0xE1,0x9D,0xC5};
    for(uint32_t i=0;i<6;++i)Ignore(m,pause[i]);
    Ignore(m,0xFA);Ignore(m,0xFE);Ignore(m,0x85);
    const char* text=failures?"FAIL keyboard identity tests\n":"PASS stable keyboard make/break identity and modifiers\n";
    uint32_t n=0;while(text[n])++n;
    { uint32_t written; asm volatile("int $0x80" : "=a"(written) : "0"(4),"b"(1),"c"(text),"d"(n) : "memory", "cc"); }
    return failures?1:0;
}
asm(".global _start\n_start:\n andl $-16,%esp\n call Main\n movl %eax,%ebx\n movl $1,%eax\n int $0x80\n");
