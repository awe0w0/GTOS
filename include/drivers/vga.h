#ifndef __GTOS__DRIVERS__VGA_H
#define __GTOS__DRIVERS__VGA_H

#include <common/types.h>
#include <hardwarecommunication/port.h>
#include <drivers/driver.h>

namespace gtos {
    namespace drivers {
        
        class VideoGraphicsArray {
            protected:
                gtos::hardwarecommunication::Port8Bit miscPort;
                gtos::hardwarecommunication::Port8Bit crtcIndexPort;
                gtos::hardwarecommunication::Port8Bit crtcDataPort;
                gtos::hardwarecommunication::Port8Bit sequencerIndexPort;
                gtos::hardwarecommunication::Port8Bit sequencerDataPort;
                gtos::hardwarecommunication::Port8Bit graphicsControllerIndexPort;
                gtos::hardwarecommunication::Port8Bit graphicsControllerDataPort;
                gtos::hardwarecommunication::Port8Bit attributeControllerIndexPort;
                gtos::hardwarecommunication::Port8Bit attributeControllerReadPort;
                gtos::hardwarecommunication::Port8Bit attributeControllerWritePort;
                gtos::hardwarecommunication::Port8Bit attributeControllerResetPort;

                void WriteRegisters(uint8_t* registers);
                uint8_t* GetFrameBufferSegment();


                virtual uint8_t GetColorIndex(uint8_t r, uint8_t g, uint8_t b);
            public:
                VideoGraphicsArray();
                ~VideoGraphicsArray();
                virtual bool SupportsMode(uint32_t width, uint32_t height, uint32_t colordepth);
                virtual bool SetMode(uint32_t width, uint32_t height, uint32_t colordepth);
                virtual void PutPixel(int32_t x, int32_t y, uint8_t r,uint8_t g, uint8_t b);
                virtual void PutPixel(int32_t x, int32_t y, uint32_t colorIndex);
                
                virtual void FillRectangle(uint32_t x, uint32_t y, uint32_t w, uint32_t h , uint8_t r, uint8_t g, uint8_t b);
        };
    }
}

#endif