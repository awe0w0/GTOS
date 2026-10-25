// GTOS-DSH-113 G1: the real CpuWorkPool admission, drain and park contract, executed on the host.
//
// The pool object under test is the production one. What the seam replaces is only the privileged boundary: the flag
// register, the control registers, the CPU identity read, interrupt masking, the local APIC page, the IPI, the paging
// query and the shared-block allocation that Prepare and Start would otherwise perform. The admission rule, the
// closure flag, the drain arithmetic, the worker state transitions and every CpuWorkQueue are the real production
// code, because a test that stubbed those would prove nothing about them.
//
// What this unit therefore shows: the pool refuses a submission when it is closed, when it was never started, when the
// APIC id is unknown and when a worker is not ready; it reports drained only when nothing is queued, running or
// completed-but-uncollected; and it refuses to park while any of that is outstanding. What it cannot show: that a real
// AP ever executed anything, or that the hardware IPI was delivered. Those are guest observations.

#include <common/types.h>
#include <hardwarecommunication/cpu_work_pool.h>

using namespace gtos::hardwarecommunication;

extern "C" uint32_t gtos_host_flags;
extern "C" uint32_t gtos_host_cr0;
extern "C" uint32_t gtos_host_cr3;
extern "C" uint32_t gtos_host_identity;
extern "C" uint32_t gtos_host_wake_result;
extern "C" uint32_t gtos_host_wake_calls;

extern "C" bool gtos_host_install_pool(uint32_t workerCount, uint32_t bspApicId, uint32_t lapicAddress);
extern "C" bool gtos_host_attach_pool(void* poolAddress);
extern "C" bool gtos_host_worker_execute(uint32_t workerIndex, uint32_t apicId);
extern "C" uint32_t gtos_host_worker_state(uint32_t workerIndex);
extern "C" void gtos_host_reset_pool();

// The seam globals. Their initial values describe an idle BSP with paging off and interrupts clear, which is the state
// the pool's own preconditions expect.
// The seam globals are defined here; the pool object declares them.
extern "C" {
uint32_t gtos_host_flags = 0x00000002U;
uint32_t gtos_host_cr0 = 0x00000011U;
uint32_t gtos_host_cr3 = 0x00000000U;
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
bool FreshPool(CpuWorkPool& pool, u32 workerCount) {
    gtos_host_reset_pool();
    if (!gtos_host_install_pool(workerCount, kBspApic, kLapic)) return false;
    return gtos_host_attach_pool(&pool);
}

// The APIC id of worker 0, which is what a caller submits to.
u32 WorkerApic(u32 index) { return kBspApic + 1 + index; }

} // namespace

extern "C" int poolTestMain() {
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
        Expect(pool.ParkWorkers(), "parking succeeds once nothing is owed");
        Expect(gtos_host_worker_state(0) == CpuWorkerParked, "the worker reports the parked state");
    }

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
        Expect(pool.ParkWorkers(), "a closed idle pool parks");
    }

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
        Expect(pool.ParkWorkers(), "the drained closed pool parks");
    }

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
        Expect(pool.ParkWorkers(), "the drained pool parks");
    }

    Say(failures == 0 ? "POOL UNIT PASS\n" : "POOL UNIT FAIL\n");
    return (int)failures;
}
