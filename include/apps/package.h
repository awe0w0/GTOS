#ifndef GTOS_APPS_PACKAGE_H
#define GTOS_APPS_PACKAGE_H
#include <common/types.h>
namespace gtos { namespace apps {
const uint32_t PackageLimit=8192, HeaderSize=128, RegisterCount=32;
uint32_t Read32(const uint8_t* p);
void Write32(uint8_t* p, uint32_t v);
uint32_t CRC32(const uint8_t* p, uint32_t len);
uint32_t PackageCRC(const uint8_t* p, uint32_t len);
enum PackageError { PackageOK, PackageSize, PackageMagic, PackageVersion,
    PackageChecksum, PackageManifest, PackageInstruction };
struct PackageInfo {
    char id[24], title[24], summary[40];
    uint32_t size, instructionCount, entry;
};
PackageError ValidatePackage(const uint8_t* bytes, uint32_t len, PackageInfo* info=0);
const char* PackageErrorText(PackageError error);
} }
#endif
