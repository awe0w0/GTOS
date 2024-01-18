#ifndef __GTOS__DRIVERS__ATA_H
#define __GTOS__DRIVERS__ATA_H

#include <common/types.h>
#include <hardwarecommunication/port.h>

namespace gtos {
    namespace drivers {
        class AdvancedTechnologyAttachment {
        protected:
            hardwarecommunication::Port16Bit dataPort;
            hardwarecommunication::Port8Bit errorPort;
            hardwarecommunication::Port8Bit sectorCountPort;
            hardwarecommunication::Port8Bit lbaLowPort;
            hardwarecommunication::Port8Bit lbaMidPort;
            hardwarecommunication::Port8Bit lbaHiPort;
            hardwarecommunication::Port8Bit devicePort;
            hardwarecommunication::Port8Bit commandPort;
            hardwarecommunication::Port8Bit controlPort;

            bool master;
            uint16_t bytesPerSector;
        public:
            AdvancedTechnologyAttachment(uint16_t portBase, bool master);
            ~AdvancedTechnologyAttachment();

            void Identify();
            void Read28(uint32_t sector, uint8_t* data, int count);
            void Write28(uint32_t sector, uint8_t* data, int count);
            void Flush();
        };
    }
}

#endif