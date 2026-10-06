#ifndef __GTOS__HARDWARECOMMUNICATION__CPU_WORK_QUEUE_H
#define __GTOS__HARDWARECOMMUNICATION__CPU_WORK_QUEUE_H

#include <common/types.h>

namespace gtos { namespace hardwarecommunication {
    enum WorkKind : uint32_t { WorkIntegerHash = 1, WorkModularSum = 2 };

    struct WorkRequest {
        WorkKind kind;
        uint32_t iterations;
        uint32_t seed;
        WorkRequest(WorkKind kind = WorkIntegerHash, uint32_t iterations = 0,
                    uint32_t seed = 0);
    };

    struct WorkTicket {
        uint32_t slot;
        uint32_t id;
        WorkTicket(uint32_t slot = 0xFFFFFFFFU, uint32_t id = 0);
        bool IsValid() const;
    };

    struct WorkResult {
        uint32_t value;
        uint32_t executingApicId;
        WorkResult(uint32_t value = 0, uint32_t executingApicId = 0xFFFFFFFFU);
    };

    enum WorkSubmitStatus { WorkAccepted, WorkFull, WorkInvalid, WorkExhausted };
    enum WorkCollectStatus { WorkTicketInvalid, WorkPending, WorkComplete };

    struct WorkQueueStats {
        uint32_t submitted;
        uint32_t completed;
        uint32_t collected;
        uint32_t rejected;
    };

    // One queue belongs to one BSP producer/collector and one AP consumer.
    // Submit and Collect must be serialized on the BSP (including IRQ entry).
    // Construct before publishing its address to the AP; never reset, move,
    // destroy or reuse the storage while a ticket or AP can still refer to it.
    class CpuWorkQueue {
    public:
        enum { Capacity = 16, MaxIterations = 65536 };
        CpuWorkQueue();
        // iterations must be 1..MaxIterations. Every failure invalidates ticket.
        // Slots stay occupied until collected; full may be conservative when
        // a later completed slot was collected before the next ring slot.
        WorkSubmitStatus Submit(const WorkRequest& request, WorkTicket& ticket);
        // The sole consumer supplies its actual hardware APIC ID. Executes at
        // most one job in FIFO order. Never invokes a callback or device code.
        bool ExecuteOne(uint32_t actualApicId);
        // Only WorkComplete writes result and releases the slot. Tickets are
        // queue-local; stale and already-collected tickets are rejected. A copy of
        // a live ticket may collect once; always use its originating queue.
        WorkCollectStatus Collect(const WorkTicket& ticket, WorkResult& result);
        // Acquire check of the consumer's next slot. Use on the consuming AP
        // inside the caller's interrupt-safe check/sleep protocol. Other CPUs
        // can use this as a momentary hint, never as a wakeup guarantee.
        bool HasPending() const;
        // Independently atomic counters, not a transactionally coherent snapshot.
        // Rejected saturates at UINT32_MAX; the others cannot overflow because
        // ticket IDs stop permanently after UINT32_MAX accepted submissions.
        WorkQueueStats GetStats() const;
    private:
        enum SlotState { SlotEmpty, SlotPending, SlotComplete };
        struct Slot {
            uint32_t state;
            uint32_t id;
            WorkRequest request;
            WorkResult result;
        };
        Slot slots[Capacity];
        uint32_t produceIndex;
        uint32_t consumeIndex;
        uint32_t nextId;
        uint32_t submitted;
        uint32_t completed;
        uint32_t collected;
        uint32_t rejected;
        void Reject();
        CpuWorkQueue(const CpuWorkQueue&);
        CpuWorkQueue& operator=(const CpuWorkQueue&);
    };
} }
#endif
