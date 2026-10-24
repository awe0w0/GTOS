// GTOS-DSH-090 — real unit tests for the typed raw-data registration cell.
//
// This file is compiled together with the REAL src/drivers/raw_data_registration.cpp and linked into a static
// i386 ELF that is then executed. Nothing here re-implements the cell: every state transition asserted below is
// performed by the shipped implementation, and every observation is read back out of it.
//
// THE SEAM IS DELIBERATELY SMALL, AND THAT SHAPES THESE TESTS
//
// The approved header adds exactly one thing: a guarded `friend class RawDataRegistrationCellTestAccess` inside
// RawDataRegistrationCell. It does NOT friend RawDataRegistrationKey or RawDataRegistrationLease, and friendship
// is not transitive. So this file can reach the cell's private `state`, `generation`, `active` and `target`, and
// it can reach nothing private in the key or the lease.
//
// That is a real constraint, not an inconvenience to route around. It means a key or a lease can only be
// observed through its public surface - `Issued()`, `Held()`, `CapturedTarget()` - and a malformed key cannot be
// fabricated at all. The cases below are written to that constraint, and the ones that would have needed a
// fabricated key are replaced with cases that reach the same refusal through records a caller can really obtain.
//
// WHAT THE SEAM IS ALLOWED TO DO
//
//   1. hand out independent cells, so a test never touches the shared boot cell's state
//   2. inject the boundary values a black-box test cannot reach (generation at the reserved value, active at
//      UINT32_MAX, a specific state)
//   3. observe the cell's own fields
//
// It never performs TryBind, Acquire, Release, RequestRetire, TryDrain or FinishRetirementLocked on the cell's
// behalf. If it did, the test would be checking its own re-implementation instead of the shipped code.
//
// WHAT THESE TESTS DO NOT PROVE
//
// The cell is a single-threaded state machine here. `GTOS_MEMORY_TEST` compiles the InterruptGuard in
// criticalsection.h down to nothing, so no CLI/STI executes and no interrupt is ever masked. These tests say
// nothing about interrupt safety, SMP safety, a real handler's lifetime, the provider's wiring, or the target
// kernel. They prove the state machine, its boundaries and its RAII, on this host, in this process.

#include <drivers/raw_data_registration.h>
#include <common/types.h>

extern "C" void test_write(const char* text, unsigned int length);
extern "C" void test_exit(int code) __attribute__((noreturn));

using namespace gtos::drivers;

namespace gtos {
namespace drivers {

// Declared as the cell's friend under GTOS_RAW_DATA_REGISTRATION_TEST. It must live in gtos::drivers, because
// that is the namespace the friend declaration in the header names; a class with the same spelling at global
// scope is a different class and gets no access at all.
class RawDataRegistrationCellTestAccess {
public:
    static RawDataRegistrationCell cells[8];

    static RawDataRegistrationCell* Cell(unsigned int index) {
        return &cells[index % 8];
    }

    static uint32_t Generation(const RawDataRegistrationCell* cell) { return cell->generation; }
    static void SetGeneration(RawDataRegistrationCell* cell, uint32_t value) { cell->generation = value; }
    static uint32_t Active(const RawDataRegistrationCell* cell) { return cell->active; }
    static void SetActive(RawDataRegistrationCell* cell, uint32_t value) { cell->active = value; }
    static int StateOf(const RawDataRegistrationCell* cell) { return (int)cell->state; }
    static void SetState(RawDataRegistrationCell* cell, int value) {
        cell->state = (RawDataRegistrationCell::State)value;
    }
    static RawDataHandler* Target(const RawDataRegistrationCell* cell) { return cell->target; }
    static void SetTarget(RawDataRegistrationCell* cell, RawDataHandler* value) { cell->target = value; }

    static int StateFree() { return (int)RawDataRegistrationCell::CELL_FREE; }
    static int StateBound() { return (int)RawDataRegistrationCell::CELL_BOUND; }
    static int StateRetiring() { return (int)RawDataRegistrationCell::CELL_RETIRING; }
    static int StateExhausted() { return (int)RawDataRegistrationCell::CELL_EXHAUSTED; }

    static void Reset(RawDataRegistrationCell* cell) {
        cell->target = 0;
        cell->generation = 0;
        cell->active = 0;
        cell->state = RawDataRegistrationCell::CELL_FREE;
    }
};

RawDataRegistrationCell RawDataRegistrationCellTestAccess::cells[8];

} // namespace drivers
} // namespace gtos

// ---------------------------------------------------------------- opaque handler identities
//
// The cell only ever compares and stores a `RawDataHandler*`; it never dereferences one. So a distinct address
// of static storage is a faithful stand-in for a handler identity, and it is deliberately never dereferenced
// here either. This is an identity test, not a handler-lifetime test: no handler is constructed, called or
// destroyed anywhere in this file.
static unsigned char g_handlerStorage[4][8];

