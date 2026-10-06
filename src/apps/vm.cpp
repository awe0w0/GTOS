#include <apps/vm.h>
using namespace gtos::apps;
VirtualMachine::VirtualMachine(Host* h):host(h),pc(0),random(0xA75391B5U),loaded(false),running(false),fault(0) {
    info.id[0]=info.title[0]=info.summary[0]=0;
    info.size=info.instructionCount=info.entry=0;
    for (uint32_t i=0;i<RegisterCount;i++) regs[i]=0;
}
void VirtualMachine::Fail(const char* reason) { fault=reason; running=false; }
bool VirtualMachine::Load(const uint8_t* package,uint32_t length) {
    loaded=false; running=false; fault=0;
    PackageError e=ValidatePackage(package,length,&info);
    if (e!=PackageOK) { Fail(PackageErrorText(e)); return false; }
    if (!host) { Fail("No app display"); return false; }
    for (uint32_t i=0;i<length;i++) program[i]=package[i];
    loaded=true; Reset(); return true;
}
void VirtualMachine::Reset() {
    if (!loaded) return;
    for (uint32_t i=0;i<RegisterCount;i++) regs[i]=0;
    pc=info.entry; random=0xA75391B5U; running=true; fault=0;
}
void VirtualMachine::Stop() { running=false; }
bool VirtualMachine::Step(uint32_t keys,uint32_t ticks) {
    if (!running) return false;
    for (uint32_t steps=0;steps<InstructionBudget;steps++) {
        if (pc>=info.instructionCount) { Fail("App ran past code"); return false; }
        const uint8_t* p=program+HeaderSize+8*pc++;
        uint8_t op=p[0],a=p[1],b=p[2],c=p[3]; uint32_t v=Read32(p+4);
        switch(op) {
            case 0: running=false; return false;
            case 1: regs[a]=(int32_t)v; break;
            case 2: regs[a]=regs[b]; break;
            case 3: regs[a]=(int32_t)((uint32_t)regs[b]+(uint32_t)regs[c]); break;
            case 4: regs[a]=(int32_t)((uint32_t)regs[b]-(uint32_t)regs[c]); break;
            case 5: regs[a]=(int32_t)((uint32_t)regs[b]*(uint32_t)regs[c]); break;
            case 6:
                if (!regs[c]) { Fail("App division by zero"); return false; }
                regs[a]=(regs[b]==(int32_t)0x80000000U && regs[c]==-1)?0:regs[b]%regs[c]; break;
            case 7: regs[a]=regs[b]&regs[c]; break;
            case 8: regs[a]=regs[b]<regs[c]?1:0; break;
            case 9: regs[a]=regs[b]==regs[c]?1:0; break;
            case 10: pc=v; break;
            case 11: if (regs[a]) pc=v; break;
            case 12: regs[a]=(int32_t)(v==0?keys:(v==1?ticks:(uint32_t)(v==2?Width:Height))); break;
            case 13:
                random^=random<<13; random^=random>>17; random^=random<<5;
                regs[a]=random%v; break;
            case 14: host->Clear(v); break;
            case 15: {
                // Clip before handing coordinates to host; no signed addition overflow.
                int32_t x=regs[a],y=regs[b],w=regs[c],h=regs[v&255];
                if (w<=0||h<=0||x>=Width||y>=Height) break;
                if (x<0) { if (x<=-w) break; w+=x; x=0; }
                if (y<0) { if (y<=-h) break; h+=y; y=0; }
                if (w>Width-x) w=Width-x;
                if (h>Height-y) h=Height-y;
                host->Rect(x,y,w,h,(v>>8)&255); break;
            }
            case 16: {
                int32_t x=regs[a],y=regs[b];
                if (x<0||y<0||x>=Width||y>Height-8) break;
                const char* src=v?info.summary:info.title;
                char buf[41]; uint32_t n=0,limit=(Width-x)/6;
                while (src[n]&&n<40&&n<limit) { buf[n]=src[n]; n++; }
                buf[n]=0; host->Text(x,y,buf,15); break;
            }
            case 17: {
                uint32_t magnitude=regs[c]<0?0U-(uint32_t)regs[c]:(uint32_t)regs[c];
                uint32_t chars=regs[c]<0?2:1;
                while (magnitude>=10) { magnitude/=10; chars++; }
                if (regs[a]>=0&&regs[a]<=Width-(int32_t)(chars*6)&&regs[b]>=0&&regs[b]<=Height-8)
                    host->Number(regs[a],regs[b],regs[c],v);
                break;
            }
            case 18: return true;
            default: Fail("App invalid opcode"); return false;
        }
    }
    Fail("App instruction budget exceeded"); return false;
}
