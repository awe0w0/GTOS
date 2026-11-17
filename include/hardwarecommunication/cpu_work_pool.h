#ifndef __GTOS__HARDWARECOMMUNICATION__CPU_WORK_POOL_H
#define __GTOS__HARDWARECOMMUNICATION__CPU_WORK_POOL_H
#include <hardwarecommunication/cpu_memory_types.h>
#include <hardwarecommunication/cpu_startup.h>
#include <hardwarecommunication/cpu_work_queue.h>
#include <memory/paging.h>
namespace gtos { namespace hardwarecommunication {
    // Parked is a terminal diagnostic state: the worker no longer takes jobs and no longer touches any payload the
    // BSP may reclaim. It is reached only through CloseAdmission followed by Drain.
    enum CpuWorkerState { CpuWorkerPrepared, CpuWorkerStarting, CpuWorkerIdle, CpuWorkerBusy, CpuWorkerFaulted,
        CpuWorkerParkRequested, CpuWorkerParked };
    enum CpuWorkerFailure { CpuWorkerNoFailure, CpuWorkerMemoryTypeMismatch, CpuWorkerApicMismatch,
        CpuWorkerSelfTestMismatch, CpuWorkerException, CpuWorkerPagingMismatch };
    enum CpuWorkPoolError { CpuWorkPoolOk, CpuWorkPoolBadState, CpuWorkPoolWrongCpu,
        CpuWorkPoolNoMemory, CpuWorkPoolBadPaging, CpuWorkPoolStartupFailed,
        CpuWorkPoolReadyTimeout, CpuWorkPoolWakeFailed, CpuWorkPoolBadMemoryTypes, CpuWorkPoolWorkerSetupFailed,
        CpuWorkPoolClosed };
    enum CpuWorkSubmitStatus { CpuWorkAccepted, CpuWorkAcceptedWakeFailed, CpuWorkFull,
        CpuWorkInvalid, CpuWorkExhausted, CpuWorkOffline, CpuWorkWrongContext };
    enum CpuWorkCollectStatus { CpuWorkTicketInvalid, CpuWorkPending, CpuWorkComplete, CpuWorkFailed };
    struct CpuWorkTicket {
        uint32_t workerIndex;
        // The session this ticket was issued in. A ticket from an earlier pool carries a different session and cannot be
        // collected against this one. The raw WorkTicket below remains queue-local.
        uint32_t session;
        // The queue this ticket was issued on. Two workers number their own tickets from one, so (slot, id) alone is
        // ambiguous across workers; this tag is what makes a ticket unaddressable on any other worker.
        uint32_t queueTag;
        WorkTicket ticket;
        CpuWorkTicket();
    };
    struct CpuWorkerSnapshot {
        uint32_t apicId;
        CpuWorkerState state;
        CpuWorkerFailure failure;
        CpuMemoryTypeFailure memoryTypeFailure;
        uint32_t observedCr3, observedCr0, observedCr4, wakeInterrupts, spuriousInterrupts;
        uint32_t faultVector, faultError, faultInstruction, faultAddress;
        WorkQueueStats queue;
        // The park protocol. `parkRequested` is written by the BSP; `parkAckedSession` is written by the AP itself
        // after it has left its work loop, and carries the session it belongs to so a stale acknowledgement from an
        // earlier run cannot be mistaken for this one.
        uint32_t parkRequested;
        uint32_t parkAckedSession;
        uint32_t parkAckCount;
    };
    struct CpuWorkPoolReport {
        CpuWorkPoolError error;
        uint32_t configuredWorkers, readyWorkers, busyWorkers, faultedWorkers;
        uint32_t completedJobs, schedulerOnlineProcessors, sharedPageDirectory;
        // Distinguishes "the BSP asked" from "the AP confirmed". A report where requested equals workers but acked is
        // zero is a pool that has been asked and has not confirmed.
        uint32_t parkRequestedWorkers, parkAckedWorkers, session;
    };
    struct CpuWorkPoolShared;

