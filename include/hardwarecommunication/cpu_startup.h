#ifndef __GTOS__HARDWARECOMMUNICATION__CPU_STARTUP_H
#define __GTOS__HARDWARECOMMUNICATION__CPU_STARTUP_H

#include <hardwarecommunication/cpu.h>
#include <memory/physical.h>

namespace gtos { namespace hardwarecommunication {
    enum CpuStartupState {
        CpuStartupUnattempted, CpuStartupBootstrap, CpuStartupUnsupportedId,
        CpuStartupNoStack, CpuStartupStarting, CpuStartupEntered,
        CpuStartupSelfTestFailed, CpuStartupParked, CpuStartupTimedOut,
        CpuStartupDeliveryFailed
    };
    enum CpuStartupError {
        CpuStartupOk, CpuStartupAlreadyRun, CpuStartupInterruptsEnabled,
        CpuStartupPagingEnabled, CpuStartupNoFirmware, CpuStartupInvalidFirmware,
        CpuStartupNoLocalApic, CpuStartupX2ApicUnsupported, CpuStartupBadApicBase,
        CpuStartupNoTrampoline, CpuStartupBadTrampoline, CpuStartupNoSharedMemory,
        CpuStartupTimerUnavailable, CpuStartupPartialFailure
    };
    struct CpuStartupProcessorInfo {
        uint32_t apicId;
        CpuStartupState state;
        uint32_t stackBase;
        uint32_t stackPages;
        uint32_t observedApicId;
        uint32_t observedStackPointer;
        uint32_t selfTestChecksum;
    };
    struct CpuStartupReport {
        CpuStartupError error;
        uint32_t detectedProcessors;
        uint32_t attemptedAps;
        uint32_t acknowledgedAps;
        uint32_t parkedAps;
        uint32_t failedAps;
        uint32_t schedulerOnlineProcessors;
        uint32_t trampolineAddress;
        uint32_t localApicAddress;
    };
    struct CpuStartupShared;

    // One-shot boot-time xAPIC startup. APs execute an isolated integer/stack/
    // identity self-test, publish an acknowledgment and park with IF clear.
    // They never enter the BSP scheduler, allocator or device drivers.
    class CpuStartup {
        CpuStartupShared* shared;
        CpuStartupReport report;
        bool used;
        bool InitializeTrampoline(uint32_t address);
        bool SendIpi(uint32_t apicId, uint32_t command);
        bool WaitDelivery();
        static void ApplicationProcessorEntry(void* record) __attribute__((noreturn));
        CpuStartup(const CpuStartup&);
        CpuStartup& operator=(const CpuStartup&);
    public:
        CpuStartup();
        // Must run on the BSP, with IF clear and paging disabled. Requires a
        // validated firmware inventory and flat 4-GiB segments. Startup memory
        // is intentionally retained for the entire boot, including on timeout:
        // a late AP must never enter freed/reused stack or trampoline storage.
        bool Start(const CpuInfo& cpu, memory::PhysicalMemoryManager& frames,
                   uint32_t accessiblePhysicalBytes);
        CpuStartupReport GetReport() const;
        bool GetProcessor(uint32_t index, CpuStartupProcessorInfo& result) const;
        static const char* ErrorName(CpuStartupError error);
        // Revalidates the existing firmware table without modifying CpuManager.
        // Successful inventory includes its BSP exactly once and at most256 IDs.
        static bool ReadFirmwareInventory(const CpuInfo& cpu, uint32_t accessibleBytes,
                                          uint32_t* apicIds, uint32_t capacity,
                                          uint32_t& count, uint32_t& localApicAddress);
    };
} }
#endif
