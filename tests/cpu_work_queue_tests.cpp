// Freestanding i386 Linux tests of the real queue; no libc or kernel mocks.
#define private public
#include <hardwarecommunication/cpu_work_queue.h>
#undef private
#include <hardwarecommunication/cpu.h>

using namespace gtos::hardwarecommunication;

void* operator new(size_t, void* address) { return address; }

namespace {
    int failures = 0;
    uint32_t checks = 0;
    int32_t Syscall3(uint32_t number, uint32_t arg1 = 0, uint32_t arg2 = 0, uint32_t arg3 = 0) {
        int32_t result;
        asm volatile("int $0x80" : "=a"(result) : "0"(number), "b"(arg1), "c"(arg2), "d"(arg3)
                     : "memory", "cc");
        return result;
    }
    void Print(const char* text) {
        uint32_t length = 0;
        while (text[length]) ++length;
        Syscall3(4, 1, (uint32_t)text, length);
    }
    void Check(bool passed, const char* name) {
        ++checks;
        if (!passed) { ++failures; Print("FAIL: "); Print(name); Print("\n"); }
    }
    uint32_t Reference(const WorkRequest& request) {
        if (request.kind == WorkModularSum) {
            const uint32_t n = request.iterations;
            return request.seed + ((n & 1) ? n * ((n + 1) / 2) : (n / 2) * (n + 1));
        }
        // Independent 64-bit multiply/truncation and quotient representation.
        uint64_t value = request.seed;
        for (uint32_t i = 0; i < request.iterations; ++i) {
            value = ((value ^ (i + 0x9E3779B9U)) * 16777619ULL) & 0xFFFFFFFFULL;
            value ^= value / 8192;
        }
        return (uint32_t)value;
    }
    void TestBasicAndInvalid() {
        CpuWorkQueue queue;
        WorkTicket ticket;
        WorkResult result(0x1234, 0x5678);
        WorkQueueStats stats = queue.GetStats();
        Check(!ticket.IsValid() && !WorkTicket(16, 1).IsValid()
              && !WorkTicket(0, 0).IsValid() && WorkTicket(15, 1).IsValid(), "ticket bounds");
        Check(!queue.HasPending() && !queue.ExecuteOne(7), "empty queue is idle");
        Check(stats.submitted == 0 && stats.completed == 0 && stats.collected == 0
              && stats.rejected == 0, "fresh statistics");
        Check(queue.Collect(ticket, result) == WorkTicketInvalid
              && result.value == 0x1234 && result.executingApicId == 0x5678,
              "invalid collection preserves output");
        const WorkRequest invalid[] = {
            WorkRequest(), WorkRequest(WorkIntegerHash, 65537, 0),
            WorkRequest(WorkModularSum, 0xFFFFFFFFU, 0),
            WorkRequest((WorkKind)0, 1, 0), WorkRequest((WorkKind)3, 1, 0),
            WorkRequest((WorkKind)0xFFFFFFFFU, 1, 0)
        };
        for (uint32_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
            ticket = WorkTicket(5, 12);
            Check(queue.Submit(invalid[i], ticket) == WorkInvalid && !ticket.IsValid(),
                  "invalid kind or iterations invalidates submission ticket");
        }
        stats = queue.GetStats();
        Check(stats.rejected == 6 && stats.submitted == 0 && !queue.HasPending(),
              "invalid requests have no queue side effects");
        Check(queue.Submit(WorkRequest(WorkModularSum, 1, 0xFFFFFFFFU), ticket) == WorkAccepted
              && ticket.slot == 0 && ticket.id == 1, "first accepted sequence");
        Check(queue.HasPending() && queue.Collect(ticket, result) == WorkPending
              && result.value == 0x1234 && result.executingApicId == 0x5678,
              "pending collection preserves output");
        Check(queue.Collect(WorkTicket(0, 2), result) == WorkTicketInvalid,
              "wrong generation rejected while pending");
        Check(queue.ExecuteOne(0xFEDCBA98U) && !queue.HasPending() && !queue.ExecuteOne(1),
              "execute once and return to idle");
        Check(queue.Collect(ticket, result) == WorkComplete && result.value == 0
              && result.executingApicId == 0xFEDCBA98U, "modular overflow and full-width APIC identity");
        result = WorkResult(99, 100);
        Check(queue.Collect(ticket, result) == WorkTicketInvalid && result.value == 99
              && result.executingApicId == 100, "collected ticket cannot collect twice");
        stats = queue.GetStats();
        Check(stats.submitted == 1 && stats.completed == 1 && stats.collected == 1
              && stats.rejected == 6, "collections do not count as submission rejection");
    }

