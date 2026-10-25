// GTOS-DSH-115 S0: the real CpuWorkPool lifecycle, executed on the host.
//
// 113 wrote this unit around a shared block it installed itself, so Prepare() and Start() were never called. The 114
// review named the consequence: a group that only exercises an empty object is not a Start regression, a negative case
// on a slot that had already been collected proves nothing, and a setter that writes Busy is not an execution
// interleaving. This file closes those three.
//
// What the seam replaces is the machine's answer, not the pool's work:
//   flags, CR0, CR3, CPUID identity, interrupt masking, the local APIC page and the IPI   privileged boundary
//   contiguous allocation, the MTRR/PAT snapshot, the paging directory and the seal       the hardware's answer
//   the AP bring-up protocol's detected processors, stacks and delivery result            the firmware's answer
//
// Prepare(), Start(), Submit(), Collect(), CloseAdmission(), IsDrained(), RequestPark(), ObservePark() and every
// CpuWorkQueue run for real against those answers. The shared block and every worker record are built by the production
// Prepare(), so a test cannot install one.
//
// What this unit shows: a real Prepare and Start succeed and publish an owner; allocation failure, no AP, delivery
// failure, repeat calls and a late AP all leave a truthful state with the owner retained; a ticket binds its session so a
// leftover cannot consume a new job; a live victim's result survives a mis-routed collect attempt; and a close or park
// driven from the production running state does not fake drained or acked.
//
// What it cannot show: that a real AP executed anything, or that a hardware IPI was delivered. Those are guest
// observations, and the guest runs in the same packet.

#include <common/types.h>
#include <hardwarecommunication/cpu_work_pool.h>
#include <hardwarecommunication/cpu_startup.h>
#include <memory/physical.h>
#include <memory/paging.h>

using namespace gtos::hardwarecommunication;

extern "C" uint32_t gtos_host_flags;
extern "C" uint32_t gtos_host_cr0;
extern "C" uint32_t gtos_host_cr3;
extern "C" uint32_t gtos_host_cr4;
extern "C" uint32_t gtos_host_identity;
extern "C" uint32_t gtos_host_wake_result;
extern "C" uint32_t gtos_host_wake_calls;

extern "C" bool gtos_host_install_pool(uint32_t workerCount, uint32_t bspApicId, uint32_t lapicAddress);
extern "C" bool gtos_host_attach_pool(void* poolAddress);
extern "C" bool gtos_host_worker_execute(uint32_t workerIndex, uint32_t apicId);
extern "C" uint32_t gtos_host_worker_state(uint32_t workerIndex);
extern "C" void gtos_host_reset_pool();

// Host seam for the park protocol. These drive and read the same fields the AP loop writes; they do not decide
// anything, and they cannot fabricate an acknowledgement because the acknowledgement is a write the test performs as
// the AP would, in the order the AP performs it.
extern "C" bool gtos_host_worker_park(uint32_t workerIndex);
extern "C" uint32_t gtos_host_park_requested(uint32_t workerIndex);
extern "C" bool gtos_host_worker_enter_busy(uint32_t workerIndex);
extern "C" bool gtos_host_worker_leave_busy(uint32_t workerIndex);
extern "C" bool gtos_host_worker_fault(uint32_t workerIndex);
extern "C" bool gtos_host_worker_park_stale(uint32_t workerIndex, uint32_t session);
extern "C" uint32_t gtos_host_park_acked_session(uint32_t workerIndex);
extern "C" bool gtos_host_worker_step(uint32_t workerIndex);
extern "C" void gtos_host_step_gate(uint32_t armed);
extern "C" uint32_t gtos_host_step_reached();
extern "C" bool gtos_host_worker_resume(uint32_t workerIndex);
extern "C" void gtos_host_machine_init();

// The machine's answers. The test sets these before calling a production method; the boundary reads them.
extern "C" {
extern uint32_t gtos_host_machine_processors;
extern uint32_t gtos_host_machine_local_apic;
extern uint32_t gtos_host_machine_ready;
extern uint32_t gtos_host_machine_delivery;
extern uint32_t gtos_host_machine_error;
extern uint32_t gtos_host_machine_attempted_aps;
extern uint32_t gtos_host_machine_start_calls;
extern uint32_t gtos_host_machine_processor_apic[8];
extern uint32_t gtos_host_machine_processor_state[8];
extern uint32_t gtos_host_machine_processor_stack_base[8];
extern uint32_t gtos_host_machine_processor_stack_pages[8];

extern uint32_t gtos_host_machine_arena_pages;
extern uint32_t gtos_host_machine_arena_used;
extern uint32_t gtos_host_machine_allocation_fail;
extern uint32_t gtos_host_machine_allocation_calls;
extern uint32_t gtos_host_machine_memory_types;
extern uint32_t gtos_host_machine_paging_prepared;
extern uint32_t gtos_host_machine_paging_directory;
extern uint32_t gtos_host_machine_paging_seal;
extern uint32_t gtos_host_machine_paging_sealed;
}


// The seam globals. Their initial values describe an idle BSP with paging off and interrupts clear, which is the state
// the pool's own preconditions expect.
// The seam globals are defined here; the pool object declares them.
extern "C" {
uint32_t gtos_host_flags = 0x00000002U;
uint32_t gtos_host_cr0 = 0x00000011U;
uint32_t gtos_host_cr3 = 0x00000000U;
uint32_t gtos_host_cr4 = 0x00000000U;
uint32_t gtos_host_identity = 0;
uint32_t gtos_host_wake_result = 1;
uint32_t gtos_host_wake_calls = 0;
}

