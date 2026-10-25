// Host unit for the production boot-diagnostic helper.
//
// This file is linked against the real src/common/boot_diagnostic.cpp object, so every expectation below is checked
// against the same function body the kernel calls. It does not re-implement the scan, and it never calls kernelMain,
// allocates, or touches a device.
//
// The program is a freestanding i386 ELF with no libc. Failures are written to fd 2 and the process exits with the
// number of failed expectations, so a zero exit means every check passed.
//
// Two defects from the earlier packet are corrected here, in the test rather than in the production return values:
//
//   1. The old ExpectStop helper asserted reject == RejectNone as part of "this is a stop". That made it impossible to
//      express a refusal, and the same input was then also expected to refuse, so the two expectations contradicted.
//      Acceptance and refusal are now separate predicates with separate reasons.
//   2. The old test asserted that the three markers differ in *length*. Length is not a business invariant; what matters
//      is that the three marker texts are distinguishable. That is now checked by comparing content.

#include <common/boot_diagnostic.h>

namespace {

typedef unsigned int u32;

int WriteAll(const char* text, u32 length) {
    int result = 0;
    asm volatile("int $0x80"
                 : "=a"(result)
                 : "a"(4), "b"(2), "c"(text), "d"(length)
                 : "memory");
    return result;
}

u32 Length(const char* text) {
    u32 n = 0;
    while (text && text[n])
        ++n;
    return n;
}

void Say(const char* text) { WriteAll(text, Length(text)); }

u32 failures = 0;
u32 checks = 0;

void Expect(bool condition, const char* label) {
    ++checks;
    if (condition)
        return;
    ++failures;
    Say("FAIL ");
    Say(label);
    Say("\n");
}

struct Span {
    const char* data;
    u32 size;
};

// The readable span of a NUL-terminated line includes the terminating NUL, so the size carries it. A case that wants
// to exercise an unterminated span builds its own buffer without a terminator, below.
Span Literal(const char* text) {
    Span span;
    span.data = text;
    span.size = Length(text) + 1;
    return span;
}

typedef gtos::common::BootDiagnostic Diagnostic;

Diagnostic::Decision Parse(Span span) { return Diagnostic::Parse(span.data, span.size); }

// An accepted stop: the decision must name that stop and carry no rejection reason.
void ExpectStop(Span span, u32 stop, const char* label) {
    const Diagnostic::Decision decision = Parse(span);
    ++checks;
    if (decision.stop == stop && decision.reject == Diagnostic::RejectNone)
        return;
    ++failures;
    Say("FAIL ");
    Say(label);
    Say("\n");
}

// A refusal: the decision must be StopReject and carry exactly this reason. This is deliberately NOT expressed through
// ExpectStop, because a refusal is not a stop and has a reason of its own.
void ExpectReject(Span span, u32 reject, const char* label) {
    const Diagnostic::Decision decision = Parse(span);
    ++checks;
    if (decision.stop == Diagnostic::StopReject && decision.reject == reject)
        return;
    ++failures;
    Say("FAIL ");
    Say(label);
    Say("\n");
}

bool TextEquals(const char* a, const char* b) {
    if (a == 0 || b == 0)
        return a == b;
    u32 i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i])
            return false;
        ++i;
    }
    return a[i] == b[i];
}

bool Contains(const char* text, const char* needle) {
    if (text == 0 || needle == 0)
        return false;
    for (u32 i = 0; text[i]; ++i) {
        u32 j = 0;
        while (needle[j] && text[i + j] == needle[j])
            ++j;
        if (!needle[j])
            return true;
    }
    return false;
}

} // namespace

