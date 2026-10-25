#ifndef __GTOS__HARDWARECOMMUNICATION__CPU_WORK_POOL_H
#define __GTOS__HARDWARECOMMUNICATION__CPU_WORK_POOL_H
#include <hardwarecommunication/cpu_startup.h>
#include <hardwarecommunication/cpu_work_queue.h>
#include <memory/paging.h>
namespace gtos { namespace hardwarecommunication {
    // Parked is a terminal diagnostic state: the worker no longer takes jobs and no longer touches any payload the
    // BSP may reclaim. It is reached only through CloseAdmission followed by Drain.
    enum CpuWorkerState { CpuWorkerPrepared, CpuWorkerStarting, CpuWorkerIdle, CpuWorkerBusy, CpuWorkerFaulted,
        CpuWorkerParked };
    enum CpuWorkerFailure { CpuWorkerNoFailure, CpuWorkerMemoryTypeMismatch, CpuWorkerApicMismatch, CpuWorkerSelfTestMismatch, CpuWorkerException };
    enum CpuWorkPoolError { CpuWorkPoolOk, CpuWorkPoolBadState, CpuWorkPoolWrongCpu,
        CpuWorkPoolNoMemory, CpuWorkPoolBadPaging, CpuWorkPoolStartupFailed,
        CpuWorkPoolReadyTimeout, CpuWorkPoolWakeFailed, CpuWorkPoolBadMemoryTypes, CpuWorkPoolWorkerSetupFailed,
        CpuWorkPoolClosed };
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
#ifdef GTOS_CPU_WORK_POOL_HOST_TEST
    // Host-unit seam. These install the shared block that Prepare and Start would have produced, so the admission,
    // drain, park and queue rules can be exercised against real worker records without an allocator or real APs.
    // They exist only in a host test build and cannot appear in a production object.
    extern "C" bool gtos_host_install_pool(uint32_t workerCount, uint32_t bspApicId, uint32_t lapicAddress);
    extern "C" bool gtos_host_attach_pool(void* poolAddress);
    extern "C" bool gtos_host_worker_execute(uint32_t workerIndex, uint32_t apicId);
    extern "C" uint32_t gtos_host_worker_state(uint32_t workerIndex);
    extern "C" void gtos_host_reset_pool();
#endif
    class CpuWorkPool {
        CpuWorkPoolShared* shared;
        CpuStartup* startup;
        CpuWorkPoolError error;
        bool prepared, started, closed;
        bool OnBootstrapProcessor() const;
        int FindWorker(uint32_t apicId) const;
        static void ApplicationProcessorMain(uint32_t apicId, void* context) __attribute__((noreturn));
        CpuWorkPool(const CpuWorkPool&);
        CpuWorkPool& operator=(const CpuWorkPool&);
#ifdef GTOS_CPU_WORK_POOL_HOST_TEST
        // The host unit installs the shared block that Prepare and Start would have produced. Access is granted to one
        // struct in the test build only, so a production object cannot reach private state this way.
        friend struct CpuWorkPoolHostTest;
#endif
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
        // One-way: after this returns true, no new job is admitted on any worker, whatever its state. Existing
        // in-flight work is untouched and every ticket already issued stays collectable. Calling it twice is a
        // no-op that reports the already-closed state.
        bool CloseAdmission();
        // Reports whether every worker has finished what it was given: no pending slot, no busy worker, and no
        // completed-but-uncollected slot. It does not close anything and does not wait.
        bool IsDrained(uint32_t& queued, uint32_t& busy, uint32_t& uncollected) const;
        // Parks every worker that has nothing left to do. Refuses while anything is still in flight, because parking
        // an AP that still holds a job would abandon that job silently. After a successful park the workers take no
        // further jobs and the BSP may reclaim the payloads.
        bool ParkWorkers();
        bool AdmissionClosed() const;
        // Number of jobs submitted on this pool, for a caller that must not confuse a configured worker with an
        // executed job.
        uint32_t SubmittedJobs() const;
        CpuWorkPoolReport GetReport() const;
        bool GetWorker(uint32_t index, CpuWorkerSnapshot& result) const;
        static const char* ErrorName(CpuWorkPoolError error);
    };
} }
#endif