static RawDataHandler* HandlerA() { return reinterpret_cast<RawDataHandler*>(&g_handlerStorage[0][0]); }
static RawDataHandler* HandlerB() { return reinterpret_cast<RawDataHandler*>(&g_handlerStorage[1][0]); }

// ---------------------------------------------------------------- output and assertions
//
// Literal lengths are never hand-counted: `write_text` measures the string it was given, so editing a message
// cannot silently truncate it or read past its end.
static void write_text(const char* text) {
    unsigned int n = 0;
    while (text[n] != 0) { ++n; }
    test_write(text, n);
}

static unsigned int g_cases = 0;
static unsigned int g_assertions = 0;
static unsigned int g_failures = 0;
static unsigned int g_cycles = 0;

static char g_line[256];
static unsigned int g_used = 0;

static void line_reset() { g_used = 0; g_line[0] = 0; }

static void line_put(const char* text) {
    while (*text != 0 && g_used < sizeof(g_line) - 1) {
        g_line[g_used++] = *text++;
    }
    g_line[g_used] = 0;
}

static void line_put_u32(uint32_t value) {
    char digits[11];
    unsigned int at = 0;
    if (value == 0) { line_put("0"); return; }
    while (value > 0 && at < sizeof(digits)) {
        digits[at++] = (char)('0' + (value % 10));
        value /= 10;
    }
    char ordered[12];
    unsigned int k = 0;
    while (at > 0) { ordered[k++] = digits[--at]; }
    ordered[k] = 0;
    line_put(ordered);
}

static void line_flush() {
    line_put("\n");
    test_write(g_line, g_used);
    line_reset();
}

// Every assertion names the case it belongs to, so a failure line is actionable on its own.
static bool expect(bool condition, const char* caseId, const char* what) {
    ++g_assertions;
    if (condition) { return true; }
    ++g_failures;
    line_reset();
    line_put("FAIL ");
    line_put(caseId);
    line_put(": ");
    line_put(what);
    line_flush();
    return false;
}

static void begin_case(const char* caseId) {
    ++g_cases;
    line_reset();
    line_put("CASE ");
    line_put(caseId);
}

static void end_case() {
    line_put(" ok");
    line_flush();
}

// ---------------------------------------------------------------- the cases

// C01 - a freshly reset cell binds nothing and holds no use.
static void case_initial_state() {
    const char* id = "C01-initial-no-binding";
    begin_case(id);
    RawDataRegistrationCell* cell = RawDataRegistrationCellTestAccess::Cell(0);
    RawDataRegistrationCellTestAccess::Reset(cell);

    expect(RawDataRegistrationCellTestAccess::StateOf(cell)
               == RawDataRegistrationCellTestAccess::StateFree(), id, "state is not CELL_FREE");
    expect(RawDataRegistrationCellTestAccess::Target(cell) == 0, id, "target is not null");
    expect(RawDataRegistrationCellTestAccess::Generation(cell) == 0, id, "generation is not 0");
    expect(RawDataRegistrationCellTestAccess::Active(cell) == 0, id, "active is not 0");

    RawDataRegistrationLease lease;
    expect(!cell->Acquire(&lease), id, "Acquire succeeded on an unbound cell");
    expect(!lease.Held(), id, "a refused Acquire still held the lease");
    expect(lease.CapturedTarget() == 0, id, "a refused Acquire captured a target");

    RawDataRegistrationKey key;
    expect(!key.Issued(), id, "a default key reports itself issued");
    expect(!cell->TryDrain(key), id, "TryDrain accepted a default key");
    expect(!cell->RequestRetire(key), id, "RequestRetire accepted a default key");
    expect(RawDataRegistrationCellTestAccess::Generation(cell) == 0, id,
           "a refused operation advanced the generation");
    end_case();
}

// C02 - every null argument is refused, and a refusal never disturbs a record the caller already holds.
static void case_null_arguments() {
    const char* id = "C02-null-arguments";
    begin_case(id);
    RawDataRegistrationCell* cell = RawDataRegistrationCellTestAccess::Cell(1);
    RawDataRegistrationCellTestAccess::Reset(cell);

    RawDataRegistrationKey key;
    expect(!cell->TryBind(0, &key), id, "TryBind accepted a null target");
    expect(!key.Issued(), id, "a refused TryBind still issued the key");
    expect(!cell->TryBind(HandlerA(), 0), id, "TryBind accepted a null out");
    expect(RawDataRegistrationCellTestAccess::StateOf(cell)
               == RawDataRegistrationCellTestAccess::StateFree(), id,
           "a refused TryBind changed the state");
    expect(RawDataRegistrationCellTestAccess::Generation(cell) == 0, id,
           "a refused TryBind advanced the generation");

    expect(!cell->Acquire(0), id, "Acquire accepted a null out");
    expect(!cell->Release(0), id, "Release accepted a null lease");

    RawDataRegistrationLease never;
    expect(!cell->Release(&never), id, "Release accepted a lease that was never acquired");

    // With a valid binding in place, the same null refusals must leave the caller's key intact. A refusal that
    // half-writes its output is a corruption path, not a refusal.
    expect(cell->TryBind(HandlerA(), &key), id, "TryBind failed for a valid target");
    expect(!cell->TryBind(0, &key), id, "a null-target TryBind was accepted while bound");
    expect(key.Issued(), id, "a refused TryBind invalidated the caller's key");
    expect(RawDataRegistrationCellTestAccess::Generation(cell) == 1, id,
           "a refused TryBind advanced the generation");
    expect(RawDataRegistrationCellTestAccess::Target(cell) == HandlerA(), id,
           "a refused TryBind changed the target");
    end_case();
}