    // A pool ticket bound to the pool, worker and queue it was issued on. The raw WorkTicket is queue-local, so two
    // workers can hold tickets that compare equal; a holder that carries this identity cannot address the wrong queue
    // and cannot read a result that belongs to another worker. The handle is only meaningful while the pool is alive,
    // which the caller guarantees because every owner lives until the guest ends.
    struct CpuWorkHolder {
        CpuWorkPoolShared* pool;
        uint32_t workerIndex;
        uint32_t apicId;
        WorkTicket ticket;
        // The session this holder was bound in. A holder from an earlier pool reports IsBound() false.
        uint32_t session;
        CpuWorkHolder();
        bool IsBound() const;
        // True only when this holder names the given pool session and worker. A holder from an earlier pool fails here
        // even if its worker index happens to match.
        bool BoundTo(uint32_t poolSession, uint32_t index) const;
    };
    // BSP-only producer/control plane; each AP consumes its own bounded queue.
    // Jobs never invoke the global allocator, device drivers or BSP scheduler.
#ifdef GTOS_CPU_WORK_POOL_HOST_TEST
    // Host-unit seam. These install the shared block that Prepare and Start would have produced, so the admission,
    // drain, park and queue rules can be exercised against real worker records without an allocator or real APs.
    // They exist only in a host test build and cannot appear in a production object.
    extern "C" bool gtos_host_install_pool(uint32_t workerCount, uint32_t bspApicId, uint32_t lapicAddress);
    extern "C" bool gtos_host_attach_pool(void* poolAddress);
    extern "C" bool gtos_host_worker_execute(uint32_t workerIndex, uint32_t apicId);
    // Runs one real worker step: the same function the AP loop calls. It publishes Running, executes one job through the
    // production queue and returns to Idle, so a test can observe the genuine intermediate state rather than a state a
    // setter fabricated.
    // Runs one real worker step up to its interior boundary and returns whether it paused there. A second call resumes
    // and completes the same job. Two calls are one step; neither fabricates a state.
    extern "C" bool gtos_host_worker_step(uint32_t workerIndex);
    extern "C" bool gtos_host_worker_resume(uint32_t workerIndex);
    // Arms or releases a pause at that step's interior boundary. The step itself is production code; only the pause is
    // test-only, and it cannot fabricate a terminal state.
    extern "C" void gtos_host_step_gate(uint32_t armed);
    extern "C" uint32_t gtos_host_step_reached();
    extern "C" uint32_t gtos_host_worker_state(uint32_t workerIndex);
    extern "C" void gtos_host_reset_pool();
#endif
    class CpuWorkPool {
        CpuWorkPoolShared* shared;
        CpuStartup* startup;
        CpuWorkPoolError error;
        bool prepared, started, closed;
        // The session stamp this instance reports when it has no shared block yet. Once a pool is prepared the stamp
        // lives in the shared block, so every reader sees the same value.
        uint32_t sessionStamp;
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
        bool Prepare(CpuStartup& boot, memory::PhysicalMemoryManager& frames,
                     bool restoreTcgMtrrs = false);
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
        // Two-step: RequestPark asks every live worker to leave its work loop and writes the request; no worker writes
        // its own confirmation here. ObservePark reports how many workers have confirmed with the CURRENT session stamp.
        // A pool that has only been asked is never reported as parked.
        bool RequestPark();
        bool ObservePark(uint32_t& requested, uint32_t& acked) const;
        bool ParkWorkers();
        // The physical range this pool owns. Retained for the pool's whole life, including after a failed Start, because
        // an AP may still be reading it.
        bool OwnedRange(uint32_t& address, uint32_t& pages) const;
        bool AdmissionClosed() const;
        // Number of jobs submitted on this pool, for a caller that must not confuse a configured worker with an
        // executed job.
        uint32_t SubmittedJobs() const;
        // The session this pool instance belongs to. Set once at construction, so a value observed here can be compared
        // with the stamp an AP wrote and a stale confirmation from an earlier pool is visibly different.
        uint32_t Session() const;
        // True only when the AP delivery call succeeded and admission may open. Distinct from the owner being published:
        // a Start that failed may still have entered APs, whose stacks, tables and shared block must stay alive.
        bool Started() const;
        // True when the shared owner block is published. Publishing happens before delivery so an entering AP can
        // identify itself, and is never undone, because an AP may already be reading it.
        bool OwnerPublished() const;
        // The queue a worker index owns, for a holder that must prove which queue it is addressing. Null when the index
        // is not a worker of this pool.
        const CpuWorkQueue* WorkerQueue(uint32_t workerIndex) const;
        CpuWorkPoolReport GetReport() const;
        bool GetWorker(uint32_t index, CpuWorkerSnapshot& result) const;
        static const char* ErrorName(CpuWorkPoolError error);
    };
} }
#endif
