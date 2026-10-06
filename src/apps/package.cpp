#include <apps/package.h>
using namespace gtos::apps;
uint32_t gtos::apps::Read32(const uint8_t* p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
void gtos::apps::Write32(uint8_t* p,uint32_t v) {
    p[0]=v&255; p[1]=(v>>8)&255; p[2]=(v>>16)&255; p[3]=(v>>24)&255;
}
static uint32_t UpdateCRC(uint32_t c,uint8_t b) {
    c^=b;
    for (uint32_t i=0;i<8;i++) c=(c>>1)^((c&1)?0xEDB88320U:0);
    return c;
}
uint32_t gtos::apps::CRC32(const uint8_t* p,uint32_t len) {
    uint32_t c=0xFFFFFFFFU;
    for (uint32_t i=0;i<len;i++) c=UpdateCRC(c,p[i]);
    return ~c;
}
uint32_t gtos::apps::PackageCRC(const uint8_t* p,uint32_t len) {
    uint32_t c=0xFFFFFFFFU;
    for (uint32_t i=0;i<len;i++) c=UpdateCRC(c,(i>=120&&i<124)?0:p[i]);
    return ~c;
}
static bool ValidString(const uint8_t* p,uint32_t len,bool id,bool nonempty=true) {
    bool end=false;
    if (nonempty && !p[0]) return false;
    for (uint32_t i=0;i<len;i++) {
        uint8_t c=p[i];
        if (!c) { end=true; continue; }
        if (end) return false;
        if (id) {
            if (!(c>='a'&&c<='z') && !(c>='0'&&c<='9') && c!='_' && c!='-') return false;
        } else if (c<32 || c>126) return false;
    }
    return end;
}
PackageError gtos::apps::ValidatePackage(const uint8_t* p,uint32_t len,PackageInfo* info) {
    if (!p || len<HeaderSize+8 || len>PackageLimit || (len-HeaderSize)%8) return PackageSize;
    const uint8_t magic[8]={'G','T','A','P','P','0','1',0};
    for (uint32_t i=0;i<8;i++) if (p[i]!=magic[i]) return PackageMagic;
    if (Read32(p+8)!=1 || Read32(p+12)!=HeaderSize) return PackageVersion;
    uint32_t n=Read32(p+20);
    if (Read32(p+16)!=len || n!=(len-HeaderSize)/8 || Read32(p+24)>=n) return PackageSize;
    if (Read32(p+120)!=PackageCRC(p,len)) return PackageChecksum;
    if (Read32(p+28) || Read32(p+124) || !ValidString(p+32,24,true)
        || !ValidString(p+56,24,false) || !ValidString(p+80,40,false,false)) return PackageManifest;
    for (uint32_t i=0;i<n;i++) {
        const uint8_t* q=p+HeaderSize+8*i;
        uint8_t op=q[0],a=q[1],b=q[2],c=q[3]; uint32_t v=Read32(q+4);
        bool ok=false;
        switch(op) {
            case 0: case 18: ok=!a&&!b&&!c&&!v; break;
            case 1: ok=a<32&&!b&&!c; break;
            case 2: ok=a<32&&b<32&&!c&&!v; break;
            case 3: case 4: case 5: case 6: case 7: case 8: case 9:
                ok=a<32&&b<32&&c<32&&!v; break;
            case 10: ok=!a&&!b&&!c&&v<n; break;
            case 11: ok=a<32&&!b&&!c&&v<n; break;
            case 12: ok=a<32&&!b&&!c&&v<4; break;
            case 13: ok=a<32&&!b&&!c&&v>0&&v<=1000000; break;
            case 14: ok=!a&&!b&&!c&&v<256; break;
            case 15: ok=a<32&&b<32&&c<32&&v<65536&&(v&255)<32; break;
            case 16: ok=a<32&&b<32&&!c&&v<2; break;
            case 17: ok=a<32&&b<32&&c<32&&v<256; break;
        }
        if (!ok) return PackageInstruction;
    }
    if (info) {
        for (uint32_t i=0;i<24;i++) { info->id[i]=p[32+i]; info->title[i]=p[56+i]; }
        for (uint32_t i=0;i<40;i++) info->summary[i]=p[80+i];
        info->size=len; info->instructionCount=n; info->entry=Read32(p+24);
    }
    return PackageOK;
}
const char* gtos::apps::PackageErrorText(PackageError error) {
    switch(error) {
        case PackageOK:return "Package valid";
        case PackageSize:return "Invalid package length";
        case PackageMagic:return "Not a GTOS app package";
        case PackageVersion:return "Unsupported package version";
        case PackageChecksum:return "Package checksum mismatch";
        case PackageManifest:return "Invalid app manifest";
        case PackageInstruction:return "Invalid bytecode instruction";
    }
    return "Unknown package error";
}