// C03 - a successful bind issues a key, and an idle binding retires at once.
static void case_bind_issues_key() {
    const char* id = "C03-bind-issues-key";
    begin_case(id);
    RawDataRegistrationCell* cell = RawDataRegistrationCellTestAccess::Cell(2);
    RawDataRegistrationCellTestAccess::Reset(cell);

    RawDataRegistrationKey key;
    expect(cell->TryBind(HandlerA(), &key), id, "TryBind failed");
    expect(key.Issued(), id, "the issued key does not report itself issued");
    expect(RawDataRegistrationCellTestAccess::Generation(cell) == 1, id, "the first generation is not 1");
    expect(RawDataRegistrationCellTestAccess::StateOf(cell)
               == RawDataRegistrationCellTestAccess::StateBound(), id, "state is not CELL_BOUND");
    expect(RawDataRegistrationCellTestAccess::Target(cell) == HandlerA(), id, "the cell kept another target");

    // An idle binding has no outstanding use, so retiring it must drain immediately.
    expect(!cell->TryDrain(key), id, "a live binding reported drained");
    expect(cell->RequestRetire(key), id, "RequestRetire failed on an idle binding");
    expect(cell->TryDrain(key), id, "an idle binding did not drain");
    expect(RawDataRegistrationCellTestAccess::StateOf(cell)
               == RawDataRegistrationCellTestAccess::StateFree(), id, "the cell did not return to CELL_FREE");
    expect(RawDataRegistrationCellTestAccess::Target(cell) == 0, id, "the retired target was not cleared");
    expect(RawDataRegistrationCellTestAccess::Generation(cell) == 2, id,
           "retirement did not advance the generation");
    end_case();
}

// C04 - rebinding while idle is allowed, and it drains the previous key.
static void case_idle_rebind_drains_old_key() {
    const char* id = "C04-idle-rebind-drains-old";
    begin_case(id);
    RawDataRegistrationCell* cell = RawDataRegistrationCellTestAccess::Cell(3);
    RawDataRegistrationCellTestAccess::Reset(cell);

    RawDataRegistrationKey first;
    expect(cell->TryBind(HandlerA(), &first), id, "the first bind failed");
    RawDataRegistrationKey second;
    expect(cell->TryBind(HandlerB(), &second), id, "an idle rebind was refused");
    expect(RawDataRegistrationCellTestAccess::Generation(cell) == 2, id,
           "the rebind did not advance the generation");
    expect(cell->TryDrain(first), id, "the superseded key did not drain");
    expect(!cell->TryDrain(second), id, "the live key reported drained");
    expect(RawDataRegistrationCellTestAccess::Target(cell) == HandlerB(), id,
           "the rebind did not take the new target");

    // And the old key can no longer retire the new binding.
    expect(!cell->RequestRetire(first), id, "a stale key retired the new binding");
    expect(RawDataRegistrationCellTestAccess::StateOf(cell)
               == RawDataRegistrationCellTestAccess::StateBound(), id,
           "the stale retire changed the state");
    expect(RawDataRegistrationCellTestAccess::Target(cell) == HandlerB(), id,
           "the stale retire changed the target");
    end_case();
}

// C05 - a rebind while a use is outstanding is refused, and the caller's output is untouched.
static void case_active_rebind_refused() {
    const char* id = "C05-active-rebind-refused";
    begin_case(id);
    RawDataRegistrationCell* cell = RawDataRegistrationCellTestAccess::Cell(4);
    RawDataRegistrationCellTestAccess::Reset(cell);

    RawDataRegistrationKey key;
    expect(cell->TryBind(HandlerA(), &key), id, "the bind failed");
    RawDataRegistrationLease lease;
    expect(cell->Acquire(&lease), id, "Acquire failed");
    expect(lease.Held(), id, "the lease does not report itself held");
    expect(lease.CapturedTarget() == HandlerA(), id, "the lease captured another target");
    expect(RawDataRegistrationCellTestAccess::Active(cell) == 1, id, "active is not 1 after one Acquire");

    RawDataRegistrationKey other;
    expect(!cell->TryBind(HandlerB(), &other), id, "a rebind was accepted with a use outstanding");
    expect(!other.Issued(), id, "the refused rebind still issued a key");
    expect(key.Issued(), id, "the refused rebind invalidated the live key");
    expect(RawDataRegistrationCellTestAccess::Generation(cell) == 1, id,
           "the refused rebind advanced the generation");
    expect(RawDataRegistrationCellTestAccess::Target(cell) == HandlerA(), id,
           "the refused rebind changed the target");

    expect(cell->Release(&lease), id, "Release failed");
    expect(!lease.Held(), id, "the released lease still reports itself held");
    expect(RawDataRegistrationCellTestAccess::Active(cell) == 0, id, "active did not return to 0");
    end_case();
}

