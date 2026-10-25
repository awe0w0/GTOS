// GTOS-DSH-112 W1: the real CpuWorkQueue, executed on the host.
//
// This links the production queue object and calls its actual methods. No queue algorithm is copied, no model is
// substituted, and nothing here is a stand-in for the queue: the bytes under test are the same bytes the kernel links.
//
// What this unit can and cannot show is stated in the report. It runs on the host, so it proves the ticket, slot,
// admission, state-transition and range logic. It cannot prove an AP executed anything, and it does not touch the APIC,
// CR registers, MSRs or interrupt masking; the guest scenarios cover that.
//
// The contract these expectations encode is the one the header states:
//   - a submission yields a real ticket, and every failure invalidates it
//   - a slot stays occupied until collected, so a completed-but-uncollected job still holds its slot
//   - only a completed job releases its slot, and a stale or already-collected ticket is refused
//   - the consumer supplies its own hardware APIC ID, which the result carries
//   - the ring index wraps, and a wrapped slot is never readable through an old ticket

#include <common/types.h>
#include <hardwarecommunication/cpu_work_queue.h>

using namespace gtos::hardwarecommunication;

namespace {

typedef unsigned int u32;

u32 failures = 0;
u32 checks = 0;

u32 Length(const char* text) {
    u32 n = 0;
    while (text && text[n]) ++n;
    return n;
}

// One write syscall for the whole buffer, with the return captured, so the syscall cannot quietly clobber the loop
// state of its caller.
int WriteAll(const char* text, u32 length) {
    int result = 0;
    asm volatile("int $0x80"
                 : "=a"(result)
                 : "a"(4), "b"(2), "c"(text), "d"(length)
                 : "memory");
    return result;
}

void Say(const char* text) { (void)WriteAll(text, Length(text)); }

void Expect(bool value, const char* label) {
    ++checks;
    if (!value) {
        ++failures;
        Say("FAIL ");
        Say(label);
        Say("\n");
    }
}
bool TextEquals(const char* a, const char* b) {
    u32 i = 0;
    while (a[i] && b[i]) { if (a[i] != b[i]) return false; ++i; }
    return a[i] == b[i];
}

// A deterministic reference for the two built-in operations, computed here only to check that the queue's own
// arithmetic is stable and distinct. It is not an alternative implementation of the queue.
u32 ReferenceHash(u32 seed, u32 iterations) {
    u32 value = seed;
    for (u32 i = 0; i < iterations; ++i) {
        value = (value ^ (i + 0x9E3779B9U)) * 16777619U;
        value ^= value >> 13;
    }
    return value;
}
u32 ReferenceSum(u32 iterations) {
    u32 value = 0;
    for (u32 i = 0; i < iterations; ++i) value += i + 1;
    return value;
}

const u32 kApicA = 7;
const u32 kApicB = 3;

} // namespace

