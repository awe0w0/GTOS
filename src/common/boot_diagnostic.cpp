#include <common/boot_diagnostic.h>

namespace gtos {
namespace common {

namespace {

// OptionPrefixLength counts the visible characters; the array needs one more byte for the terminating NUL.
const char kOptionPrefix[BootDiagnostic::OptionPrefixLength + 1] = "bootdiag-stop=";
const char kMemoryValue[] = "memory";
const char kTablesValue[] = "tables";
const char kApValue[] = "ap";
const char kPagingValue[] = "paging";

// Exact, length-bounded comparison against a NUL-terminated literal.
bool Equals(const char* span, const char* text, uint32_t length) {
    for (uint32_t i = 0; i < length; ++i) {
        if (span[i] != text[i])
            return false;
    }
    return text[length] == '\0';
}

// One table maps a value to its stop, so the parser and ValueName can never disagree about which token is accepted.
uint32_t StopForValue(const char* span, uint32_t length) {
    if (Equals(span, kMemoryValue, length)) return BootDiagnostic::StopMemory;
    if (Equals(span, kTablesValue, length)) return BootDiagnostic::StopTables;
    if (Equals(span, kApValue, length)) return BootDiagnostic::StopAp;
    if (Equals(span, kPagingValue, length)) return BootDiagnostic::StopPaging;
    return BootDiagnostic::StopNone;
}

} // namespace

BootDiagnostic::Decision BootDiagnostic::Parse(const char* span, uint32_t capacity) {
    Decision decision;
    decision.stop = StopNone;
    decision.reject = RejectNone;

    // No command line at all, or no readable bytes: the normal flow is unchanged.
    if (span == 0 || capacity == 0)
        return decision;

    // A caller that declares more readable bytes than the kernel's own bound has handed over a line this helper may
    // not interpret. Refuse rather than silently scanning a shorter prefix, because a clamp here would let a valid
    // early token hide a later duplicate or illegal option that sat beyond the shortened window.
    if (capacity > (uint32_t)MaximumCommandLine) {
        decision.stop = StopReject;
        decision.reject = RejectOversizeSpan;
        return decision;
    }

    const uint32_t limit = capacity;
    uint32_t found = StopNone;
    uint32_t index = 0;
    while (index < limit) {
        while (index < limit && span[index] == ' ')
            ++index;
        if (index >= limit)
            break;
        if (span[index] == '\0')
            break;

        const uint32_t start = index;
        uint32_t end = start;
        while (end < limit && span[end] != ' ' && span[end] != '\0')
            ++end;
        // end == limit means neither a space nor a NUL appeared inside the readable span.
        const bool unterminated = (end == limit);

        const uint32_t length = end - start;
        bool hasPrefix = length >= (uint32_t)OptionPrefixLength;
        if (hasPrefix) {
            for (uint32_t i = 0; i < (uint32_t)OptionPrefixLength; ++i) {
                if (span[start + i] != kOptionPrefix[i]) {
                    hasPrefix = false;
                    break;
                }
            }
        }
        if (!hasPrefix) {
            // An ordinary token. A long one that reaches the end is harmless, because it is not our option.
            index = end;
            continue;
        }

        if (unterminated) {
            decision.stop = StopReject;
            decision.reject = RejectUnterminated;
            return decision;
        }

        const uint32_t valueStart = start + (uint32_t)OptionPrefixLength;
        const uint32_t valueLength = end - valueStart;
        if (valueLength == 0) {
            decision.stop = StopReject;
            decision.reject = RejectEmptyValue;
            return decision;
        }
        const uint32_t thisStop = StopForValue(span + valueStart, valueLength);
        if (thisStop == StopNone) {
            decision.stop = StopReject;
            decision.reject = RejectUnknownValue;
            return decision;
        }

        // A second diagnostic option is ambiguous, even when it repeats the same value.
        if (found != StopNone) {
            decision.stop = StopReject;
            decision.reject = RejectDuplicate;
            return decision;
        }
        found = thisStop;
        index = end;
    }

    decision.stop = found;
    return decision;
}

const char* BootDiagnostic::Marker(uint32_t stop) {
    switch (stop) {
    case StopMemory:
        return "BOOTDIAG MEMORY CHECKS PASSED";
    case StopTables:
        return "BOOTDIAG TABLES CHECKPOINT";
    case StopAp:
        return "BOOTDIAG AP CHECKPOINT";
    case StopPaging:
        return "BOOTDIAG PAGING ENABLED OWNED MAP";
    case StopReject:
        return "BOOTDIAG REJECT invalid-option";
    default:
        return 0;
    }
}

const char* BootDiagnostic::RejectName(uint32_t reject) {
    switch (reject) {
    case RejectEmptyValue:
        return "empty-value";
    case RejectUnknownValue:
        return "unknown-value";
    case RejectDuplicate:
        return "duplicate";
    case RejectUnterminated:
        return "unterminated";
    case RejectOversizeSpan:
        return "oversize-span";
    default:
        return "none";
    }
}

uint32_t BootDiagnostic::RequiredPhase(uint32_t stop) {
    switch (stop) {
    case StopMemory:
        return PhaseMemory;
    case StopTables:
        return PhaseTables;
    case StopAp:
        return PhaseAp;
    case StopPaging:
        return PhasePaging;
    default:
        return PhaseCommandLine;
    }
}

const char* BootDiagnostic::ValueName(uint32_t stop) {
    switch (stop) {
    case StopMemory:
        return kMemoryValue;
    case StopTables:
        return kTablesValue;
    case StopAp:
        return kApValue;
    case StopPaging:
        return kPagingValue;
    default:
        return 0;
    }
}

} // namespace common
} // namespace gtos