// C06 - with two uses outstanding, retirement refuses new Acquires, and the drain only happens on the last
// release. This is the case where "one release is enough" and "the first release drains" would both be wrong.
static void case_two_leases_retire() {
    const char* id = "C06-two-leases-retire";
    begin_case(id);
    RawDataRegistrationCell* cell = RawDataRegistrationCellTestAccess::Cell(5);
    RawDataRegistrationCellTestAccess::Reset(cell);

    RawDataRegistrationKey key;
    expect(cell->TryBind(HandlerA(), &key), id, "the bind failed");
    RawDataRegistrationLease first;
    RawDataRegistrationLease second;
    expect(cell->Acquire(&first), id, "the first Acquire failed");
    expect(cell->Acquire(&second), id, "the second Acquire failed");
    expect(RawDataRegistrationCellTestAccess::Active(cell) == 2, id, "active is not 2");

    expect(cell->RequestRetire(key), id, "RequestRetire failed with two uses outstanding");
    expect(RawDataRegistrationCellTestAccess::StateOf(cell)
               == RawDataRegistrationCellTestAccess::StateRetiring(), id, "state is not CELL_RETIRING");
    expect(!cell->TryDrain(key), id, "the binding drained with two uses outstanding");

    // A retiring cell refuses new uses.
    RawDataRegistrationLease third;
    expect(!cell->Acquire(&third), id, "Acquire succeeded on a retiring cell");
    expect(!third.Held(), id, "the refused Acquire still held the lease");
    expect(RawDataRegistrationCellTestAccess::Active(cell) == 2, id,
           "the refused Acquire changed the use count");

    expect(cell->Release(&first), id, "the first Release failed");
    expect(RawDataRegistrationCellTestAccess::Active(cell) == 1, id, "active is not 1 after one release");
    expect(!cell->TryDrain(key), id, "the binding drained with one use still outstanding");
    expect(RawDataRegistrationCellTestAccess::StateOf(cell)
               == RawDataRegistrationCellTestAccess::StateRetiring(), id,
           "the first release ended the retirement");
    expect(RawDataRegistrationCellTestAccess::Target(cell) == HandlerA(), id,
           "the first release cleared the target");

    expect(cell->Release(&second), id, "the second Release failed");
    expect(RawDataRegistrationCellTestAccess::Active(cell) == 0, id, "active did not return to 0");
    expect(cell->TryDrain(key), id, "the binding did not drain on the last release");
    expect(RawDataRegistrationCellTestAccess::StateOf(cell)
               == RawDataRegistrationCellTestAccess::StateFree(), id,
           "the cell did not return to CELL_FREE");
    expect(RawDataRegistrationCellTestAccess::Target(cell) == 0, id, "the target was not cleared");
    end_case();
}

// C07 - the lease destructor is the release path. Leaving a scope must return the use.
static void case_raii_destructor() {
    const char* id = "C07-raii-destructor";
    begin_case(id);
    RawDataRegistrationCell* cell = RawDataRegistrationCellTestAccess::Cell(6);
    RawDataRegistrationCellTestAccess::Reset(cell);

    RawDataRegistrationKey key;
    expect(cell->TryBind(HandlerA(), &key), id, "the bind failed");
    {
        RawDataRegistrationLease lease;
        expect(cell->Acquire(&lease), id, "Acquire failed");
        expect(RawDataRegistrationCellTestAccess::Active(cell) == 1, id, "active is not 1 inside the scope");
        expect(lease.CapturedTarget() == HandlerA(), id, "the lease captured another target");
    }
    expect(RawDataRegistrationCellTestAccess::Active(cell) == 0, id,
           "the destructor did not return the use");

    // RAII during retirement: the destructor must complete the drain.
    expect(cell->RequestRetire(key), id, "RequestRetire failed on an idle binding");
    expect(cell->TryDrain(key), id, "the idle binding did not drain");

    RawDataRegistrationKey second;
    expect(cell->TryBind(HandlerB(), &second), id, "the rebind after retirement failed");
    {
        RawDataRegistrationLease lease;
        expect(cell->Acquire(&lease), id, "Acquire failed on the rebound cell");
        expect(cell->RequestRetire(second), id, "RequestRetire failed with one use outstanding");
        expect(!cell->TryDrain(second), id, "the binding drained while the lease was alive");
    }
    expect(cell->TryDrain(second), id, "the destructor did not complete the drain");
    expect(RawDataRegistrationCellTestAccess::StateOf(cell)
               == RawDataRegistrationCellTestAccess::StateFree(), id,
           "the cell did not return to CELL_FREE after the destructor drained it");
    end_case();
}