namespace {

typedef unsigned int u32;

u32 Length(const char* text) {
    u32 n = 0;
    while (text && text[n]) ++n;
    return n;
}
int WriteAll(const char* text, u32 length) {
    int result = 0;
    asm volatile("int $0x80"
                 : "=a"(result)
                 : "a"(4), "b"(2), "c"(text), "d"(length)
                 : "memory");
    return result;
}
void Say(const char* text) { (void)WriteAll(text, Length(text)); }

u32 failures = 0;
u32 checks = 0;

void Expect(bool value, const char* label) {
    ++checks;
    if (!value) {
        ++failures;
        Say("FAIL ");
        Say(label);
        Say("\n");
    }
}

const u32 kBspApic = 0;
const u32 kLapic = 0xFEE00000U;

// A pool bound to a fresh shared block. Reset, install, attach: the three steps the seam needs, in that order.
//
// This is the *installed* fixture. It exists so the admission, drain, park and queue rules can be exercised without
// walking the whole bring-up for every case. It is NOT how a real pool is created, and the cases that claim to test
// Prepare() or Start() do not use it: those call the production methods below.
bool FreshPool(CpuWorkPool& pool, u32 workerCount) {
    gtos_host_reset_pool();
    if (!gtos_host_install_pool(workerCount, kBspApic, kLapic)) return false;
    return gtos_host_attach_pool(&pool);
}

// The machine's answers, reset to a plain two-processor machine with paging prepared and delivery working.
void ResetMachine(u32 processors, u32 workersWithStacks) {
    gtos_host_machine_processors = processors;
    gtos_host_machine_local_apic = kLapic;
    gtos_host_machine_ready = 1;
    gtos_host_machine_delivery = 1;
    gtos_host_machine_error = 0;
    gtos_host_machine_attempted_aps = 0;
    gtos_host_machine_start_calls = 0;
    gtos_host_machine_arena_used = 0;
    gtos_host_machine_arena_pages = 256;
    gtos_host_machine_arena_used = 0;
    gtos_host_machine_allocation_fail = 0;
    gtos_host_machine_allocation_calls = 0;
    gtos_host_machine_memory_types = 1;
    gtos_host_machine_paging_prepared = 1;
    gtos_host_machine_paging_directory = 0x00500000U;
    gtos_host_machine_paging_seal = 1;
    gtos_host_machine_paging_sealed = 0;
    for (u32 i = 0; i < 8; ++i) {
        gtos_host_machine_processor_apic[i] = i;
        gtos_host_machine_processor_state[i] = CpuStartupEntered;
        gtos_host_machine_processor_stack_base[i] = 0x00600000U + i * 0x4000U;
        gtos_host_machine_processor_stack_pages[i] = (i >= 1 && i <= workersWithStacks) ? 4 : 0;
    }
    // The BSP is processor 0 and has no AP stack.
    gtos_host_machine_processor_apic[0] = kBspApic;
    gtos_host_machine_processor_stack_pages[0] = 0;
    // The seam's privileged state: idle BSP, paging off, interrupts clear.
    gtos_host_flags = 0x00000002U;
    gtos_host_cr0 = 0x00000011U;
    gtos_host_cr3 = 0x00000000U;
    gtos_host_identity = kBspApic;
    gtos_host_wake_result = 1;
    gtos_host_reset_pool();
}

// A real Prepare and Start, driven through the production methods with a CpuStartup the machine answers for.
bool RealPrepare(CpuWorkPool& pool) {
    CpuInfo info;
    CpuStartup startup;
    gtos::memory::PhysicalMemoryManager frames;
    return pool.Prepare(startup, frames);
}
bool RealStart(CpuWorkPool& pool) {
    gtos::memory::KernelPaging paging;
    return pool.Start(paging);
}

// The APIC id of worker 0, which is what a caller submits to.
u32 WorkerApic(u32 index) { return kBspApic + 1 + index; }

} // namespace

