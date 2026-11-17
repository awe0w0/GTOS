#include <memory/multiboot2.h>
using namespace gtos::memory;

namespace {
    uint32_t Read32(const uint8_t* p) {
        return p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
    }
    uint64_t Read64(const uint8_t* p) { return Read32(p) | ((uint64_t)Read32(p+4)<<32); }
    bool StringSize(const uint8_t* text, uint32_t bytes, uint32_t& size) {
        for (size=0; size<bytes; ++size)
            if (!text[size]) { ++size; return true; }
        return false;
    }
}

bool Multiboot2Handoff::read(const void* data, uint32_t available) {
    ready = efi = false;
    info = {};
    rsdpBytes = acpiLimit = 0;
    const uint32_t address=(uint32_t)data;
    if (address<4096 || (address&7) || available<16 || address>0xFFFFFFF0u) return false;
    const uint8_t* p=(const uint8_t*)data;
    const uint32_t total=Read32(p);
    if (Read32(p+4) || total<16 || total>available || total>MaximumBytes || (total&7)
        || (uint64_t)address+total>0x100000000ULL) return false;
    uint32_t seen=0;
    bool end=false;
    for (uint32_t offset=8; offset<total;) {
        const uint8_t* tag=p+offset;
        const uint32_t type=Read32(tag), size=Read32(tag+4);
        if (size<8 || size>total-offset) return false;
        if (type==0) {
            end=size==8 && offset==total-8;
            break;
        }
        if (type<=18 && type!=3 && (seen&(1U<<type))) return false;
        if (type<=18 && type!=3) seen |= 1U<<type;
        if (type==1) {
            uint32_t bytes;
            if (!StringSize(tag+8,size-8,bytes) || bytes>sizeof(commandLine)) return false;
            for (uint32_t i=0; i<bytes; ++i) commandLine[i]=(char)tag[8+i];
            info.commandLine=(uint32_t)commandLine;
            info.flags |= 1U<<2;
        } else if (type==3) {
            if (size<17 || info.moduleCount==sizeof(modules)/sizeof(modules[0])) return false;
            const uint32_t start=Read32(tag+8), finish=Read32(tag+12);
            uint32_t ignored;
            if (!start || finish<=start || !StringSize(tag+16,size-16,ignored)) return false;
            // No kernel consumer uses boot module descriptions; drop them.
            modules[info.moduleCount++]={start,finish,0,0};
            info.modules=(uint32_t)modules;
            info.flags |= 1U<<3;
        } else if (type==4) {
            if (size!=16) return false;
            info.memLower=Read32(tag+8);
            info.memUpper=Read32(tag+12);
            info.flags |= 1;
        } else if (type==6) {
            if (size<16 || Read32(tag+12)!=0) return false;
            const uint32_t stride=Read32(tag+8);
            if (stride<24 || (size-16)%stride) return false;
            const uint32_t count=(size-16)/stride;
            if (!count || count>sizeof(map)/sizeof(map[0])) return false;
            for (uint32_t i=0; i<count; ++i) {
                const uint8_t* record=tag+16+i*stride;
                const uint64_t base=Read64(record), length=Read64(record+8);
                const uint32_t memoryType=Read32(record+16);
                if (length>~0ULL-base) return false;
                map[i]={20,base,length,memoryType};
                if ((memoryType==3 || memoryType==4) && base<0xF0000000u
                    && base+length<=0xF0000000u && base+length>acpiLimit)
                    acpiLimit=(uint32_t)(base+length);
            }
            info.memoryMapLength=count*sizeof(map[0]);
            info.memoryMap=(uint32_t)map;
            info.flags |= 1U<<6;
        } else if (type==8) {
            if (size<32) return false;
            if (tag[29]==1) {
                if (size<38) return false;
                info.framebufferAddress=Read64(tag+8);
                info.framebufferPitch=Read32(tag+16);
                info.framebufferWidth=Read32(tag+20);
                info.framebufferHeight=Read32(tag+24);
                info.framebufferBitsPerPixel=tag[28];
                info.framebufferType=tag[29];
                for (uint32_t i=0; i<6; ++i) info.framebufferColorInfo[i]=tag[32+i];
                info.flags |= 1U<<12;
            }
        } else if (type==11 || type==12) {
            if (efi || size!=(type==11 ? 12u : 16u)
                || !(type==11 ? Read32(tag+8) : Read64(tag+8))) return false;
            efi=true;
        } else if (type==14 || type==15) {
            const uint32_t bytes=size-8;
            if ((type==14 && bytes!=20) || (type==15 && (bytes<36 || bytes>sizeof(rsdp))))
                return false;
            if (type==15 || !rsdpBytes) {
                for (uint32_t i=0; i<bytes; ++i) rsdp[i]=tag[8+i];
                rsdpBytes=bytes;
            }
        } else if (type==18) {
            return false; // EFI boot services still own memory and hardware.
        }
        offset += (size+7)&~7U;
    }
    ready=end && (info.flags&(1U<<6));
    return ready;
}