// C08 - releasing the same lease twice is refused, and the second refusal does not corrupt the count.
static void case_double_release() {
    const char* id = "C08-double-release";
    begin_case(id);
    RawDataRegistrationCell* cell = RawDataRegistrationCellTestAccess::Cell(7);
    RawDataRegistrationCellTestAccess::Reset(cell);

    RawDataRegistrationKey key;
    expect(cell->TryBind(HandlerA(), &key), id, "the bind failed");
    RawDataRegistrationLease first;
    RawDataRegistrationLease second;
    expect(cell->Acquire(&first), id, "the first Acquire failed");
    expect(cell->Acquire(&second), id, "the second Acquire failed");

    expect(cell->Release(&first), id, "the first Release failed");
    expect(!cell->Release(&first), id, "a second Release of the same lease was accepted");
    expect(RawDataRegistrationCellTestAccess::Active(cell) == 1, id,
           "the double release corrupted the use count");
    expect(!first.Held(), id, "the double-released lease reports itself held");
    expect(cell->Release(&second), id, "the remaining Release failed");
    expect(RawDataRegistrationCellTestAccess::Active(cell) == 0, id, "active did not return to 0");
    end_case();
}

// C09 - a lease from another cell is refused, and refusing it must not consume the valid lease.
static void case_foreign_lease() {
    const char* id = "C09-foreign-lease";
    begin_case(id);
    RawDataRegistrationCell* cellA = RawDataRegistrationCellTestAccess::Cell(0);
    RawDataRegistrationCell* cellB = RawDataRegistrationCellTestAccess::Cell(1);
    RawDataRegistrationCellTestAccess::Reset(cellA);
    RawDataRegistrationCellTestAccess::Reset(cellB);

    RawDataRegistrationKey keyA;
    RawDataRegistrationKey keyB;
    expect(cellA->TryBind(HandlerA(), &keyA), id, "cell A failed to bind");
    expect(cellB->TryBind(HandlerB(), &keyB), id, "cell B failed to bind");

    // A real lease issued by A, handed to B.
    RawDataRegistrationLease realLease;
    expect(cellA->Acquire(&realLease), id, "cell A failed to Acquire");
    expect(!cellB->Release(&realLease), id, "cell B released a lease issued by cell A");
    expect(realLease.Held(), id, "the foreign refusal consumed the valid lease");
    expect(RawDataRegistrationCellTestAccess::Active(cellA) == 1, id,
           "the foreign refusal changed cell A's use count");
    expect(RawDataRegistrationCellTestAccess::Active(cellB) == 0, id,
           "the foreign refusal changed cell B's use count");
    expect(cellA->Release(&realLease), id, "the real owner could not release its own lease");
    expect(!realLease.Held(), id, "the real owner's release left the lease held");
    expect(RawDataRegistrationCellTestAccess::Active(cellA) == 0, id,
           "the real owner's release left the use count above zero");
    end_case();
}

// C10 - a key from another cell is refused, and the receiving cell is left untouched.
static void case_foreign_key() {
    const char* id = "C10-foreign-key";
    begin_case(id);
    RawDataRegistrationCell* cellA = RawDataRegistrationCellTestAccess::Cell(2);
    RawDataRegistrationCell* cellB = RawDataRegistrationCellTestAccess::Cell(3);
    RawDataRegistrationCellTestAccess::Reset(cellA);
    RawDataRegistrationCellTestAccess::Reset(cellB);

    RawDataRegistrationKey keyA;
    expect(cellA->TryBind(HandlerA(), &keyA), id, "cell A failed to bind");
    RawDataRegistrationKey keyB;
    expect(cellB->TryBind(HandlerB(), &keyB), id, "cell B failed to bind");

    expect(!cellB->RequestRetire(keyA), id, "cell B retired a key issued by cell A");
    expect(RawDataRegistrationCellTestAccess::StateOf(cellB)
               == RawDataRegistrationCellTestAccess::StateBound(), id,
           "the foreign key changed cell B's state");
    expect(RawDataRegistrationCellTestAccess::Target(cellB) == HandlerB(), id,
           "the foreign key changed cell B's target");
    expect(!cellB->TryDrain(keyA), id, "cell B drained on a key issued by cell A");

    // The foreign refusal must not have disturbed A's own record either.
    expect(!cellA->TryDrain(keyA), id, "cell A's live key reported drained");
    expect(cellA->RequestRetire(keyA), id, "cell A could not retire its own key");
    end_case();
}