    void TestFifoCapacityAndReuse() {
        CpuWorkQueue queue;
        WorkTicket tickets[CpuWorkQueue::Capacity];
        WorkResult result;
        for (uint32_t i = 0; i < CpuWorkQueue::Capacity; ++i)
            Check(queue.Submit(WorkRequest(WorkModularSum, i + 1, i * 100), tickets[i]) == WorkAccepted
                  && tickets[i].slot == i && tickets[i].id == i + 1, "fill queue with distinct tickets");
        WorkTicket extra(1, 1);
        Check(queue.Submit(WorkRequest(WorkIntegerHash, 1, 1), extra) == WorkFull
              && !extra.IsValid(), "seventeenth pending job rejected as full");
        for (uint32_t i = 0; i < CpuWorkQueue::Capacity; ++i) {
            Check(queue.HasPending() && queue.ExecuteOne(0x100 + i), "FIFO execution available");
            if (i + 1 < CpuWorkQueue::Capacity)
                Check(queue.Collect(tickets[i + 1], result) == WorkPending,
                      "later FIFO job remains pending");
        }
        Check(!queue.HasPending() && !queue.ExecuteOne(0), "no repeat execution of completed slots");
        Check(queue.Submit(WorkRequest(WorkModularSum, 1, 1), extra) == WorkFull,
              "completion alone does not release slot");
        Check(queue.Collect(tickets[15], result) == WorkComplete && result.value == 1636
              && result.executingApicId == 0x10F, "out-of-order collection has its own result identity");
        Check(queue.Submit(WorkRequest(WorkIntegerHash, 1, 1), extra) == WorkFull,
              "free later slot conservatively backpressures at ring head");
        const WorkTicket copied = tickets[0];
        Check(queue.Collect(copied, result) == WorkComplete && result.value == 1
              && result.executingApicId == 0x100, "a copied live ticket can collect exactly once");
        Check(queue.Submit(WorkRequest(WorkIntegerHash, 16, 0x12345678), extra) == WorkAccepted
              && extra.slot == copied.slot && extra.id == 17, "collected head is reused with fresh ID");
        result = WorkResult(101, 102);
        Check(queue.Collect(copied, result) == WorkTicketInvalid && result.value == 101,
              "stale ticket rejected after its slot is reused");
        Check(queue.ExecuteOne(77) && queue.Collect(extra, result) == WorkComplete
              && result.value == 0x279BE802U && result.executingApicId == 77,
              "known integer hash vector and reused slot identity");
        for (uint32_t i = 14; i > 0; --i)
            Check(queue.Collect(tickets[i], result) == WorkComplete
                  && result.value == i * 100 + (i + 1) * (i + 2) / 2
                  && result.executingApicId == 0x100 + i, "reverse collection preserves all FIFO results");
        WorkQueueStats stats = queue.GetStats();
        Check(stats.submitted == 17 && stats.completed == 17 && stats.collected == 17
              && stats.rejected == 3, "full and reuse statistics");
    }