extern "C" int bootDiagnosticTestMain() {
    // ================================================================ absent option preserves the flow
    ExpectStop(Literal(""), Diagnostic::StopNone, "an empty span selects no stop");
    ExpectStop(Literal("live serial legacy bootlog verbose"), Diagnostic::StopNone,
               "ordinary options select no stop");
    ExpectStop(Literal("bootlog-fail"), Diagnostic::StopNone,
               "the legacy E99 token is not a diagnostic stop");
    ExpectStop(Literal("live bootlog-fail serial"), Diagnostic::StopNone,
               "E99 among ordinary options is not a diagnostic stop");
    {
        const Diagnostic::Decision decision = Diagnostic::Parse(0, 256);
        Expect(decision.stop == Diagnostic::StopNone && decision.reject == Diagnostic::RejectNone,
               "a null span selects no stop");
    }
    {
        const Diagnostic::Decision decision = Diagnostic::Parse("bootdiag-stop=memory", 0);
        Expect(decision.stop == Diagnostic::StopNone && decision.reject == Diagnostic::RejectNone,
               "a zero capacity selects no stop");
    }

    // ================================================================ the two exact tokens
    ExpectStop(Literal("bootdiag-stop=memory"), Diagnostic::StopMemory, "the exact memory token");
    ExpectStop(Literal("bootdiag-stop=tables"), Diagnostic::StopTables, "the exact tables token");
    ExpectStop(Literal("legacy live serial bootlog bootdiag-stop=memory"), Diagnostic::StopMemory,
               "memory after ordinary tokens");
    ExpectStop(Literal("legacy live serial bootlog bootdiag-stop=tables"), Diagnostic::StopTables,
               "tables after ordinary tokens");
    ExpectStop(Literal("bootdiag-stop=tables legacy live serial"), Diagnostic::StopTables,
               "tables before ordinary tokens");
    ExpectStop(Literal("  bootdiag-stop=memory  "), Diagnostic::StopMemory,
               "leading and trailing spaces are separators");
    ExpectStop(Literal("live   bootdiag-stop=memory"), Diagnostic::StopMemory,
               "a run of spaces is still one separator");
    ExpectStop(Literal("bootdiag-stop=memory "), Diagnostic::StopMemory,
               "a trailing space after the token is accepted");

    // ---- the two new diagnostic options -------------------------------------------------------
    ExpectStop(Literal("bootdiag-stop=ap"), Diagnostic::StopAp, "the exact ap token");
    ExpectStop(Literal("bootdiag-stop=paging"), Diagnostic::StopPaging, "the exact paging token");
    ExpectStop(Literal("legacy live serial bootlog bootdiag-stop=ap"), Diagnostic::StopAp,
               "ap after ordinary tokens");
    ExpectStop(Literal("legacy live serial bootlog bootdiag-stop=paging"), Diagnostic::StopPaging,
               "paging after ordinary tokens");
    ExpectStop(Literal("  bootdiag-stop=paging  "), Diagnostic::StopPaging,
               "paging with surrounding spaces");
    // ---- the new values obey the same rejection rules as the old ones ------------------------
    ExpectReject(Literal("bootdiag-stop=a"), Diagnostic::RejectUnknownValue,
                 "a truncated ap value is unknown");
    ExpectReject(Literal("bootdiag-stop=APP"), Diagnostic::RejectUnknownValue,
                 "the ap value is case sensitive");
    ExpectReject(Literal("bootdiag-stop=aping"), Diagnostic::RejectUnknownValue,
                 "a suffix on the ap value is unknown");
    ExpectReject(Literal("bootdiag-stop=pag"), Diagnostic::RejectUnknownValue,
                 "a truncated paging value is unknown");
    ExpectReject(Literal("bootdiag-stop=pagingx"), Diagnostic::RejectUnknownValue,
                 "a suffix on the paging value is unknown");
    ExpectReject(Literal("bootdiag-stop=ap bootdiag-stop=ap"), Diagnostic::RejectDuplicate,
                 "a duplicate ap option refuses");
    ExpectReject(Literal("bootdiag-stop=paging bootdiag-stop=paging"), Diagnostic::RejectDuplicate,
                 "a duplicate paging option refuses");
    ExpectReject(Literal("bootdiag-stop=ap bootdiag-stop=paging"), Diagnostic::RejectDuplicate,
                 "two different diagnostic stops refuse");
    ExpectReject(Literal("bootdiag-stop=memory bootdiag-stop=ap"), Diagnostic::RejectDuplicate,
                 "an old and a new stop still refuse as a duplicate");
    // ---- the new values respect the 256 byte span contract -----------------------------------
    {
        static char apAtBound[256];
        const char* head = "bootdiag-stop=ap ";
        u32 n = 0;
        while (head[n]) { apAtBound[n] = head[n]; ++n; }
        while (n < 256) apAtBound[n++] = 'l';
        ExpectStop(Span{apAtBound, 256}, Diagnostic::StopAp,
                   "the ap token inside the 256 byte bound is accepted");
    }
    {
        static char pagingUnterminated[32];
        const char* head = "bootdiag-stop=paging";
        u32 n = 0;
        while (head[n]) { pagingUnterminated[n] = head[n]; ++n; }
        ExpectReject(Span{pagingUnterminated, n}, Diagnostic::RejectUnterminated,
                     "an unterminated paging token refuses");
    }
    // ---- old option regression: the original values are unchanged ----------------------------
    ExpectStop(Literal("bootdiag-stop=memory"), Diagnostic::StopMemory,
               "regression: memory is still the memory stop");
    ExpectStop(Literal("bootdiag-stop=tables"), Diagnostic::StopTables,
               "regression: tables is still the tables stop");
    ExpectStop(Literal("bootlog-fail"), Diagnostic::StopNone,
               "regression: the legacy E99 token is still not a stop");
    ExpectReject(Literal("bootdiag-stop=bogus"), Diagnostic::RejectUnknownValue,
                 "regression: an unknown value still refuses");
    ExpectReject(Literal("bootdiag-stop="), Diagnostic::RejectEmptyValue,
                 "regression: an empty value still refuses");

    // ================================================================ similar prefix and suffix tokens
    // These are ordinary tokens, not our option, so the flow is unchanged rather than refused.
    ExpectStop(Literal("bootdiag-stopx=memory"), Diagnostic::StopNone,
               "a longer option name is not our token");
    ExpectStop(Literal("xbootdiag-stop=memory"), Diagnostic::StopNone,
               "a token with a prefix is not at a token start");
    ExpectStop(Literal("bootdiag-stop"), Diagnostic::StopNone,
               "the bare option name without '=' is not our token");
    ExpectStop(Literal("bootdiag=memory"), Diagnostic::StopNone,
               "a different option name is not our token");
    ExpectStop(Literal("bootdiag-stop =memory"), Diagnostic::StopNone,
               "a space before '=' breaks the token into two ordinary tokens");
    // These carry the prefix but a wrong value, so they are refusals with a stated reason.
    ExpectReject(Literal("bootdiag-stop=memoryx"), Diagnostic::RejectUnknownValue,
                 "a suffix on the value is an unknown value");
    ExpectReject(Literal("bootdiag-stop=tables2"), Diagnostic::RejectUnknownValue,
                 "a numeric suffix is an unknown value");
    ExpectReject(Literal("bootdiag-stop=MEMORY"), Diagnostic::RejectUnknownValue,
                 "the value is case sensitive");
    ExpectReject(Literal("bootdiag-stop=mem"), Diagnostic::RejectUnknownValue,
                 "a truncated value is unknown");
    ExpectReject(Literal("bootdiag-stop=memorytables"), Diagnostic::RejectUnknownValue,
                 "a joined value is unknown");
    ExpectReject(Literal("bootdiag-stop=memory="), Diagnostic::RejectUnknownValue,
                 "a trailing '=' in the value is unknown");

    // ================================================================ empty and unknown values
    ExpectReject(Literal("bootdiag-stop="), Diagnostic::RejectEmptyValue, "an empty value refuses");
    ExpectReject(Literal("bootdiag-stop= bootdiag-stop=tables"), Diagnostic::RejectEmptyValue,
                 "an empty value before a valid token still refuses");
    ExpectReject(Literal("bootdiag-stop=bogus"), Diagnostic::RejectUnknownValue, "an unknown value refuses");

    // ================================================================ duplicate option, same value included
    ExpectReject(Literal("bootdiag-stop=memory bootdiag-stop=memory"), Diagnostic::RejectDuplicate,
                 "a duplicate memory option refuses");
    ExpectReject(Literal("bootdiag-stop=tables bootdiag-stop=tables"), Diagnostic::RejectDuplicate,
                 "a duplicate tables option refuses");
    ExpectReject(Literal("bootdiag-stop=memory bootdiag-stop=tables"), Diagnostic::RejectDuplicate,
                 "two different diagnostic stops refuse");

    // ================================================================ the span contract
    // capacity is the caller's guarantee of readable bytes. It is never silently reduced.
    {
        // A token that carries the prefix and runs to the end of the span has no terminator.
        static char unterminated[32];
        const char* head = "bootdiag-stop=memory";
        u32 n = 0;
        while (head[n]) { unterminated[n] = head[n]; ++n; }
        ExpectReject(Span{unterminated, n}, Diagnostic::RejectUnterminated,
                     "a prefixed token reaching the span end is unterminated");
    }
    {
        // The same bytes, but the span ends one byte later with no NUL, still unterminated.
        static char padded[33];
        const char* head = "bootdiag-stop=memory";
        u32 n = 0;
        while (head[n]) { padded[n] = head[n]; ++n; }
        padded[n] = 'y';
        ExpectReject(Span{padded, n + 1}, Diagnostic::RejectUnterminated,
                     "a prefixed token with a filler byte at the end is unterminated");
    }
    {
        // A valid token inside the bound, followed by a long ordinary token that reaches the end: still accepted,
        // because the long token is not our option.
        static char trailing[64];
        const char* head = "bootdiag-stop=memory ";
        u32 n = 0;
        while (head[n]) { trailing[n] = head[n]; ++n; }
        while (n < 40) trailing[n++] = 'l';
        ExpectStop(Span{trailing, 40}, Diagnostic::StopMemory,
                   "a valid token survives a long ordinary tail");
    }
    {
        // Exactly 256 readable bytes, which is the declared maximum.
        static char atBound[256];
        const char* head = "bootdiag-stop=tables";
        u32 n = 0;
        while (head[n]) { atBound[n] = head[n]; ++n; }
        atBound[n++] = ' ';
        while (n < 256) atBound[n++] = 'l';
        ExpectStop(Span{atBound, 256}, Diagnostic::StopTables,
                   "a token at the 256 byte bound is accepted");
    }
    {
        // One byte past the declared maximum is an oversize span and refuses, rather than clamping.
        static char beyond[257];
        const char* head = "bootdiag-stop=memory ";
        u32 n = 0;
        while (head[n]) { beyond[n] = head[n]; ++n; }
        while (n < 257) beyond[n++] = 'l';
        ExpectReject(Span{beyond, 257}, Diagnostic::RejectOversizeSpan,
                     "a span beyond 256 bytes refuses instead of clamping");
    }
    {
        // A well-formed token inside an oversize span must still refuse: the clamp would have hidden the tail.
        static char oversizeWithToken[300];
        const char* head = "bootdiag-stop=memory ";
        u32 n = 0;
        while (head[n]) { oversizeWithToken[n] = head[n]; ++n; }
        while (n < 300) oversizeWithToken[n++] = 'l';
        ExpectReject(Span{oversizeWithToken, 300}, Diagnostic::RejectOversizeSpan,
                     "a valid token cannot mask an oversize span");
    }
    {
        // A trailing duplicate that sits beyond 256 bytes cannot be hidden by an early valid token.
        static char masked[300];
        const char* head = "bootdiag-stop=memory ";
        u32 n = 0;
        while (head[n]) { masked[n] = head[n]; ++n; }
        while (n < 270) masked[n++] = 'l';
        const char* dup = " bootdiag-stop=memory";
        u32 d = 0;
        while (dup[d] && n < 300) { masked[n++] = dup[d++]; }
        ExpectReject(Span{masked, n}, Diagnostic::RejectOversizeSpan,
                     "a later duplicate beyond the bound is not masked");
    }

    // ================================================================ repeated parses retain no state
    {
        const Span memory = Literal("bootdiag-stop=memory");
        const Span tables = Literal("bootdiag-stop=tables");
        const Span none = Literal("live serial");
        const Span bad = Literal("bootdiag-stop=bogus");
        bool stable = true;
        for (u32 round = 0; round < 64; ++round) {
            const Diagnostic::Decision a = Parse(memory);
            const Diagnostic::Decision b = Parse(none);
            const Diagnostic::Decision c = Parse(tables);
            const Diagnostic::Decision d = Parse(bad);
            if (a.stop != Diagnostic::StopMemory || a.reject != Diagnostic::RejectNone
                || b.stop != Diagnostic::StopNone
                || c.stop != Diagnostic::StopTables || c.reject != Diagnostic::RejectNone
                || d.stop != Diagnostic::StopReject || d.reject != Diagnostic::RejectUnknownValue)
                stable = false;
        }
        Expect(stable, "64 interleaved parses are deterministic and retain no state");
    }
    {
        // The same decision must be produced by a fresh call after a refusal, in both directions.
        bool stable = true;
        for (u32 round = 0; round < 32; ++round) {
            const Diagnostic::Decision refused = Parse(Literal("bootdiag-stop="));
            const Diagnostic::Decision accepted = Parse(Literal("bootdiag-stop=tables"));
            if (refused.reject != Diagnostic::RejectEmptyValue
                || accepted.stop != Diagnostic::StopTables)
                stable = false;
        }
        Expect(stable, "a refusal does not leak into the next parse");
    }

    // ================================================================ markers are distinguishable by content
    {
        const char* memory = Diagnostic::Marker(Diagnostic::StopMemory);
        const char* tables = Diagnostic::Marker(Diagnostic::StopTables);
        const char* reject = Diagnostic::Marker(Diagnostic::StopReject);
        Expect(memory != 0, "memory has a marker");
        Expect(tables != 0, "tables has a marker");
        Expect(reject != 0, "reject has a marker");
        Expect(Diagnostic::Marker(Diagnostic::StopNone) == 0, "no stop has no marker");
        // Content uniqueness, not length coincidence: the three texts must differ from each other.
        Expect(!TextEquals(memory, tables), "memory and tables markers differ in content");
        Expect(!TextEquals(memory, reject), "memory and reject markers differ in content");
        Expect(!TextEquals(tables, reject), "tables and reject markers differ in content");
        const char* ap = Diagnostic::Marker(Diagnostic::StopAp);
        const char* paging = Diagnostic::Marker(Diagnostic::StopPaging);
        Expect(ap != 0, "ap has a marker");
        Expect(paging != 0, "paging has a marker");
        // Every marker pair must differ, so a log reader can never confuse two checkpoints.
        const char* all[5] = {memory, tables, ap, paging, reject};
        bool allDistinct = true;
        for (u32 i = 0; i < 5; ++i)
            for (u32 j = i + 1; j < 5; ++j)
                if (TextEquals(all[i], all[j])) allDistinct = false;
        Expect(allDistinct, "all five markers differ in content");
        Expect(Contains(ap, "AP"), "the ap marker names AP");
        Expect(Contains(paging, "PAGING"), "the paging marker names PAGING");
        Expect(!Contains(ap, "CHECKS PASSED"), "the ap marker claims no memory pass");
        Expect(!Contains(ap, "TABLES"), "the ap marker cannot be mistaken for the tables checkpoint");
        Expect(!Contains(tables, "AP "), "the tables marker cannot be mistaken for the ap checkpoint");
        Expect(!Contains(paging, "AP "), "the paging marker cannot be mistaken for the ap checkpoint");
        Expect(!Contains(paging, "CHECKS PASSED"),
               "the paging marker cannot be mistaken for a memory pass");
        Expect(Contains(paging, "OWNED MAP"), "the paging marker names the owned map probe");
        // And each must be recognisable on its own, so a log reader can identify which one appeared.
        Expect(Contains(memory, "MEMORY"), "the memory marker names MEMORY");
        Expect(Contains(tables, "TABLES"), "the tables marker names TABLES");
        Expect(Contains(reject, "REJECT"), "the reject marker names REJECT");
        Expect(!Contains(tables, "MEMORY CHECKS PASSED"),
               "the tables marker cannot be mistaken for a memory pass");
        Expect(!Contains(reject, "CHECKS PASSED") && !Contains(reject, "CHECKPOINT"),
               "the reject marker claims no checkpoint");
        // A guest log check that looks for the memory pass must not match the tables checkpoint.
        Expect(Contains(memory, "CHECKS PASSED") && !Contains(tables, "CHECKS PASSED"),
               "only the memory marker carries CHECKS PASSED");
    }

    // ================================================================ phases and reject names
    Expect(Diagnostic::RequiredPhase(Diagnostic::StopReject) == Diagnostic::PhaseCommandLine,
           "reject belongs to the command-line phase");
    Expect(Diagnostic::RequiredPhase(Diagnostic::StopMemory) == Diagnostic::PhaseMemory,
           "memory belongs to the memory phase");
    Expect(Diagnostic::RequiredPhase(Diagnostic::StopTables) == Diagnostic::PhaseTables,
           "tables belongs to the tables phase");
    Expect(Diagnostic::RequiredPhase(Diagnostic::StopMemory)
           < Diagnostic::RequiredPhase(Diagnostic::StopTables),
           "memory is dominated by tables");
    Expect(Diagnostic::RequiredPhase(Diagnostic::StopAp) == Diagnostic::PhaseAp,
           "ap belongs to the ap phase");
    Expect(Diagnostic::RequiredPhase(Diagnostic::StopPaging) == Diagnostic::PhasePaging,
           "paging belongs to the paging phase");
    // The phase order is the real boot order, so each later checkpoint strictly dominates the earlier ones.
    Expect(Diagnostic::RequiredPhase(Diagnostic::StopTables)
           < Diagnostic::RequiredPhase(Diagnostic::StopAp),
           "tables is dominated by ap");
    Expect(Diagnostic::RequiredPhase(Diagnostic::StopAp)
           < Diagnostic::RequiredPhase(Diagnostic::StopPaging),
           "ap is dominated by paging");
    // The value name a caller reports must be the value the parser accepts, or a log could name the wrong stop.
    Expect(TextEquals(Diagnostic::ValueName(Diagnostic::StopMemory), "memory"), "memory value name");
    Expect(TextEquals(Diagnostic::ValueName(Diagnostic::StopTables), "tables"), "tables value name");
    Expect(TextEquals(Diagnostic::ValueName(Diagnostic::StopAp), "ap"), "ap value name");
    Expect(TextEquals(Diagnostic::ValueName(Diagnostic::StopPaging), "paging"), "paging value name");
    Expect(Diagnostic::ValueName(Diagnostic::StopNone) == 0, "no stop has no value name");
    Expect(Diagnostic::ValueName(Diagnostic::StopReject) == 0, "reject has no value name");
    Expect(Diagnostic::RequiredPhase(Diagnostic::StopReject)
           < Diagnostic::RequiredPhase(Diagnostic::StopMemory),
           "reject is dominated by memory");
    {
        // Every rejection reason has a distinct name, so a guest log records why it refused.
        const char* names[6] = {
            Diagnostic::RejectName(Diagnostic::RejectNone),
            Diagnostic::RejectName(Diagnostic::RejectEmptyValue),
            Diagnostic::RejectName(Diagnostic::RejectUnknownValue),
            Diagnostic::RejectName(Diagnostic::RejectDuplicate),
            Diagnostic::RejectName(Diagnostic::RejectUnterminated),
            Diagnostic::RejectName(Diagnostic::RejectOversizeSpan),
        };
        bool allNamed = true;
        for (u32 i = 0; i < 6; ++i) {
            if (names[i] == 0 || Length(names[i]) == 0)
                allNamed = false;
        }
        Expect(allNamed, "every rejection reason is named");
        bool allDistinct = true;
        for (u32 i = 0; i < 6; ++i) {
            for (u32 j = i + 1; j < 6; ++j) {
                if (TextEquals(names[i], names[j]))
                    allDistinct = false;
            }
        }
        Expect(allDistinct, "the rejection names are distinct");
        Expect(TextEquals(Diagnostic::RejectName(Diagnostic::RejectOversizeSpan), "oversize-span"),
               "the oversize reason is named as such");
    }

    Say(failures == 0 ? "BOOTDIAG HOST UNIT PASS\n" : "BOOTDIAG HOST UNIT FAIL\n");
    return (int)failures;
}