// C11 - a stale key cannot retire a newer binding, and the newer binding stays usable.
static void case_stale_key() {
    const char* id = "C11-stale-key";
    begin_case(id);
    RawDataRegistrationCell* cell = RawDataRegistrationCellTestAccess::Cell(4);
    RawDataRegistrationCellTestAccess::Reset(cell);

    RawDataRegistrationKey old;
    expect(cell->TryBind(HandlerA(), &old), id, "the first bind failed");
    expect(cell->RequestRetire(old), id, "retiring the idle binding failed");
    expect(cell->TryDrain(old), id, "the idle binding did not drain");

    RawDataRegistrationKey fresh;
    expect(cell->TryBind(HandlerB(), &fresh), id, "the rebind failed");
    expect(!cell->RequestRetire(old), id, "a drained key retired the new binding");
    expect(RawDataRegistrationCellTestAccess::Target(cell) == HandlerB(), id,
           "the stale retire replaced the target");
    expect(RawDataRegistrationCellTestAccess::StateOf(cell)
               == RawDataRegistrationCellTestAccess::StateBound(), id,
           "the stale retire changed the state");

    RawDataRegistrationLease lease;
    expect(cell->Acquire(&lease), id, "the new binding could not be acquired");
    expect(cell->Release(&lease), id, "the new binding could not be released");
    end_case();
}

// C12 - an already-held output lease is refused, and the existing use is not disturbed.
static void case_already_held_out() {
    const char* id = "C12-already-held-out";
    begin_case(id);
    RawDataRegistrationCell* cell = RawDataRegistrationCellTestAccess::Cell(5);
    RawDataRegistrationCellTestAccess::Reset(cell);

    RawDataRegistrationKey key;
    expect(cell->TryBind(HandlerA(), &key), id, "the bind failed");
    RawDataRegistrationLease lease;
    expect(cell->Acquire(&lease), id, "the first Acquire failed");
    expect(!cell->Acquire(&lease), id, "an Acquire into an already-held lease was accepted");
    expect(RawDataRegistrationCellTestAccess::Active(cell) == 1, id,
           "the refused Acquire changed the use count");
    expect(lease.CapturedTarget() == HandlerA(), id, "the refused Acquire changed the captured target");
    expect(cell->Release(&lease), id, "the real release failed");
    end_case();
}

// C13 - the use count saturates at UINT32_MAX and fails closed without changing anything.
static void case_active_saturation() {
    const char* id = "C13-active-saturation";
    begin_case(id);
    RawDataRegistrationCell* cell = RawDataRegistrationCellTestAccess::Cell(6);
    RawDataRegistrationCellTestAccess::Reset(cell);

    RawDataRegistrationKey key;
    expect(cell->TryBind(HandlerA(), &key), id, "the bind failed");
    RawDataRegistrationCellTestAccess::SetActive(cell, 0xFFFFFFFFu);

    RawDataRegistrationLease lease;
    expect(!cell->Acquire(&lease), id, "Acquire succeeded at a saturated use count");
    expect(!lease.Held(), id, "the saturated refusal still held the lease");
    expect(RawDataRegistrationCellTestAccess::Active(cell) == 0xFFFFFFFFu, id,
           "the saturated refusal changed the use count");

    // Retiring at a saturated count moves the cell to RETIRING and stops there, because a retirement only
    // finalizes at zero outstanding uses.
    expect(cell->RequestRetire(key), id, "RequestRetire failed at a saturated use count");
    expect(RawDataRegistrationCellTestAccess::StateOf(cell)
               == RawDataRegistrationCellTestAccess::StateRetiring(), id,
           "a saturated retirement did not move the cell to CELL_RETIRING");
    expect(!cell->TryDrain(key), id, "a saturated binding reported drained");
    expect(RawDataRegistrationCellTestAccess::Active(cell) == 0xFFFFFFFFu, id,
           "retirement changed the saturated use count");

    // A cell stuck at RETIRING with a saturated count is not usable.
    RawDataRegistrationKey blocked;
    expect(!cell->TryBind(HandlerB(), &blocked), id, "a retiring cell accepted a bind");
    expect(!blocked.Issued(), id, "the refused bind still issued a key");

    // Clearing the count by hand is not a release, so the binding still does not report drained. That is the
    // honest state: only Release can finish a retirement.
    RawDataRegistrationCellTestAccess::SetActive(cell, 0);
    expect(!cell->TryDrain(key), id,
           "a hand-cleared count reported drained, which would mean the state was never consulted");
    expect(RawDataRegistrationCellTestAccess::StateOf(cell)
               == RawDataRegistrationCellTestAccess::StateRetiring(), id,
           "clearing the count changed the state without a Release");
    end_case();
}

