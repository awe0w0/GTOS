#ifndef GTOS_APPS_VM_H
#define GTOS_APPS_VM_H
#include <apps/package.h>
namespace gtos { namespace apps {
class Host {
public:
    virtual void Clear(uint8_t color)=0;
    virtual void Rect(int32_t x,int32_t y,int32_t w,int32_t h,uint8_t color)=0;
    virtual void Text(int32_t x,int32_t y,const char* text,uint8_t color)=0;
    virtual void Number(int32_t x,int32_t y,int32_t value,uint8_t color)=0;
};
class VirtualMachine {
    Host* host;
    uint8_t program[PackageLimit];
    PackageInfo info;
    int32_t regs[RegisterCount];
    uint32_t pc, random;
    bool loaded, running;
    const char* fault;
    void Fail(const char* reason);
public:
    enum Keys { Left=1, Right=2, Up=4, Down=8, Action=16 };
    enum { Width=272, Height=128, InstructionBudget=2048 };
    explicit VirtualMachine(Host* host);
    bool Load(const uint8_t* package,uint32_t length);
    void Reset();
    void Stop();
    bool Step(uint32_t keys,uint32_t ticks);
    bool Running() const { return running; }
    const char* Fault() const { return fault; }
    const PackageInfo& Info() const { return info; }
};
} }
#endif