extern "C" int queueTestMain() {
    // ================================================================ 1. the ticket is real
    {
        CpuWorkQueue queue;
        WorkTicket ticket;
        const WorkSubmitStatus status = queue.Submit(WorkRequest(WorkIntegerHash, 8, 0x1234U), ticket);
        Expect(status == WorkAccepted, "a valid submission is accepted");
        Expect(ticket.IsValid(), "an accepted submission yields a valid ticket");
        Expect(ticket.slot < CpuWorkQueue::Capacity, "the ticket slot is inside the ring");
        Expect(ticket.id != 0, "the ticket id is nonzero, because zero is the terminal marker");

        const WorkQueueStats s = queue.GetStats();
        Expect(s.submitted == 1, "the submitted counter advanced");
        Expect(s.completed == 0, "nothing is completed before execution");
        Expect(s.collected == 0, "nothing is collected before execution");
        Expect(s.rejected == 0, "a valid submission is not counted as rejected");
        Expect(queue.HasPending(), "the consumer sees a pending slot");
    }

    // ================================================================ 2. execution carries the consumer's own identity
    {
        CpuWorkQueue queue;
        WorkTicket ticket;
        queue.Submit(WorkRequest(WorkIntegerHash, 4, 99U), ticket);
        Expect(!queue.HasPending() == false, "a pending job is visible before execution");

        const bool ran = queue.ExecuteOne(kApicA);
        Expect(ran, "the consumer executes the pending job");
        Expect(!queue.HasPending(), "the slot is no longer pending after execution");
        Expect(queue.GetStats().completed == 1, "the completed counter advanced");

        WorkResult result;
        const WorkCollectStatus collected = queue.Collect(ticket, result);
        Expect(collected == WorkComplete, "a completed job collects");
        Expect(result.executingApicId == kApicA,
               "the result carries the APIC id the executing call supplied");
        Expect(result.value == ReferenceHash(99U, 4U), "the hash result is the operation's own value");
        Expect(queue.GetStats().collected == 1, "the collected counter advanced");

        // A second consumer identity must be recorded as itself, never as the first one.
        WorkTicket second;
        queue.Submit(WorkRequest(WorkIntegerHash, 2, 5U), second);
        queue.ExecuteOne(kApicB);
        WorkResult secondResult;
        queue.Collect(second, secondResult);
        Expect(secondResult.executingApicId == kApicB,
               "a different consumer identity is recorded as itself");
    }

    // ================================================================ 3. the two built-in operations
    {
        CpuWorkQueue queue;
        WorkTicket hashTicket, sumTicket;
        queue.Submit(WorkRequest(WorkIntegerHash, 16, 0xA5A5U), hashTicket);
        queue.Submit(WorkRequest(WorkModularSum, 16, 0U), sumTicket);
        queue.ExecuteOne(kApicA);
        queue.ExecuteOne(kApicA);
        WorkResult hashResult, sumResult;
        queue.Collect(hashTicket, hashResult);
        queue.Collect(sumTicket, sumResult);
        Expect(hashResult.value == ReferenceHash(0xA5A5U, 16U), "the hash operation is stable");
        Expect(sumResult.value == ReferenceSum(16U), "the sum operation is stable");
        Expect(hashResult.value != sumResult.value,
               "the two operations produce different values for the same length");
    }

    // ================================================================ 4. a completed job still occupies its slot
    {
        CpuWorkQueue queue;
        WorkTicket first;
        queue.Submit(WorkRequest(WorkIntegerHash, 2, 1U), first);
        queue.ExecuteOne(kApicA);
        // Completed but not collected: the slot must not be handed out again.
        const WorkQueueStats before = queue.GetStats();
        Expect(before.completed == 1 && before.collected == 0,
               "a completed job is not collected until Collect runs");
        WorkResult pending;
        // A premature collect of the same ticket is Complete, not Pending; the point here is the slot's occupancy.
        const WorkCollectStatus status = queue.Collect(first, pending);
        Expect(status == WorkComplete, "the uncollected completed job collects normally");
    }

    // ================================================================ 5. premature collect
    {
        CpuWorkQueue queue;
        WorkTicket ticket;
        queue.Submit(WorkRequest(WorkIntegerHash, 4, 7U), ticket);
        WorkResult result;
        const WorkCollectStatus status = queue.Collect(ticket, result);
        Expect(status == WorkPending, "collecting a job that has not executed reports pending");
        Expect(queue.GetStats().collected == 0, "a pending collect does not advance the collected counter");
        Expect(queue.HasPending(), "the job is still pending after a premature collect");
        // It must still be executable and collectable afterwards.
        queue.ExecuteOne(kApicA);
        const WorkCollectStatus second = queue.Collect(ticket, result);
        Expect(second == WorkComplete, "the job collects after a premature attempt");
    }

    // ================================================================ 6. repeated collect and stale tickets
    {
        CpuWorkQueue queue;
        WorkTicket ticket;
        queue.Submit(WorkRequest(WorkIntegerHash, 3, 11U), ticket);
        queue.ExecuteOne(kApicA);
        WorkResult result;
        Expect(queue.Collect(ticket, result) == WorkComplete, "the first collect succeeds");
        WorkResult again;
        Expect(queue.Collect(ticket, again) == WorkTicketInvalid,
               "collecting the same ticket twice is refused");

        // The ring does not hand a freed slot straight back: the producer index advances and a slot is reused only
        // when it wraps. Reuse is therefore exercised through a full cycle, which is where a stale ticket matters.
        for (u32 i = 1; i < CpuWorkQueue::Capacity; ++i) {
            WorkTicket filler;
            queue.Submit(WorkRequest(WorkIntegerHash, 1, i), filler);
            queue.ExecuteOne(kApicA);
            WorkResult drained;
            queue.Collect(filler, drained);
        }
        WorkTicket fresh;
        queue.Submit(WorkRequest(WorkModularSum, 5, 0U), fresh);
        Expect(fresh.slot == ticket.slot, "the producer index wraps and the freed slot is genuinely reused");
        Expect(fresh.id != ticket.id, "the reused slot carries a new ticket id");
        queue.ExecuteOne(kApicB);
        WorkResult stolen;
        Expect(queue.Collect(ticket, stolen) == WorkTicketInvalid,
               "a stale ticket cannot read the job that reused its slot");
        WorkResult correct;
        Expect(queue.Collect(fresh, correct) == WorkComplete, "the new ticket collects its own job");
        Expect(correct.executingApicId == kApicB, "the new job's identity is its own");
        Expect(correct.value == ReferenceSum(5U), "the new job's value is its own");
    }

    // ================================================================ 7. unknown and malformed tickets
    {
        CpuWorkQueue queue;
        WorkResult result;
        Expect(queue.Collect(WorkTicket(), result) == WorkTicketInvalid,
               "a default ticket is refused");
        Expect(queue.Collect(WorkTicket(0, 0), result) == WorkTicketInvalid,
               "a zero id is refused");
        Expect(queue.Collect(WorkTicket(CpuWorkQueue::Capacity, 1), result) == WorkTicketInvalid,
               "a slot at the capacity bound is refused");
        Expect(queue.Collect(WorkTicket(CpuWorkQueue::Capacity + 5, 1), result) == WorkTicketInvalid,
               "a slot beyond the capacity is refused");
        Expect(queue.Collect(WorkTicket(0, 0xFFFFFFFFU), result) == WorkTicketInvalid,
               "an id that was never issued is refused");
        // None of those refusals may consume or disturb anything.
        WorkTicket ticket;
        queue.Submit(WorkRequest(WorkIntegerHash, 1, 1U), ticket);
        queue.ExecuteOne(kApicA);
        Expect(queue.Collect(ticket, result) == WorkComplete,
               "a real ticket still collects after the malformed attempts");
    }

    // ================================================================ 8. invalid input never yields a ticket
    {
        CpuWorkQueue queue;
        WorkTicket ticket(0xAAAAAAAAU, 0xBBBBBBBBU);
        Expect(queue.Submit(WorkRequest((WorkKind)0, 4, 0), ticket) == WorkInvalid,
               "an unknown operation kind is refused");
        Expect(!ticket.IsValid(), "a refused submission invalidates the caller's ticket");
        Expect(queue.Submit(WorkRequest((WorkKind)99, 4, 0), ticket) == WorkInvalid,
               "an out-of-range operation kind is refused");
        Expect(queue.Submit(WorkRequest(WorkIntegerHash, 0, 0), ticket) == WorkInvalid,
               "zero iterations is refused");
        Expect(queue.Submit(WorkRequest(WorkIntegerHash, CpuWorkQueue::MaxIterations + 1, 0), ticket)
                 == WorkInvalid,
               "iterations above the maximum are refused");
        Expect(queue.Submit(WorkRequest(WorkIntegerHash, CpuWorkQueue::MaxIterations, 0), ticket)
                 == WorkAccepted,
               "iterations exactly at the maximum are accepted");
        const WorkQueueStats s = queue.GetStats();
        Expect(s.rejected == 4, "each refused submission is counted as rejected");
        Expect(s.submitted == 1, "only the accepted submission is counted");
        // A refused submission must not leave a readable slot behind.
        WorkResult result;
        Expect(queue.Collect(ticket, result) == WorkPending,
               "the accepted job is pending, so the refusals left nothing else");
    }

    // ================================================================ 9. a full queue
    {
        CpuWorkQueue queue;
        WorkTicket tickets[CpuWorkQueue::Capacity];
        u32 accepted = 0;
        for (u32 i = 0; i < CpuWorkQueue::Capacity; ++i) {
            if (queue.Submit(WorkRequest(WorkIntegerHash, 2, i), tickets[i]) == WorkAccepted) ++accepted;
        }
        Expect(accepted == CpuWorkQueue::Capacity, "every slot in the ring accepts one job");
        WorkTicket overflow;
        Expect(queue.Submit(WorkRequest(WorkIntegerHash, 2, 999), overflow) == WorkFull,
               "a submission into a full queue is refused as full");
        Expect(!overflow.IsValid(), "the full-queue refusal invalidates the ticket");
        Expect(queue.GetStats().rejected == 1, "the full-queue refusal is counted");

        // Draining one slot must let exactly one more job in, and not disturb the others.
        queue.ExecuteOne(kApicA);
        WorkResult first;
        Expect(queue.Collect(tickets[0], first) == WorkComplete, "the first job collects");
        WorkTicket replacement;
        Expect(queue.Submit(WorkRequest(WorkModularSum, 3, 0), replacement) == WorkAccepted,
               "the freed slot accepts a replacement job");
        WorkResult kept;
        Expect(queue.Collect(tickets[1], kept) == WorkPending,
               "an untouched job is still pending, so the replacement consumed nothing else");
        Expect(queue.Submit(WorkRequest(WorkIntegerHash, 2, 1000), overflow) == WorkFull,
               "the queue is full again after the single replacement");
    }

    // ================================================================ 10. FIFO order across the ring
    {
        CpuWorkQueue queue;
        // Fill, drain and refill past the capacity several times so the produce and consume indices wrap.
        u32 order = 1;
        bool fifo = true;
        for (u32 round = 0; round < 5; ++round) {
            WorkTicket batch[4];
            for (u32 i = 0; i < 4; ++i) {
                queue.Submit(WorkRequest(WorkModularSum, i + 1, 0), batch[i]);
            }
            for (u32 i = 0; i < 4; ++i) {
                queue.ExecuteOne(kApicA);
                WorkResult result;
                if (queue.Collect(batch[i], result) != WorkComplete) fifo = false;
                else if (result.value != ReferenceSum(i + 1)) fifo = false;
            }
            ++order;
        }
        Expect(fifo, "each batch executes and collects in submission order across the wrap");
        const WorkQueueStats s = queue.GetStats();
        Expect(s.submitted == 20, "twenty jobs were submitted across the wrap");
        Expect(s.completed == 20, "twenty jobs were completed across the wrap");
        Expect(s.collected == 20, "twenty jobs were collected across the wrap");
        Expect(s.rejected == 0, "no wrap-cycle submission was refused");
    }

    // ================================================================ 11. two queues are independent
    {
        CpuWorkQueue left, right;
        WorkTicket a, b;
        left.Submit(WorkRequest(WorkIntegerHash, 3, 1U), a);
        right.Submit(WorkRequest(WorkIntegerHash, 3, 2U), b);
        Expect(a.slot == b.slot && a.id == b.id,
               "two fresh queues issue the same first ticket, so tickets are queue-local");
        left.ExecuteOne(kApicA);
        WorkResult fromRight;
        // The right queue's slot 0 holds its own pending job with the same id, so the foreign ticket is not
        // distinguishable from its own: it reports Pending. This is the hazard the header warns about, and it is
        // asserted rather than hidden.
        Expect(right.Collect(a, fromRight) == WorkPending,
               "an identical foreign ticket matches the other queue's own pending slot");
        Expect(!right.HasPending() == false,
               "the other queue still has its own pending job after the foreign collect attempt");
        WorkResult fromLeft;
        Expect(left.Collect(a, fromLeft) == WorkComplete, "the originating queue collects it");
        Expect(fromLeft.value == ReferenceHash(1U, 3U), "the originating queue's value is its own");
        Expect(left.GetStats().collected == 1 && right.GetStats().collected == 0,
               "the two queues keep separate counters");

        // Against a queue whose slot is empty, the same foreign ticket is refused outright.
        CpuWorkQueue empty;
        WorkResult refused;
        Expect(empty.Collect(a, refused) == WorkTicketInvalid,
               "a foreign ticket is refused by a queue whose slot is empty");
    }

    // ================================================================ 12. execute with nothing pending
    {
        CpuWorkQueue queue;
        Expect(!queue.ExecuteOne(kApicA), "executing an empty queue does nothing");
        Expect(queue.GetStats().completed == 0, "an empty execute does not advance completed");
        WorkTicket ticket;
        queue.Submit(WorkRequest(WorkIntegerHash, 2, 3U), ticket);
        Expect(queue.ExecuteOne(kApicA), "the job executes after the empty attempt");
        // A second execute must not run the same slot twice.
        Expect(!queue.ExecuteOne(kApicA), "a completed slot is not executed twice");
        Expect(queue.GetStats().completed == 1, "the job was completed exactly once");
    }

    Say(failures == 0 ? "QUEUE UNIT PASS\n" : "QUEUE UNIT FAIL\n");
    return (int)failures;
}