// C14 - the generation boundary. Binding stops at the reserved UINT32_MAX, retirement there makes the cell
// permanently exhausted, and exhaustion never wraps back to a usable cell.
static void case_generation_exhaustion() {
    const char* id = "C14-generation-exhaustion";
    begin_case(id);
    RawDataRegistrationCell* cell = RawDataRegistrationCellTestAccess::Cell(7);
    RawDataRegistrationCellTestAccess::Reset(cell);

    // A cell already at the reserved generation refuses to bind.
    RawDataRegistrationCellTestAccess::SetGeneration(cell, 0xFFFFFFFFu);
    RawDataRegistrationKey refused;
    expect(!cell->TryBind(HandlerA(), &refused), id, "TryBind succeeded at the reserved generation");
    expect(!refused.Issued(), id, "the refused bind still issued a key");
    expect(RawDataRegistrationCellTestAccess::Generation(cell) == 0xFFFFFFFFu, id,
           "the refused bind advanced the generation");

    // One below the boundary is still usable, and retiring it exhausts the cell.
    RawDataRegistrationCellTestAccess::SetGeneration(cell, 0xFFFFFFFEu);
    RawDataRegistrationCellTestAccess::SetState(cell, RawDataRegistrationCellTestAccess::StateFree());
    RawDataRegistrationCellTestAccess::SetTarget(cell, 0);
    RawDataRegistrationKey key;
    expect(cell->TryBind(HandlerA(), &key), id, "TryBind failed one generation below the boundary");
    expect(RawDataRegistrationCellTestAccess::Generation(cell) == 0xFFFFFFFFu, id,
           "the bind did not advance to the reserved generation");
    expect(cell->RequestRetire(key), id, "RequestRetire failed at the boundary");
    expect(RawDataRegistrationCellTestAccess::StateOf(cell)
               == RawDataRegistrationCellTestAccess::StateExhausted(), id,
           "retirement at the boundary did not exhaust the cell");
    expect(cell->TryDrain(key), id, "an exhausted cell did not report drained");
    expect(RawDataRegistrationCellTestAccess::Target(cell) == 0, id,
           "exhaustion left a target behind");

    // Exhaustion is permanent: no bind, and no wrap back into a usable generation.
    RawDataRegistrationKey afterExhaustion;
    expect(!cell->TryBind(HandlerB(), &afterExhaustion), id, "an exhausted cell accepted a bind");
    expect(!afterExhaustion.Issued(), id, "the exhausted bind still issued a key");
    expect(RawDataRegistrationCellTestAccess::Generation(cell) == 0xFFFFFFFFu, id,
           "the exhausted bind wrapped the generation");
    expect(RawDataRegistrationCellTestAccess::StateOf(cell)
               == RawDataRegistrationCellTestAccess::StateExhausted(), id,
           "the exhausted bind changed the state");

    RawDataRegistrationLease lease;
    expect(!cell->Acquire(&lease), id, "an exhausted cell accepted an Acquire");

    // Exhaustion also survives a further retire request on the drained key.
    expect(!cell->RequestRetire(key), id, "a drained key retired the exhausted cell again");
    expect(RawDataRegistrationCellTestAccess::StateOf(cell)
               == RawDataRegistrationCellTestAccess::StateExhausted(), id,
           "a repeated retire left the exhausted state");
    end_case();
}

// C15 - retiring twice, and retiring an idle binding, both behave as no-ops that still report success.
static void case_repeated_and_idle_retire() {
    const char* id = "C15-repeated-and-idle-retire";
    begin_case(id);
    RawDataRegistrationCell* cell = RawDataRegistrationCellTestAccess::Cell(0);
    RawDataRegistrationCellTestAccess::Reset(cell);

    RawDataRegistrationKey key;
    expect(cell->TryBind(HandlerA(), &key), id, "the bind failed");
    RawDataRegistrationLease lease;
    expect(cell->Acquire(&lease), id, "Acquire failed");
    expect(cell->RequestRetire(key), id, "the first RequestRetire failed");
    // A second request while already retiring is idempotent and reports success.
    expect(cell->RequestRetire(key), id, "a repeated RequestRetire was refused while retiring");
    expect(RawDataRegistrationCellTestAccess::StateOf(cell)
               == RawDataRegistrationCellTestAccess::StateRetiring(), id,
           "the repeated request changed the state");
    expect(cell->Release(&lease), id, "Release failed");
    expect(cell->TryDrain(key), id, "the binding did not drain");

    // On a drained binding the same key is refused rather than silently accepted.
    expect(!cell->RequestRetire(key), id, "a drained key retired a freed cell");

    // Immediate idle retirement, without any Acquire at all.
    RawDataRegistrationKey idle;
    expect(cell->TryBind(HandlerB(), &idle), id, "the idle bind failed");
    expect(cell->RequestRetire(idle), id, "the idle retirement failed");
    expect(cell->TryDrain(idle), id, "the idle retirement did not drain");
    expect(RawDataRegistrationCellTestAccess::StateOf(cell)
               == RawDataRegistrationCellTestAccess::StateFree(), id,
           "the idle retirement did not free the cell");
    end_case();
}