    void TestBoundsAndExhaustion() {
        CpuWorkQueue queue;
        WorkTicket ticket;
        WorkResult result;
        const WorkRequest vectors[] = {
            WorkRequest(WorkIntegerHash, 1, 0x12345678),
            WorkRequest(WorkIntegerHash, 2, 0x12345678),
            WorkRequest(WorkIntegerHash, 65536, 0),
            WorkRequest(WorkIntegerHash, 65536, 0xFFFFFFFFU),
            WorkRequest(WorkModularSum, 65536, 0xFFFFFFFFU)
        };
        const uint32_t expected[] = {0x2A057CF2U, 0xF2B9C5AAU, 0x55C51B26U, 0xB827702CU, 0x80007FFFU};
        for (uint32_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i) {
            Check(queue.Submit(vectors[i], ticket) == WorkAccepted && queue.ExecuteOne(i)
                  && queue.Collect(ticket, result) == WorkComplete && result.value == expected[i]
                  && result.value == Reference(vectors[i]) && result.executingApicId == i,
                  "hash vectors and maximum iteration boundary");
        }
        // White-box fixture reaches a 2^32-event boundary without billions of jobs.
        CpuWorkQueue nearEnd;
        nearEnd.nextId = 0xFFFFFFFEU;
        WorkTicket penultimate, last;
        Check(nearEnd.Submit(WorkRequest(WorkModularSum, 1, 0), penultimate) == WorkAccepted
              && penultimate.id == 0xFFFFFFFEU, "penultimate sequence admitted");
        Check(nearEnd.Submit(WorkRequest(WorkModularSum, 1, 0), last) == WorkAccepted
              && last.id == 0xFFFFFFFFU, "maximum sequence admitted exactly once");
        ticket = last;
        Check(nearEnd.Submit(WorkRequest(WorkModularSum, 1, 0), ticket) == WorkExhausted
              && !ticket.IsValid(), "sequence refuses to wrap and invalidates output ticket");
        Check(nearEnd.ExecuteOne(4) && nearEnd.ExecuteOne(5)
              && nearEnd.Collect(last, result) == WorkComplete && result.executingApicId == 5
              && nearEnd.Collect(penultimate, result) == WorkComplete && result.executingApicId == 4,
              "outstanding jobs remain usable after sequence exhaustion");
        Check(nearEnd.Submit(WorkRequest(WorkModularSum, 1, 0), ticket) == WorkExhausted,
              "collection cannot undo permanent sequence exhaustion");
        Check(nearEnd.Collect(last, result) == WorkTicketInvalid, "exhausted terminal ticket retires normally");
        nearEnd.rejected = 0xFFFFFFFFU;
        Check(nearEnd.Submit(WorkRequest(), ticket) == WorkInvalid
              && nearEnd.GetStats().rejected == 0xFFFFFFFFU, "rejected counter saturates without wrapping");
    }

    uint32_t Random(uint32_t& state) { state = state * 1664525U + 1013904223U; return state; }
    void TestRandomizedModel() {
        CpuWorkQueue queue;
        uint32_t states[16] = {}, ids[16] = {}, values[16] = {}, apics[16] = {};
        WorkRequest requests[16];
        uint32_t producer = 0, consumer = 0, sequence = 1, random = 0x13579BDFU;
        uint32_t accepted = 0, completed = 0, collected = 0, rejected = 0;
        bool valid = true;
        for (uint32_t step = 0; step < 100000 && valid; ++step) {
            const uint32_t choice = Random(random);
            if (choice % 10 < 4) {
                WorkRequest request((choice & 1) ? WorkIntegerHash : WorkModularSum,
                                    1 + (Random(random) % 64), Random(random));
                WorkTicket ticket;
                const WorkSubmitStatus status = queue.Submit(request, ticket);
                if (states[producer]) {
                    valid = status == WorkFull && !ticket.IsValid();
                    ++rejected;
                } else {
                    valid = status == WorkAccepted && ticket.slot == producer && ticket.id == sequence;
                    states[producer] = 1; ids[producer] = sequence++;
                    requests[producer] = request;
                    producer = (producer + 1) % 16; ++accepted;
                }
            } else if (choice % 10 < 7) {
                const uint32_t apic = Random(random);
                const bool expected = states[consumer] == 1;
                valid = queue.HasPending() == expected && queue.ExecuteOne(apic) == expected;
                if (expected) {
                    states[consumer] = 2;
                    values[consumer] = Reference(requests[consumer]); apics[consumer] = apic;
                    consumer = (consumer + 1) % 16; ++completed;
                }
            } else {
                const uint32_t index = (Random(random) >> 16) % 16;
                const uint32_t id = (choice & 0x100) ? ids[index] : ids[index] + 1;
                WorkResult result(0xAABBCCDD, 0x11223344);
                const WorkCollectStatus expected = !states[index] || id != ids[index] || !id
                    ? WorkTicketInvalid : states[index] == 1 ? WorkPending : WorkComplete;
                valid = queue.Collect(WorkTicket(index, id), result) == expected;
                if (expected == WorkComplete) {
                    valid = valid && result.value == values[index] && result.executingApicId == apics[index];
                    states[index] = 0; ++collected;
                } else {
                    valid = valid && result.value == 0xAABBCCDD && result.executingApicId == 0x11223344;
                }
            }
            const WorkQueueStats stats = queue.GetStats();
            valid = valid && stats.submitted == accepted && stats.completed == completed
                && stats.collected == collected && stats.rejected == rejected;
        }
        Check(valid, "100000 randomized transitions match independent bounded FIFO model");
    }

