#include <drivers/ata.h>
using namespace gtos::drivers;
AdvancedTechnologyAttachment::AdvancedTechnologyAttachment(uint16_t base, bool isMaster)
: dataPort(base), errorPort(base+1), sectorCountPort(base+2), lbaLowPort(base+3),
  lbaMidPort(base+4), lbaHiPort(base+5), devicePort(base+6), commandPort(base+7),
  controlPort(base+0x206), master(isMaster), present(false), sectors(0), lastError(None) {
    model[0] = 0;
}
AdvancedTechnologyAttachment::~AdvancedTechnologyAttachment() {}
void AdvancedTechnologyAttachment::Delay() {
    for (uint32_t i=0;i<4;i++) controlPort.Read();
}
bool AdvancedTechnologyAttachment::Wait(bool drq) {
    for (uint32_t i=0;i<1000000;i++) {
        uint8_t s=commandPort.Read();
        if (s==0 || s==0xFF) { lastError=NoDevice; return false; }
        if (s&0x80) continue;
        if (s&0x21) { lastError=DeviceError; return false; }
        if (!drq || (s&8)) { lastError=None; return true; }
    }
    lastError=Timeout; return false;
}
bool AdvancedTechnologyAttachment::Identify() {
    present=false; sectors=0; model[0]=0;
    controlPort.Write(2); // Disable device IRQ: transfers are polled.
    devicePort.Write(master ? 0xA0 : 0xB0); Delay();
    sectorCountPort.Write(0); lbaLowPort.Write(0); lbaMidPort.Write(0); lbaHiPort.Write(0);
    commandPort.Write(0xEC); Delay();
    uint8_t s=commandPort.Read();
    if (s==0 || s==0xFF) { lastError=NoDevice; return false; }
    if (!Wait(true)) return false;
    if (lbaMidPort.Read()!=0 || lbaHiPort.Read()!=0) { lastError=Unsupported; return false; }
    uint16_t words[256];
    for (uint32_t i=0;i<256;i++) words[i]=dataPort.Read();
    if (!(words[49]&(1<<9))) { lastError=Unsupported; return false; }
    if ((words[106]&0xD000)==0x5000 && (words[117]!=256 || words[118]!=0)) {
        lastError=Unsupported; return false;
    }
    sectors=((uint32_t)words[61]<<16)|words[60];
    if (!sectors) { lastError=Unsupported; return false; }
    if (sectors>0x10000000) sectors=0x10000000;
    for (uint32_t i=0;i<20;i++) { model[2*i]=words[27+i]>>8; model[2*i+1]=words[27+i]&255; }
    model[40]=0;
    for (int i=39;i>=0 && model[i]==' ';i--) model[i]=0;
    present=true; lastError=None; return true;
}
bool AdvancedTechnologyAttachment::Select(uint32_t sector) {
    if (!present || sector>=sectors || sector>=0x10000000) { lastError=BadArgument; return false; }
    devicePort.Write((master?0xE0:0xF0)|((sector>>24)&15)); Delay();
    if (!Wait(false)) return false;
    errorPort.Write(0); sectorCountPort.Write(1);
    lbaLowPort.Write(sector&255); lbaMidPort.Write((sector>>8)&255); lbaHiPort.Write((sector>>16)&255);
    return true;
}
bool AdvancedTechnologyAttachment::Read28(uint32_t sector, uint8_t* data, int count) {
    if (!data || count<1 || count>512) { lastError=BadArgument; return false; }
    if (!Select(sector)) return false;
    commandPort.Write(0x20); Delay();
    if (!Wait(true)) return false;
    for (int i=0;i<512;i+=2) {
        uint16_t w=dataPort.Read();
        if (i<count) data[i]=w&255;
        if (i+1<count) data[i+1]=w>>8;
    }
    Delay(); return Wait(false);
}
bool AdvancedTechnologyAttachment::Write28(uint32_t sector, const uint8_t* data, int count) {
    if (!data || count<1 || count>512) { lastError=BadArgument; return false; }
    if (!Select(sector)) return false;
    commandPort.Write(0x30); Delay();
    if (!Wait(true)) return false;
    for (int i=0;i<512;i+=2) {
        uint16_t w=i<count ? data[i] : 0;
        if (i+1<count) w|=(uint16_t)data[i+1]<<8;
        dataPort.Write(w);
    }
    Delay(); return Wait(false);
}
bool AdvancedTechnologyAttachment::Flush() {
    if (!present) { lastError=NoDevice; return false; }
    devicePort.Write(master?0xE0:0xF0); Delay();
    if (!Wait(false)) return false;
    commandPort.Write(0xE7); Delay(); return Wait(false);
}