// C16 - eight thousand one hundred and ninety-two real lifecycles. Each one binds, acquires, releases, retires
// and drains, then checks the state, the target and the counters. Nothing is simulated: every step is the
// shipped implementation, and the generation must advance exactly twice per cycle with no leak of the use count.
static void case_lifecycle_cycles() {
    const char* id = "C16-lifecycle-8192";
    begin_case(id);
    RawDataRegistrationCell* cell = RawDataRegistrationCellTestAccess::Cell(1);
    RawDataRegistrationCellTestAccess::Reset(cell);

    const uint32_t total = 8192;
    uint32_t generation = 0;
    bool allOk = true;
    uint32_t firstBad = 0;

    for (uint32_t i = 0; i < total; ++i) {
        RawDataRegistrationKey key;
        RawDataRegistrationLease lease;
        bool stepOk = true;

        stepOk = cell->TryBind(HandlerA(), &key) && stepOk;
        generation += 1;
        stepOk = (RawDataRegistrationCellTestAccess::Generation(cell) == generation) && stepOk;
        stepOk = key.Issued() && stepOk;

        stepOk = cell->Acquire(&lease) && stepOk;
        stepOk = (RawDataRegistrationCellTestAccess::Active(cell) == 1) && stepOk;
        stepOk = (lease.CapturedTarget() == HandlerA()) && stepOk;

        stepOk = cell->Release(&lease) && stepOk;
        stepOk = (RawDataRegistrationCellTestAccess::Active(cell) == 0) && stepOk;

        stepOk = cell->RequestRetire(key) && stepOk;
        stepOk = cell->TryDrain(key) && stepOk;
        generation += 1;
        stepOk = (RawDataRegistrationCellTestAccess::Generation(cell) == generation) && stepOk;
        stepOk = (RawDataRegistrationCellTestAccess::StateOf(cell)
                  == RawDataRegistrationCellTestAccess::StateFree()) && stepOk;
        stepOk = (RawDataRegistrationCellTestAccess::Target(cell) == 0) && stepOk;

        ++g_cycles;
        if (!stepOk) {
            allOk = false;
            firstBad = i;
            break;
        }
    }

    expect(allOk, id, "a lifecycle cycle did not hold its invariants");
    if (!allOk) {
        line_reset();
        line_put("     first bad cycle ");
        line_put_u32(firstBad);
        line_put(" at generation ");
        line_put_u32(generation);
        line_flush();
    }
    expect(g_cycles == total, id, "the cycle count does not match the loop length");
    expect(generation == total * 2, id, "the generation did not advance exactly twice per cycle");
    expect(RawDataRegistrationCellTestAccess::Active(cell) == 0, id,
           "the use count leaked across the cycles");
    expect(RawDataRegistrationCellTestAccess::Target(cell) == 0, id, "a target leaked across the cycles");
    end_case();
}

// C17 - the shared boot cell is reachable and stable, and no test above touched its state.
static void case_boot_cell_identity() {
    const char* id = "C17-boot-cell-identity";
    begin_case(id);
    RawDataRegistrationCell& first = RawDataRegistrationCell::BootCell();
    RawDataRegistrationCell& second = RawDataRegistrationCell::BootCell();
    expect(&first == &second, id, "BootCell returned two different cells");
    // The boot cell is deliberately never mutated by these tests, so its counters must still be pristine.
    expect(RawDataRegistrationCellTestAccess::Generation(&first) == 0, id,
           "the boot cell's generation was disturbed");
    expect(RawDataRegistrationCellTestAccess::Active(&first) == 0, id,
           "the boot cell's use count was disturbed");
    expect(RawDataRegistrationCellTestAccess::Target(&first) == 0, id,
           "the boot cell's target was disturbed");
    end_case();
}

// ---------------------------------------------------------------- entry point
extern "C" int run_tests() {
    write_text("GTOS-DSH-090 raw data registration cell - real unit tests\n");
    write_text("cell implementation: src/drivers/raw_data_registration.cpp (linked, not modelled)\n");

    case_initial_state();
    case_null_arguments();
    case_bind_issues_key();
    case_idle_rebind_drains_old_key();
    case_active_rebind_refused();
    case_two_leases_retire();
    case_raii_destructor();
    case_double_release();
    case_foreign_lease();
    case_foreign_key();
    case_stale_key();
    case_already_held_out();
    case_active_saturation();
    case_generation_exhaustion();
    case_repeated_and_idle_retire();
    case_lifecycle_cycles();
    case_boot_cell_identity();

    line_reset();
    line_put("SUMMARY cases=");
    line_put_u32(g_cases);
    line_put(" assertions=");
    line_put_u32(g_assertions);
    line_put(" failures=");
    line_put_u32(g_failures);
    line_put(" cycles=");
    line_put_u32(g_cycles);
    line_flush();

    if (g_failures == 0) {
        write_text("RESULT PASS\n");
        return 0;
    }
    write_text("RESULT FAIL\n");
    return 1;
}