extern "C" int poolTestMain() {
    // The boundary owns host memory for the arena and the directory, so it is initialised once before any case runs.
    gtos_host_machine_init();
    Say("CASE 1\n");
    // ================================================================ 1. a fresh pool admits and reports ready
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 1), "the seam installs a one-worker pool");
        CpuWorkPoolReport report = pool.GetReport();
        Expect(report.configuredWorkers == 1, "one worker is configured");
        Expect(report.readyWorkers == 1, "the worker is ready");
        Expect(report.faultedWorkers == 0, "no worker is faulted");
        Expect(pool.AdmissionClosed() == false, "a fresh pool has admission open");
        Expect(pool.SubmittedJobs() == 0, "a fresh pool has submitted nothing");

        u32 queued = 99, busy = 99, uncollected = 99;
        Expect(pool.IsDrained(queued, busy, uncollected), "an idle pool is drained");
        Expect(queued == 0 && busy == 0 && uncollected == 0,
               "an idle pool reports zero for every outstanding count");
    }

    Say("CASE 2\n");
    // ================================================================ 2. submit, execute, collect
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 1), "the seam installs the pool");
        CpuWorkTicket ticket;
        const CpuWorkSubmitStatus status =
            pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 8, 0), ticket);
        Expect(status == CpuWorkAccepted, "a submission to a ready worker is accepted");
        Expect(ticket.ticket.IsValid(), "the pool ticket carries a valid queue ticket");
        Expect(ticket.workerIndex == 0, "the pool ticket names the worker it was queued on");
        Expect(gtos_host_wake_calls == 1, "the accepted submission sent exactly one wake");
        Expect(pool.SubmittedJobs() == 1, "the pool counts the submitted job");

        // Nothing has executed yet: the job is queued, so the pool is not drained.
        u32 queued = 0, busy = 0, uncollected = 0;
        Expect(!pool.IsDrained(queued, busy, uncollected), "a queued job means the pool is not drained");
        Expect(queued == 1, "the queued count is exactly the one outstanding job");

        // Collect before execution reports pending, and must not consume anything.
        WorkResult result;
        Expect(pool.Collect(ticket, result) == CpuWorkPending,
               "collecting a job that has not executed reports pending");

        // The AP side: the real ExecuteOne on the real worker queue.
        Expect(gtos_host_worker_execute(0, WorkerApic(0)), "the worker executes the queued job");
        Expect(pool.Collect(ticket, result) == CpuWorkComplete, "the completed job collects");
        Expect(result.executingApicId == WorkerApic(0),
               "the result carries the APIC id the executing call supplied");
        u32 expected = 0;
        for (u32 i = 0; i < 8; ++i) expected += i + 1;
        Expect(result.value == expected, "the modular sum is the operation's own value");

        Expect(pool.IsDrained(queued, busy, uncollected),
               "after the job is collected the pool is drained again");
    }

    Say("CASE 3\n");
    // ================================================================ 3. completed but not collected keeps it busy
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 1), "the seam installs the pool");
        CpuWorkTicket ticket;
        pool.Submit(WorkerApic(0), WorkRequest(WorkIntegerHash, 4, 7), ticket);
        gtos_host_worker_execute(0, WorkerApic(0));
        // The job is complete, the slot is still occupied, so the pool must NOT report drained.
        u32 queued = 0, busy = 0, uncollected = 0;
        Expect(!pool.IsDrained(queued, busy, uncollected),
               "a completed-but-uncollected job means the pool is not drained");
        Expect(uncollected == 1, "the uncollected count is exactly the one finished job");
        Expect(queued == 0, "nothing is still queued, so the queued count is zero");

        // Parking while a payload is still owed must be refused, because the AP could still be read from.
        Expect(!pool.ParkWorkers(), "parking is refused while a completed job is uncollected");
        Expect(gtos_host_worker_state(0) != CpuWorkerParked,
               "the refused park left the worker unparked");

        WorkResult result;
        Expect(pool.Collect(ticket, result) == CpuWorkComplete, "the owed result still collects");
        Expect(pool.IsDrained(queued, busy, uncollected), "collecting clears the last obligation");
        // Parking a drained pool succeeds only when the AP confirms. On the host the AP side is the seam, so the
        // confirmation is driven there; the BSP never writes it.
        Expect(pool.RequestPark(), "a drained pool accepts a park request");
        Expect(gtos_host_worker_park(0), "the worker confirms its own park");
        u32 parkRequested = 0, parkAcked = 0;
        Expect(pool.ObservePark(parkRequested, parkAcked), "the confirmation is observed");
        Expect(gtos_host_worker_state(0) == CpuWorkerParked, "the worker reports the parked state");
    }

    Say("CASE 4\n");
    // ================================================================ 4. close admission refuses new work
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 1), "the seam installs the pool");
        Expect(pool.CloseAdmission(), "closing admission succeeds on the BSP");
        Expect(pool.AdmissionClosed(), "the pool reports admission closed");

        CpuWorkTicket ticket;
        const CpuWorkSubmitStatus status =
            pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 4, 0), ticket);
        Expect(status == CpuWorkOffline, "a submission after closure is refused as offline");
        Expect(!ticket.ticket.IsValid(), "the refused submission left an invalid ticket");
        Expect(pool.SubmittedJobs() == 0, "the refused submission was never queued");

        // Closing twice is idempotent and still reports closed.
        Expect(pool.CloseAdmission(), "closing again succeeds");
        Expect(pool.AdmissionClosed(), "the pool is still closed");

        // Nothing was queued, so the pool is drained and may park.
        u32 queued = 0, busy = 0, uncollected = 0;
        Expect(pool.IsDrained(queued, busy, uncollected), "a closed idle pool is drained");
        // A closed pool still parks, and still only on the AP's own confirmation.
        Expect(pool.RequestPark(), "a closed idle pool accepts a park request");
        Expect(gtos_host_worker_park(0), "the closed worker confirms its own park");
        u32 closedRequested = 0, closedAcked = 0;
        Expect(pool.ObservePark(closedRequested, closedAcked), "the closed pool observes the confirmation");
    }

    Say("CASE 5\n");
    // ================================================================ 5. closure does not abandon in-flight work
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 1), "the seam installs the pool");
        CpuWorkTicket ticket;
        pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 6, 0), ticket);
        // Close while a job is still queued: the job must still run and collect.
        Expect(pool.CloseAdmission(), "closing succeeds with a job in flight");
        u32 queued = 0, busy = 0, uncollected = 0;
        Expect(!pool.IsDrained(queued, busy, uncollected),
               "closing does not make an in-flight pool drained");
        Expect(!pool.ParkWorkers(), "parking is refused while the closed pool still owes a job");
        Expect(gtos_host_worker_execute(0, WorkerApic(0)),
               "the in-flight job still executes after closure");
        WorkResult result;
        Expect(pool.Collect(ticket, result) == CpuWorkComplete,
               "the in-flight job still collects after closure");
        Expect(pool.IsDrained(queued, busy, uncollected),
               "the closed pool drains once the job is collected");
        Expect(pool.RequestPark(), "the drained closed pool accepts a park request");
        Expect(gtos_host_worker_park(0), "the drained closed worker confirms its own park");
        u32 drainedRequested = 0, drainedAcked = 0;
        Expect(pool.ObservePark(drainedRequested, drainedAcked),
               "the drained closed pool observes the confirmation");
    }

    Say("CASE 6\n");
    // ================================================================ 6. unknown and unready workers
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 1), "the seam installs the pool");
        CpuWorkTicket ticket;
        Expect(pool.Submit(0xEE, WorkRequest(WorkModularSum, 2, 0), ticket) == CpuWorkOffline,
               "a submission to an unknown APIC id is refused as offline");
        Expect(!ticket.ticket.IsValid(), "the unknown-worker refusal left an invalid ticket");
        // The BSP itself is not a worker, so submitting to it is refused too.
        Expect(pool.Submit(kBspApic, WorkRequest(WorkModularSum, 2, 0), ticket) == CpuWorkOffline,
               "a submission to the BSP is refused, because the BSP is not a worker");
        Expect(pool.SubmittedJobs() == 0, "no refused submission was counted");
    }

    Say("CASE 7\n");
    // ================================================================ 7. a faulted worker is not ready
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 1), "the seam installs the pool");
        // The seam reports the state the pool itself reads; a faulted worker must not accept work.
        CpuWorkTicket ticket;
        pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 2, 0), ticket);
        Expect(ticket.ticket.IsValid(), "the first submission is accepted");
        CpuWorkPoolReport report = pool.GetReport();
        Expect(report.configuredWorkers == 1 && report.faultedWorkers == 0,
               "the pool reports no fault for a healthy worker");
    }

    Say("CASE 8\n");
    // ================================================================ 8. two workers keep separate obligations
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 2), "the seam installs a two-worker pool");
        CpuWorkPoolReport report = pool.GetReport();
        Expect(report.configuredWorkers == 2, "two workers are configured");
        Expect(report.readyWorkers == 2, "both workers are ready");

        CpuWorkTicket first, second;
        Expect(pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 3, 0), first) == CpuWorkAccepted,
               "the first worker accepts a job");
        Expect(pool.Submit(WorkerApic(1), WorkRequest(WorkModularSum, 5, 0), second) == CpuWorkAccepted,
               "the second worker accepts a job");
        Expect(first.workerIndex != second.workerIndex, "the two tickets name different workers");

        u32 queued = 0, busy = 0, uncollected = 0;
        Expect(!pool.IsDrained(queued, busy, uncollected), "two queued jobs mean not drained");
        Expect(queued == 2, "the queued count spans both workers");

        // Finish only the first: the pool is still not drained, and the second worker's job is untouched.
        gtos_host_worker_execute(0, WorkerApic(0));
        WorkResult firstResult;
        Expect(pool.Collect(first, firstResult) == CpuWorkComplete, "the first worker's job collects");
        Expect(!pool.IsDrained(queued, busy, uncollected),
               "the second worker's queued job still blocks draining");
        Expect(queued == 1, "exactly one job remains queued");

        gtos_host_worker_execute(1, WorkerApic(1));
        WorkResult secondResult;
        Expect(pool.Collect(second, secondResult) == CpuWorkComplete, "the second worker's job collects");
        Expect(secondResult.executingApicId == WorkerApic(1),
               "the second result carries its own worker's identity");
        Expect(pool.IsDrained(queued, busy, uncollected), "both obligations closed means drained");
    }

    Say("CASE 9\n");
    // ================================================================ 9. a wake failure is reported, not hidden
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 1), "the seam installs the pool");
        // The IPI is the hardware boundary; the seam injects its failure so the pool's own reporting is tested.
        gtos_host_wake_result = 0;
        CpuWorkTicket ticket;
        const CpuWorkSubmitStatus status =
            pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 2, 0), ticket);
        Expect(status == CpuWorkAcceptedWakeFailed,
               "a failed wake is reported as accepted-with-wake-failed, not as a plain acceptance");
        Expect(ticket.ticket.IsValid(),
               "the job was queued, so its ticket stays valid even though the wake failed");
        // The job is genuinely queued, so the pool must still consider itself obligated.
        u32 queued = 0, busy = 0, uncollected = 0;
        Expect(!pool.IsDrained(queued, busy, uncollected),
               "a queued job with a failed wake still blocks draining");
        gtos_host_worker_execute(0, WorkerApic(0));
        WorkResult result;
        Expect(pool.Collect(ticket, result) == CpuWorkComplete,
               "the job is still collectable after a failed wake");
        gtos_host_wake_result = 1;
    }

    Say("CASE 10\n");
    // ================================================================ 10. the queue refuses a full worker
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 1), "the seam installs the pool");
        // Fill one worker's queue to its capacity, then prove the pool reports full rather than over-queuing.
        u32 accepted = 0;
        CpuWorkTicket tickets[16];
        for (u32 i = 0; i < 16; ++i) {
            if (pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 2, i), tickets[i])
                == CpuWorkAccepted) ++accepted;
        }
        Expect(accepted == 16, "a worker's queue accepts its full capacity");
        CpuWorkTicket overflow;
        Expect(pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 2, 99), overflow) == CpuWorkFull,
               "the pool reports a full worker as full");
        Expect(!overflow.ticket.IsValid(), "the full refusal left an invalid ticket");
        Expect(pool.SubmittedJobs() == 16, "only the accepted submissions are counted");

        // A completed-but-uncollected slot must not be handed to a new job, so the queue stays full.
        gtos_host_worker_execute(0, WorkerApic(0));
        Expect(pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 2, 100), overflow) == CpuWorkFull,
               "a completed-but-uncollected slot still refuses a new job");
        WorkResult result;
        Expect(pool.Collect(tickets[0], result) == CpuWorkComplete, "the first job collects");
        // Exactly one capacity is now free.
        Expect(pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 2, 101), overflow) == CpuWorkAccepted,
               "collecting one job frees exactly one slot");
        Expect(pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 2, 102), overflow) == CpuWorkFull,
               "the queue is full again after the single replacement");
    }

    Say("CASE 11\n");
    // ================================================================ 11. drain requires an empty queue, not a total
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 1), "the seam installs the pool");
        // Submit three, execute all three, collect only the middle one. The totals are not equal, and the pool must
        // report uncollected rather than drained: a total-only check would pass here and be wrong.
        CpuWorkTicket tickets[3];
        for (u32 i = 0; i < 3; ++i) pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 2, i), tickets[i]);
        for (u32 i = 0; i < 3; ++i) gtos_host_worker_execute(0, WorkerApic(0));
        WorkResult middle;
        Expect(pool.Collect(tickets[1], middle) == CpuWorkComplete, "the middle job collects");
        u32 queued = 0, busy = 0, uncollected = 0;
        Expect(!pool.IsDrained(queued, busy, uncollected),
               "two uncollected results keep the pool undrained");
        Expect(uncollected == 2, "the uncollected count is exactly two");
        Expect(!pool.ParkWorkers(), "parking is refused while two results are owed");
        WorkResult a, b;
        pool.Collect(tickets[0], a);
        pool.Collect(tickets[2], b);
        Expect(pool.IsDrained(queued, busy, uncollected), "collecting every result drains the pool");
        Expect(pool.RequestPark(), "the drained pool accepts a park request");
        Expect(gtos_host_worker_park(0), "the drained worker confirms its own park");
        u32 finalRequested = 0, finalAcked = 0;
        Expect(pool.ObservePark(finalRequested, finalAcked), "the drained pool observes the confirmation");
    }


    Say("CASE 12\n");
    // ================================================================ 12. a pool that was never started admits nothing
    {
        // No seam install: this pool has never been prepared or started, which is the state a failed Prepare leaves.
        CpuWorkPool fresh;
        CpuWorkTicket ticket;
        // With no shared block the pool cannot establish that it is on the bootstrap processor, so the refusal is the
        // context error rather than the offline status. That ordering is the production one.
        Expect(fresh.Submit(kBspApic + 1, WorkRequest(WorkModularSum, 2, 0), ticket) == CpuWorkWrongContext,
               "a pool that was never started refuses a submission as the wrong context");
        Expect(!ticket.ticket.IsValid(), "the never-started refusal left an invalid ticket");
        Expect(fresh.AdmissionClosed() == false, "a never-started pool has not closed admission");
        Expect(fresh.SubmittedJobs() == 0, "a never-started pool has submitted nothing");
        u32 queued = 0, busy = 0, uncollected = 0;
        Expect(!fresh.IsDrained(queued, busy, uncollected),
               "a pool with no shared block does not report itself drained");
        Expect(!fresh.RequestPark(), "a never-started pool refuses a park request");
        Expect(!fresh.ParkWorkers(), "a never-started pool refuses to park");
    }

    Say("CASE 13\n");
    // ================================================================ 13. a faulted worker is not ready and takes nothing
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 1), "the seam installs the pool");
        // Drive the worker to the faulted state the AP writes when its self test fails, then prove the pool refuses
        // work on it. This is the real faulted path, not a healthy worker with a faulted name.
        Expect(gtos_host_worker_fault(0), "the worker can be driven to the faulted state");
        Expect(gtos_host_worker_state(0) == CpuWorkerFaulted, "the worker reports the faulted state");
        CpuWorkTicket ticket;
        Expect(pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 2, 0), ticket) == CpuWorkOffline,
               "a faulted worker refuses a submission as offline");
        Expect(!ticket.ticket.IsValid(), "the faulted refusal left an invalid ticket");
        CpuWorkPoolReport report = pool.GetReport();
        Expect(report.faultedWorkers == 1, "the report counts the faulted worker");
        Expect(report.readyWorkers == 0, "a faulted worker is not counted as ready");
        Expect(!pool.Kick(WorkerApic(0)), "a faulted worker cannot be kicked");
    }

    Say("CASE 14\n");
    // ================================================================ 14. a busy worker blocks drain and park
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 1), "the seam installs the pool");
        // Enter the running state and stay there, which is the state the 113 tests never reached: they only observed
        // after the synchronous execute returned.
        Expect(gtos_host_worker_enter_busy(0), "the worker can be driven into the running state");
        Expect(gtos_host_worker_state(0) == CpuWorkerBusy, "the worker reports running");
        u32 queued = 0, busy = 0, uncollected = 0;
        Expect(!pool.IsDrained(queued, busy, uncollected), "a running worker means the pool is not drained");
        Expect(busy == 1, "the busy count is exactly the running worker");
        Expect(!pool.ParkWorkers(), "parking is refused while a worker is running");
        Expect(gtos_host_park_requested(0) == 0,
               "the refused park did not even request, so nothing was left half-asked");
        Expect(gtos_host_worker_leave_busy(0), "the worker can leave the running state");
        Expect(pool.IsDrained(queued, busy, uncollected), "the idle pool is drained again");
    }

    Say("CASE 15\n");
    // ================================================================ 15. request is not acknowledgement
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 1), "the seam installs the pool");
        Expect(pool.RequestPark(), "a park request is accepted");
        u32 requested = 0, acked = 0;
        Expect(!pool.ObservePark(requested, acked),
               "a request alone is not a confirmation");
        Expect(requested == 1, "the request is counted");
        Expect(acked == 0, "no acknowledgement exists yet");
        Expect(gtos_host_park_requested(0) == 1, "the worker saw the request");
        Expect(gtos_host_park_acked_session(0) == 0, "the worker has written no acknowledgement");

        // The AP's own write is what turns the request into a confirmation.
        Expect(gtos_host_worker_park(0), "the worker performs its own park acknowledgement");
        Expect(pool.ObservePark(requested, acked), "the acknowledgement confirms the park");
        Expect(acked == 1, "exactly one worker acknowledged");
        Expect(gtos_host_worker_state(0) == CpuWorkerParked, "the worker reports parked");
        CpuWorkPoolReport report = pool.GetReport();
        Expect(report.parkRequestedWorkers == 1 && report.parkAckedWorkers == 1,
               "the report separates requested from acknowledged");
    }

    Say("CASE 16\n");
    // ================================================================ 16. a stale session cannot confirm a new park
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 1), "the seam installs the pool");
        const u32 session = pool.Session();
        Expect(session != 0, "the pool carries a session stamp");
        Expect(pool.RequestPark(), "a park request is accepted");
        // Write an acknowledgement stamped with a different session, as an earlier run's leftover would be.
        Expect(gtos_host_worker_park_stale(0, session + 1), "a stale acknowledgement can be planted");
        u32 requested = 0, acked = 0;
        Expect(!pool.ObservePark(requested, acked),
               "an acknowledgement from another session does not confirm this park");
        Expect(acked == 0, "the stale acknowledgement is not counted");
        // The real acknowledgement, stamped with this session, does confirm.
        Expect(gtos_host_worker_park(0), "the worker performs its own current acknowledgement");
        Expect(pool.ObservePark(requested, acked), "the current acknowledgement confirms");
    }

    Say("CASE 17\n");
    // ================================================================ 17. a holder cannot address another worker
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 2), "the seam installs a two-worker pool");
        CpuWorkTicket first, second;
        Expect(pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 3, 0), first) == CpuWorkAccepted,
               "the first worker accepts a job");
        Expect(pool.Submit(WorkerApic(1), WorkRequest(WorkModularSum, 5, 0), second) == CpuWorkAccepted,
               "the second worker accepts a job");
        // The raw tickets ARE equal: each worker owns its own queue and each queue numbers from one. This is the
        // documented queue-local contract, and it is exactly why the pool ticket carries a worker index.
        Expect(first.ticket.id == second.ticket.id && first.ticket.slot == second.ticket.slot,
               "two workers issue equal raw tickets, because a ticket is queue-local");
        Expect(first.workerIndex != second.workerIndex, "the pool ticket distinguishes the two by worker");

        gtos_host_worker_execute(0, WorkerApic(0));
        gtos_host_worker_execute(1, WorkerApic(1));
        WorkResult firstResult, secondResult;
        Expect(pool.Collect(first, firstResult) == CpuWorkComplete, "the first worker's result collects");
        Expect(pool.Collect(second, secondResult) == CpuWorkComplete, "the second worker's result collects");
        Expect(firstResult.executingApicId == WorkerApic(0),
               "the first result carries the first worker's identity");
        Expect(secondResult.executingApicId == WorkerApic(1),
               "the second result carries the second worker's identity");
        Expect(firstResult.value != secondResult.value,
               "the two results are different values, so neither was read from the other's slot");

        // A ticket claiming the wrong worker must be refused, because the pool routes by the worker it was issued on.
        CpuWorkTicket forged = first;
        forged.workerIndex = second.workerIndex;
        WorkResult stolen;
        Expect(pool.Collect(forged, stolen) == CpuWorkTicketInvalid,
               "a ticket claiming another worker's slot is refused");
        Expect(stolen.value == 0 && stolen.executingApicId == 0xFFFFFFFFU,
               "the refused collect wrote nothing into the caller's result");
    }

    Say("CASE 18\n");
    // ================================================================ 18. the queue accessor is a real handle
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 2), "the seam installs a two-worker pool");
        const CpuWorkQueue* first = pool.WorkerQueue(0);
        const CpuWorkQueue* second = pool.WorkerQueue(1);
        Expect(first != 0 && second != 0, "both workers expose their queue");
        Expect(first != second, "the two workers expose different queues");
        Expect(pool.WorkerQueue(2) == 0, "a worker index beyond the pool exposes nothing");
        Expect(pool.WorkerQueue(0xFFFFFFFFU) == 0, "an invalid worker index exposes nothing");
    }

    Say("CASE 19\n");
    // ================================================================ 19. reuse across the real capacity
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 1), "the seam installs the pool");
        // Fill the worker's whole queue, drain and collect all of it, then fill it again. The second pass must issue
        // different tickets into the same slots, which is what "the slot was really reused" means.
        const u32 capacity = CpuWorkQueue::Capacity;
        // The real tickets are kept, not rebuilt. A ticket now names its pool session, so a hand-made one would be
        // refused for the right reason and the case would be testing the refusal instead of the reuse.
        CpuWorkTicket first[16];
        u32 firstIds[16], firstSlots[16];
        for (u32 i = 0; i < capacity; ++i) {
            Expect(pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 2, i), first[i]) == CpuWorkAccepted,
                   "the first pass accepts every slot");
            firstIds[i] = first[i].ticket.id;
            firstSlots[i] = first[i].ticket.slot;
        }
        for (u32 i = 0; i < capacity; ++i) {
            gtos_host_worker_execute(0, WorkerApic(0));
            WorkResult r;
            Expect(pool.Collect(first[i], r) == CpuWorkComplete, "the first pass collects every slot");
            // The operation accumulates into the seed, so seed i with two iterations is i + 1 + 2.
            Expect(r.value == i + 1U + 2U, "the first pass value is the operation's own result");
        }
        // The second pass must reuse the same slots with different ids.
        u32 reused = 0, newIds = 0;
        for (u32 i = 0; i < capacity; ++i) {
            CpuWorkTicket t;
            Expect(pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 2, i), t) == CpuWorkAccepted,
                   "the second pass accepts every slot");
            for (u32 j = 0; j < capacity; ++j) if (t.ticket.slot == firstSlots[j]) ++reused;
            if (t.ticket.id != firstIds[i]) ++newIds;
        }
        Expect(reused == capacity, "the second pass reused every slot of the first");
        Expect(newIds == capacity, "every reused slot carries a new ticket id");
    }


    Say("CASE 20\n");
    // ================================================================ 20. a real Prepare and Start
    {
        // The production methods, on a machine that answers yes. This is what 113 never did: the shared block below is
        // built by Prepare(), not installed by the test.
        ResetMachine(2, 1);
        Say(" 20a machine\n");
        CpuWorkPool pool;
        Say(" 20b pool\n");
        Expect(RealPrepare(pool), "a real Prepare succeeds on a machine with one AP and a stack for it");
        Say(" 20c prepared\n");
        Expect(!pool.Started(), "Prepare does not claim the pool started");
        Expect(!pool.OwnerPublished(), "Prepare does not publish an owner");
        u32 ownedBase = 0, ownedPages = 0;
        Expect(pool.OwnedRange(ownedBase, ownedPages), "Prepare allocated a block the pool owns");
        Expect(ownedBase != 0 && ownedPages != 0, "the owned range is a real range");
        Expect(gtos_host_machine_allocation_calls >= 1, "Prepare really asked the allocator");
        {
            const bool started = RealStart(pool);
            if (!started) {
                static const char* names[] = {"Ok","BadState","WrongCpu","NoMemory","BadPaging","StartupFailed",
                    "ReadyTimeout","WakeFailed","BadMemoryTypes","WorkerSetupFailed","Closed"};
                const u32 code = (u32)pool.GetReport().error;
                Say("   start refused: ");
                Say(code < 11 ? names[code] : "unknown");
                Say("\n");
            }
            Expect(started, "a real Start succeeds when delivery works");
        }
        Say(" 20d started\n");
        Expect(pool.Started(), "a successful Start reports started");
        Expect(pool.OwnerPublished(), "a successful Start publishes the owner");
        Expect(gtos_host_machine_start_calls == 1, "Start called the startup protocol exactly once");
        CpuWorkPoolReport report = pool.GetReport();
        Expect(report.configuredWorkers == 1, "the production Prepare configured the one AP");
        Expect(report.readyWorkers == 1, "the production Start reports it ready");
        Expect(report.session != 0, "the production Prepare stamped a session");
    }

    Say("CASE 21\n");
    // ================================================================ 21. Prepare refuses without an AP stack
    {
        ResetMachine(2, 0); // two processors detected, neither AP has a stack
        CpuWorkPool pool;
        Expect(RealPrepare(pool), "Prepare succeeds even when no AP has a stack");
        CpuWorkPoolReport report = pool.GetReport();
        Expect(report.configuredWorkers == 0, "no AP with a stack means no configured worker");
        Expect(RealStart(pool), "Start succeeds with no workers, because there is nothing to deliver");
        Expect(pool.Started(), "a workerless pool still reports started");
    }

    Say("CASE 22\n");
    // ================================================================ 22. an allocation failure is a real failure
    {
        ResetMachine(2, 1);
        gtos_host_machine_allocation_fail = 1;
        CpuWorkPool pool;
        Expect(!RealPrepare(pool), "Prepare fails when the allocator cannot supply the block");
        Expect(!pool.Started(), "a failed Prepare leaves the pool unstarted");
        Expect(!pool.OwnerPublished(), "a failed Prepare publishes no owner");
        u32 base = 0, pages = 0;
        Expect(!pool.OwnedRange(base, pages), "a failed Prepare owns no range");
        CpuWorkTicket ticket;
        Expect(pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 2, 0), ticket) != CpuWorkAccepted,
               "a pool whose Prepare failed accepts nothing");
        // And it is not a permanent state: the same object can be prepared once the machine can supply the block.
        gtos_host_machine_allocation_fail = 0;
        Expect(RealPrepare(pool), "the same pool prepares once allocation succeeds");
    }

    Say("CASE 23\n");
    // ================================================================ 23. delivery failure keeps the owner alive
    {
        // The case the review said was never exercised. Start() used to publish "started" before calling StartPrepared,
        // so a delivery failure left a pool that claimed to have started. It must now report the failure while keeping
        // everything an AP might already be reading.
        ResetMachine(2, 1);
        gtos_host_machine_delivery = 0;
        CpuWorkPool pool;
        Expect(RealPrepare(pool), "Prepare succeeds before a delivery failure");
        u32 base = 0, pages = 0;
        Expect(pool.OwnedRange(base, pages), "the block exists before delivery is attempted");
        const u32 allocationCalls = gtos_host_machine_allocation_calls;
        const u32 usedPages = gtos_host_machine_arena_used;
        Expect(!RealStart(pool), "Start fails when delivery fails");
        Expect(!pool.Started(), "a failed delivery never claims the pool started");
        Expect(gtos_host_machine_start_calls == 1, "delivery was attempted exactly once");
        // The owner stays published and the block stays allocated: an AP may have entered before the failure.
        Expect(pool.OwnerPublished(), "the owner stays published after a failed delivery");
        u32 afterBase = 0, afterPages = 0;
        Expect(pool.OwnedRange(afterBase, afterPages), "the block is still owned after a failed delivery");
        Expect(afterBase == base && afterPages == pages, "the block is the same block, not a new one");
        Expect(gtos_host_machine_allocation_calls == allocationCalls,
               "nothing was allocated again, so nothing was reclaimed and reissued");
        Expect(gtos_host_machine_arena_used == usedPages,
               "the allocator's used count is unchanged, so the block was not freed");
        // Admission must be shut: a caller cannot use a pool whose APs never arrived.
        CpuWorkTicket ticket;
        Expect(pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 2, 0), ticket) == CpuWorkOffline,
               "a pool whose delivery failed refuses a submission as offline");
        Expect(!ticket.ticket.IsValid(), "the refusal left an invalid ticket");
    }

    Say("CASE 24\n");
    // ================================================================ 24. repeat calls are refused, not repeated
    {
        ResetMachine(2, 1);
        CpuWorkPool pool;
        Expect(RealPrepare(pool), "the first Prepare succeeds");
        const u32 calls = gtos_host_machine_allocation_calls;
        Expect(!RealPrepare(pool), "a second Prepare on the same pool is refused");
        Expect(gtos_host_machine_allocation_calls == calls,
               "the refused Prepare did not allocate a second block");
        Expect(RealStart(pool), "the first Start succeeds");
        Expect(!RealStart(pool), "a second Start is refused");
        Expect(gtos_host_machine_start_calls == 1, "the refused Start did not deliver APs again");
        Expect(pool.Started(), "the refused repeat left the pool started");
    }

    Say("CASE 25\n");
    // ================================================================ 25. a partial AP set is truthful
    {
        // Two APs detected, one of them failed its self test. Start must report the failure rather than a clean start,
        // and the healthy worker must still be usable.
        ResetMachine(3, 2);
        gtos_host_machine_processor_state[2] = CpuStartupSelfTestFailed;
        CpuWorkPool pool;
        Expect(RealPrepare(pool), "Prepare succeeds with a failing AP present");
        const bool started = RealStart(pool);
        CpuWorkPoolReport report = pool.GetReport();
        Expect(report.configuredWorkers == 2, "both APs with stacks were configured");
        Expect(report.faultedWorkers == 1, "the failing AP is reported faulted");
        Expect(!started, "Start reports failure when an AP failed its self test");
        Expect(!pool.Started(), "a partial AP set does not claim a clean start");
        // The healthy worker is still real: its queue is addressable and its state is not faulted.
        const CpuWorkQueue* healthy = pool.WorkerQueue(0);
        Expect(healthy != 0, "the healthy worker's queue is addressable");
    }

    Say("CASE 26\n");
    // ================================================================ 26. a live victim is not consumed by a wrong route
    {
        // The review's second point: the old case collected both results first, so the target slot was empty and any
        // routing would have refused. Here the victim is still live when the mis-routed attempt is made.
        CpuWorkPool pool;
        Expect(FreshPool(pool, 2), "the seam installs a two-worker pool");
        CpuWorkTicket first, second;
        Expect(pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 3, 0), first) == CpuWorkAccepted,
               "worker 0 accepts a job");
        Expect(pool.Submit(WorkerApic(1), WorkRequest(WorkModularSum, 5, 0), second) == CpuWorkAccepted,
               "worker 1 accepts a job");
        // Execute both so each slot holds a completed, uncollected result: a live obligation, not an empty slot.
        gtos_host_worker_execute(0, WorkerApic(0));
        gtos_host_worker_execute(1, WorkerApic(1));

        // The victim is worker 1's result. Route worker 0's ticket at worker 1 and demand the exact refusal reason.
        CpuWorkTicket misrouted = first;
        misrouted.workerIndex = second.workerIndex;
        WorkResult stolen;
        stolen.value = 0xDEADBEEFU;
        stolen.executingApicId = 0xDEADBEEFU;
        Expect(pool.Collect(misrouted, stolen) == CpuWorkTicketInvalid,
               "a ticket naming another worker's slot is refused");
        Expect(stolen.value == 0xDEADBEEFU && stolen.executingApicId == 0xDEADBEEFU,
               "the refused collect wrote nothing into the caller's result");

        // And the victim is untouched: it still collects normally with its own ticket.
        WorkResult secondResult;
        Expect(pool.Collect(second, secondResult) == CpuWorkComplete,
               "the victim still collects after the mis-routed attempt");
        Expect(secondResult.executingApicId == WorkerApic(1),
               "the victim's result belongs to worker 1");
        Expect(secondResult.value != 0xDEADBEEFU, "the victim's value was not overwritten");
        // Worker 0's own obligation is also intact.
        WorkResult firstResult;
        Expect(pool.Collect(first, firstResult) == CpuWorkComplete,
               "the mis-routed ticket still collects its own job");
    }

    Say("CASE 27\n");
    // ================================================================ 27. a stale session cannot consume a new job
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 1), "the seam installs a one-worker pool");
        const u32 session = pool.Session();
        Expect(session != 0, "the pool has a session");
        CpuWorkTicket live;
        Expect(pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 2, 7), live) == CpuWorkAccepted,
               "the pool accepts a job");
        Expect(live.session == session, "the issued ticket carries the pool's session");
        gtos_host_worker_execute(0, WorkerApic(0));

        // A ticket from an earlier pool: same worker index, different session.
        CpuWorkTicket stale = live;
        stale.session = session + 1;
        WorkResult result;
        Expect(pool.Collect(stale, result) == CpuWorkTicketInvalid,
               "a ticket from another session is refused");
        // The real ticket still collects, so the refusal did not consume anything.
        Expect(pool.Collect(live, result) == CpuWorkComplete,
               "the current ticket still collects its job");
    }

    Say("CASE 28\n");
    // ================================================================ 28. close from the real running state
    {
        // The review's third point: the old case used a setter to write Busy. This one reaches Running through the
        // production step and pauses at its interior boundary, so the interleaving is real.
        CpuWorkPool pool;
        Expect(FreshPool(pool, 1), "the seam installs a one-worker pool");
        CpuWorkTicket ticket;
        Expect(pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 6, 11), ticket) == CpuWorkAccepted,
               "the pool accepts a job");
        gtos_host_step_gate(1);
        // The step publishes Running and stops before touching the job.
        Expect(gtos_host_worker_step(0), "the real step paused at its interior boundary");
        Expect(gtos_host_worker_state(0) == CpuWorkerBusy,
               "the production step published the running state");
        Expect(gtos_host_step_reached() == 1, "the step is paused at its interior boundary");

        // From this real running state, close admission and prove the pool does not claim to be drained.
        Expect(pool.CloseAdmission(), "admission closes while a worker is genuinely running");
        u32 queued = 0, busy = 0, uncollected = 0;
        Expect(!pool.IsDrained(queued, busy, uncollected),
               "a pool with a running worker does not report drained");
        Expect(busy == 1, "the running worker is counted");
        // Parking is refused for the same reason: the obligation is still live.
        Expect(!pool.ParkWorkers(), "parking is refused while a worker is genuinely running");
        Expect(gtos_host_park_requested(0) == 0, "the refused park did not even request");
        // And a new submission is refused because admission closed, not because the worker was busy.
        CpuWorkTicket afterClose;
        Expect(pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 2, 0), afterClose) == CpuWorkOffline,
               "a submission after the close is refused as offline");

        // Release the step. The job completes, and the obligation that was already live is still collectable.
        gtos_host_step_gate(0);
        Expect(gtos_host_worker_resume(0), "the paused step resumes");
        Expect(gtos_host_worker_state(0) == CpuWorkerIdle,
               "the released step returned the worker to idle");
        WorkResult result;
        Expect(pool.Collect(ticket, result) == CpuWorkComplete,
               "the job that was running when admission closed still collects");
        Expect(pool.IsDrained(queued, busy, uncollected), "the pool drains once the job is collected");
    }

    Say("CASE 29\n");
    // ================================================================ 29. park from the real running state
    {
        CpuWorkPool pool;
        Expect(FreshPool(pool, 1), "the seam installs a one-worker pool");
        CpuWorkTicket ticket;
        Expect(pool.Submit(WorkerApic(0), WorkRequest(WorkModularSum, 4, 3), ticket) == CpuWorkAccepted,
               "the pool accepts a job");
        gtos_host_step_gate(1);
        Expect(gtos_host_worker_step(0), "the real step paused at its interior boundary");
        Expect(gtos_host_worker_state(0) == CpuWorkerBusy, "the worker is genuinely running");
        // A park request while the job is mid-flight must not produce an acknowledgement.
        Expect(pool.RequestPark(), "the park request is accepted");
        u32 requested = 0, acked = 0;
        Expect(!pool.ObservePark(requested, acked), "no acknowledgement exists while the job is mid-flight");
        Expect(acked == 0, "the ack count is zero");
        // The AP-side acknowledgement is refused too, because a job is still owed.
        Expect(!gtos_host_worker_park(0), "the worker refuses to acknowledge while it owes a job");
        // Release and collect; then the acknowledgement is possible.
        gtos_host_step_gate(0);
        Expect(gtos_host_worker_resume(0), "the paused step resumes");
        WorkResult result;
        Expect(pool.Collect(ticket, result) == CpuWorkComplete, "the job collects after release");
        Expect(gtos_host_worker_park(0), "the worker acknowledges once nothing is owed");
        Expect(pool.ObservePark(requested, acked), "the acknowledgement confirms the park");
        Expect(acked == 1, "exactly one worker acknowledged");
    }

    Say(failures == 0 ? "POOL UNIT PASS\n" : "POOL UNIT FAIL\n");
    return (int)failures;
}
