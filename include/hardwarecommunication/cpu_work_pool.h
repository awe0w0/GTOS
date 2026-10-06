#ifndef __GTOS__HARDWARECOMMUNICATION__CPU_WORK_POOL_H
#define __GTOS__HARDWARECOMMUNICATION__CPU_WORK_POOL_H
#include <hardwarecommunication/cpu_startup.h>
#include <hardwarecommunication/cpu_work_queue.h>
#include <memory/paging.h>
namespace gtos { namespace hardwarecommunication {
    enum CpuWorkerState { CpuWorkerPrepared, CpuWorkerStarting, CpuWorkerIdle, CpuWorkerBusy, CpuWorkerFaulted };
    enum CpuWorkerFailure { CpuWorkerNoFailure, CpuWorkerMemoryTypeMismatch, CpuWorkerApicMismatch, CpuWorkerSelfTestMismatch, CpuWorkerException };
    enum CpuWorkPoolError { CpuWorkPoolOk, CpuWorkPoolBadState, CpuWorkPoolWrongCpu,
        CpuWorkPoolNoMemory, CpuWorkPoolBadPaging, CpuWorkPoolStartupFailed,
        CpuWorkPoolReadyTimeout, CpuWorkPoolWakeFailed, CpuWorkPoolBadMemoryTypes, CpuWorkPoolWorkerSetupFailed };
    enum CpuWorkSubmitStatus { CpuWorkAccepted, CpuWorkAcceptedWakeFailed, CpuWorkFull,
        CpuWorkInvalid, CpuWorkExhausted, CpuWorkOffline, CpuWorkWrongContext };
    enum CpuWorkCollectStatus { CpuWorkTicketInvalid, CpuWorkPending, CpuWorkComplete, CpuWorkFailed };
    struct CpuWorkTicket { uint32_t workerIndex; WorkTicket ticket; CpuWorkTicket(); };
    struct CpuWorkerSnapshot {
        uint32_t apicId;
        CpuWorkerState state;
        CpuWorkerFailure failure;
        uint32_t observedCr3, observedCr0, wakeInterrupts, spuriousInterrupts;
        uint32_t faultVector, faultError, faultInstruction, faultAddress;
        WorkQueueStats queue;
    };
    struct CpuWorkPoolReport {
        CpuWorkPoolError error;
        uint32_t configuredWorkers, readyWorkers, busyWorkers, faultedWorkers;
        uint32_t completedJobs, schedulerOnlineProcessors, sharedPageDirectory;
    };
    struct CpuWorkPoolShared;
    // BSP-only producer/control plane; each AP consumes its own bounded queue.
    // Jobs never invoke the global allocator, device drivers or BSP scheduler.
    class CpuWorkPool {
        CpuWorkPoolShared* shared;
        CpuStartup* startup;
        CpuWorkPoolError error;
        bool prepared, started;
        bool OnBootstrapProcessor() const;
        int FindWorker(uint32_t apicId) const;
        static void ApplicationProcessorMain(uint32_t apicId, void* context) __attribute__((noreturn));
        CpuWorkPool(const CpuWorkPool&);
        CpuWorkPool& operator=(const CpuWorkPool&);
    public:
        CpuWorkPool();
        // Call after CpuStartup::Prepare and before KernelPaging::prepareIdentity.
        bool Prepare(CpuStartup& boot, memory::PhysicalMemoryManager& frames);
        // Seals a prepared shared identity directory, then invokes StartPrepared
        // with a private AP continuation. BSP paging may be enabled afterward.
        bool Start(memory::KernelPaging& paging);
        CpuWorkSubmitStatus Submit(uint32_t apicId, const WorkRequest& request, CpuWorkTicket& ticket);
        CpuWorkCollectStatus Collect(const CpuWorkTicket& ticket, WorkResult& result);
        bool Kick(uint32_t apicId);
        CpuWorkPoolReport GetReport() const;
        bool GetWorker(uint32_t index, CpuWorkerSnapshot& result) const;
        static const char* ErrorName(CpuWorkPoolError error);
    };
} }
#endif