    void Yield() { Syscall3(158); }
    void Exit(uint32_t status) __attribute__((noreturn));
    void Exit(uint32_t status) {
        Syscall3(1, status);
        __builtin_unreachable();
    }
    void TestConcurrentPublication() {
        // Linux processes share only this anonymous mapping. QEMU-user executes
        // the same production queue in both processes, with no host C++ runtime.
        struct Shared { CpuWorkQueue queue; uint32_t stop; };
        static_assert(sizeof(Shared) <= 4096, "test shared mapping size");
        const uint32_t args[] = {0, 4096, 3, 0x21, 0xFFFFFFFFU, 0};
        const uint32_t mapped = (uint32_t)Syscall3(90, (uint32_t)args);
        if (mapped >= 0xFFFFF001U) { Check(false, "shared mmap succeeds"); return; }
        Shared* shared = new ((void*)mapped) Shared;
        __atomic_store_n(&shared->stop, 0, __ATOMIC_RELAXED);
        const int32_t child = Syscall3(2);
        if (child == 0) {
            while (!__atomic_load_n(&shared->stop, __ATOMIC_ACQUIRE)) {
                if (shared->queue.HasPending()) shared->queue.ExecuteOne(0x13579BDFU);
                else Yield();
            }
            Exit(0);
        }
        bool valid = child > 0;
        if (child > 0) {
            WorkTicket tickets[16];
            WorkRequest requests[16];
            WorkResult result;
            for (uint32_t batch = 0; batch < 1024 && valid; ++batch) {
                for (uint32_t i = 0; i < 16; ++i) {
                    requests[i] = WorkRequest((i & 1) ? WorkIntegerHash : WorkModularSum,
                                              1 + (batch + i) % 32, batch * 16 + i);
                    if (shared->queue.Submit(requests[i], tickets[i]) != WorkAccepted) valid = false;
                }
                for (uint32_t j = 16; j > 0 && valid; --j) {
                    const uint32_t i = j - 1;
                    WorkCollectStatus status;
                    while ((status = shared->queue.Collect(tickets[i], result)) == WorkPending) Yield();
                    valid = status == WorkComplete && result.value == Reference(requests[i])
                        && result.executingApicId == 0x13579BDFU;
                }
            }
            __atomic_store_n(&shared->stop, 1, __ATOMIC_RELEASE);
            uint32_t status = 0xFFFFFFFFU;
            const int32_t waited = Syscall3(7, child, (uint32_t)&status);
            valid = valid && waited == child && status == 0;
            const WorkQueueStats stats = shared->queue.GetStats();
            valid = valid && stats.submitted == 16384 && stats.completed == 16384
                && stats.collected == 16384 && stats.rejected == 0;
        }
        Check(valid, "16384 concurrent cross-process submissions publish requests and results correctly");
        Syscall3(91, mapped, 4096);
    }
}

extern "C" int CpuWorkQueueTestsMain() {
    TestBasicAndInvalid();
    TestFifoCapacityAndReuse();
    TestBoundsAndExhaustion();
    TestRandomizedModel();
    TestConcurrentPublication();
    if (!failures) Print("PASS: CPU work queue FIFO, capacity, stale tickets, bounds, sequence exhaustion, model and concurrent publication\n");
    return failures ? 1 : 0;
}
asm(".global _start\n_start:\n andl $-16, %esp\n call CpuWorkQueueTestsMain\n movl %eax, %ebx\n movl $1, %eax\n int $0x80\n");
