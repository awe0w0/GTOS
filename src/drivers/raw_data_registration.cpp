#include <drivers/raw_data_registration.h>
#include <memory/criticalsection.h>

using namespace gtos::drivers;

// Constant initialization in this TU; no kernel-entry call or heap is required.
RawDataRegistrationCell RawDataRegistrationCell::bootCell;

RawDataRegistrationCell& RawDataRegistrationCell::BootCell() {
    return bootCell;
}

RawDataRegistrationLease::~RawDataRegistrationLease() {
    if (held && issuer != 0) issuer->Release(this);
}

bool RawDataRegistrationCell::TryBind(RawDataHandler* requested,
                                      RawDataRegistrationKey* out) {
    if (requested == 0 || out == 0) return false;
    gtos::memory::InterruptGuard guard;
    if ((state != CELL_FREE && state != CELL_BOUND) || active != 0) return false;
    if (generation == 0xFFFFFFFFu) return false;
    generation++;
    target = requested;
    state = CELL_BOUND;
    out->issuer = this;
    out->target = requested;
    out->generation = generation;
    return true;
}

bool RawDataRegistrationCell::Acquire(RawDataRegistrationLease* out) {
    if (out == 0) return false;
    gtos::memory::InterruptGuard guard;
    if (out->held) return false;
    if (state != CELL_BOUND || target == 0 || generation == 0) return false;
    if (active == 0xFFFFFFFFu) return false;
    active++;
    out->issuer = this;
    out->target = target;
    out->generation = generation;
    out->held = true;
    return true;
}

bool RawDataRegistrationCell::RequestRetire(const RawDataRegistrationKey& key) {
    gtos::memory::InterruptGuard guard;
    if (key.issuer != this || key.target == 0 || key.generation == 0) return false;
    if (key.target != target || key.generation != generation) return false;
    if (state == CELL_RETIRING) return true;
    if (state != CELL_BOUND) return false;
    state = CELL_RETIRING;
    // No future Release exists for an idle binding: finalize it right here.
    if (active == 0) FinishRetirementLocked();
    return true;
}

bool RawDataRegistrationCell::Release(RawDataRegistrationLease* lease) {
    if (lease == 0) return false;
    gtos::memory::InterruptGuard guard;
    // Validate before consuming. Refusal cannot strand a valid record by editing it.
    if (!lease->held || lease->issuer != this) return false;
    if (lease->target != target || lease->generation != generation) return false;
    if ((state != CELL_BOUND && state != CELL_RETIRING) || active == 0) return false;
    lease->held = false;
    lease->issuer = 0;
    lease->target = 0;
    lease->generation = 0;
    active--;
    if (state == CELL_RETIRING && active == 0) FinishRetirementLocked();
    return true;
}

void RawDataRegistrationCell::FinishRetirementLocked() {
    if (state != CELL_RETIRING || active != 0) return;
    target = 0;
    if (generation == 0xFFFFFFFFu) {
        state = CELL_EXHAUSTED;
        return;
    }
    generation++;
    state = generation == 0xFFFFFFFFu ? CELL_EXHAUSTED : CELL_FREE;
}

bool RawDataRegistrationCell::TryDrain(const RawDataRegistrationKey& key) const {
    gtos::memory::InterruptGuard guard;
    if (key.issuer != this || key.target == 0 || key.generation == 0) return false;
    if (key.generation > generation) return false;
    // Every generation change requires zero active uses of the older binding.
    if (key.generation < generation) return true;
    return state == CELL_EXHAUSTED && target == 0 && active == 0;
}
