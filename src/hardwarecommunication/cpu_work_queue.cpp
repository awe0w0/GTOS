#include <hardwarecommunication/cpu_work_queue.h>

using namespace gtos::hardwarecommunication;

static_assert(sizeof(uint32_t) == 4, "CPU work requires 32-bit unsigned integers");
static_assert(__atomic_always_lock_free(sizeof(uint32_t), 0),
              "CPU work requires lock-free 32-bit atomics");

WorkRequest::WorkRequest(WorkKind kind, uint32_t iterations, uint32_t seed)
    : kind(kind), iterations(iterations), seed(seed) {}

WorkTicket::WorkTicket(uint32_t slot, uint32_t id) : slot(slot), id(id) {}
bool WorkTicket::IsValid() const { return slot < CpuWorkQueue::Capacity && id != 0; }

WorkResult::WorkResult(uint32_t value, uint32_t executingApicId)
    : value(value), executingApicId(executingApicId) {}

CpuWorkQueue::CpuWorkQueue()
    : produceIndex(0), consumeIndex(0), nextId(1), submitted(0), completed(0),
      collected(0), rejected(0) {
    for (uint32_t i = 0; i < Capacity; ++i) {
        slots[i].state = SlotEmpty;
        slots[i].id = 0;
    }
}

void CpuWorkQueue::Reject() {
    // Only the BSP writes this counter; readers may run on the AP.
    uint32_t count = __atomic_load_n(&rejected, __ATOMIC_RELAXED);
    if (count != 0xFFFFFFFFU)
        __atomic_store_n(&rejected, count + 1, __ATOMIC_RELAXED);
}

WorkSubmitStatus CpuWorkQueue::Submit(const WorkRequest& request, WorkTicket& ticket) {
    ticket = WorkTicket();
    if ((request.kind != WorkIntegerHash && request.kind != WorkModularSum)
            || request.iterations == 0 || request.iterations > MaxIterations) {
        Reject();
        return WorkInvalid;
    }
    // Zero is a permanent terminal marker, never a usable sequence number.
    if (nextId == 0) {
        Reject();
        return WorkExhausted;
    }
    Slot& slot = slots[produceIndex];
    if (__atomic_load_n(&slot.state, __ATOMIC_ACQUIRE) != SlotEmpty) {
        Reject();
        return WorkFull;
    }
    slot.request = request;
    slot.id = nextId;
    ticket = WorkTicket(produceIndex, nextId);
    nextId = nextId == 0xFFFFFFFFU ? 0 : nextId + 1;
    produceIndex = (produceIndex + 1) % Capacity;
    __atomic_store_n(&submitted, __atomic_load_n(&submitted, __ATOMIC_RELAXED) + 1,
                     __ATOMIC_RELAXED);
    // Publishes request and ID after their ordinary writes are complete.
    __atomic_store_n(&slot.state, SlotPending, __ATOMIC_RELEASE);
    return WorkAccepted;
}

bool CpuWorkQueue::ExecuteOne(uint32_t actualApicId) {
    const uint32_t index = __atomic_load_n(&consumeIndex, __ATOMIC_RELAXED);
    Slot& slot = slots[index];
    if (__atomic_load_n(&slot.state, __ATOMIC_ACQUIRE) != SlotPending)
        return false;
    const WorkRequest request = slot.request;
    uint32_t value = request.seed;
    if (request.kind == WorkIntegerHash) {
        for (uint32_t i = 0; i < request.iterations; ++i) {
            value = (value ^ (i + 0x9E3779B9U)) * 16777619U;
            value ^= value >> 13;
        }
    } else { // Submit admits only these two built-in bounded operations.
        for (uint32_t i = 0; i < request.iterations; ++i)
            value += i + 1;
    }
    slot.result = WorkResult(value, actualApicId);
    __atomic_store_n(&completed, __atomic_load_n(&completed, __ATOMIC_RELAXED) + 1,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&consumeIndex, (index + 1) % Capacity, __ATOMIC_RELAXED);
    // The BSP must acquire completion before reading result or releasing reuse.
    __atomic_store_n(&slot.state, SlotComplete, __ATOMIC_RELEASE);
    return true;
}

WorkCollectStatus CpuWorkQueue::Collect(const WorkTicket& ticket, WorkResult& result) {
    if (!ticket.IsValid())
        return WorkTicketInvalid;
    Slot& slot = slots[ticket.slot];
    const uint32_t state = __atomic_load_n(&slot.state, __ATOMIC_ACQUIRE);
    // id is producer-owned. Submit/Collect are externally serialized, and the
    // AP never changes it, so no non-atomic ID access races are possible.
    if (state == SlotEmpty || slot.id != ticket.id)
        return WorkTicketInvalid;
    if (state != SlotComplete)
        return WorkPending;
    result = slot.result;
    __atomic_store_n(&collected, __atomic_load_n(&collected, __ATOMIC_RELAXED) + 1,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&slot.state, SlotEmpty, __ATOMIC_RELEASE);
    return WorkComplete;
}

bool CpuWorkQueue::HasPending() const {
    const uint32_t index = __atomic_load_n(&consumeIndex, __ATOMIC_RELAXED);
    return __atomic_load_n(&slots[index].state, __ATOMIC_ACQUIRE) == SlotPending;
}

WorkQueueStats CpuWorkQueue::GetStats() const {
    WorkQueueStats stats;
    stats.submitted = __atomic_load_n(&submitted, __ATOMIC_RELAXED);
    stats.completed = __atomic_load_n(&completed, __ATOMIC_RELAXED);
    stats.collected = __atomic_load_n(&collected, __ATOMIC_RELAXED);
    stats.rejected = __atomic_load_n(&rejected, __ATOMIC_RELAXED);
    return stats;
}
