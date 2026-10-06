#include <process/native_fp.h>
using namespace gtos::process;
namespace {
    uint32_t failures, assertions;
    void Print(const char* text) {
        uint32_t length = 0; while (text[length]) ++length;
        uint32_t result;
        asm volatile("int $0x80" : "=a"(result) : "0"(4), "b"(1), "c"(text), "d"(length) : "memory", "cc");
    }
    void Check(bool value, const char* why) { ++assertions; if (!value) { ++failures; Print("FAIL "); Print(why); Print("\n"); } }
}
extern "C" int NativeFpTestsMain() {
    NativeFpRecord slots[4] = {};
    Check(sizeof(NativeFpContext) == 544 && sizeof(NativeFpRecord) == 560, "image/record size");
    for (uint32_t i = 0; i < 4; ++i) {
        Check(!((uint32_t)&slots[i].image & 15U), "each record alignment");
        Check((uint32_t)slots[i].image.env - (uint32_t)&slots[i] == 512, "environment offset");
    }
    NativeFpCapabilities caps = {true, 1, 0x07800001U, 1, 0};
    Check(ValidateNativeFpCapabilities(caps, NativeFpSse2) == NativeFpOk, "SSE2 floor");
    Check(ValidateNativeFpCapabilities(caps, NativeFpSse3) == NativeFpOk, "SSE3 floor");
    for (uint32_t bit = 0; bit < 32; ++bit) {
        NativeFpCapabilities bad = caps; bad.edx &= ~(1U << bit);
        Check((ValidateNativeFpCapabilities(bad, NativeFpSse2) == NativeFpMissingFeature)
            == ((caps.edx & (1U << bit)) != 0), "every baseline feature required independently");
    }
    caps.ecx = 0;
    Check(ValidateNativeFpCapabilities(caps, NativeFpSse2) == NativeFpOk
        && ValidateNativeFpCapabilities(caps, NativeFpSse3) == NativeFpMissingFeature, "SSE3 optional profile");
    caps.cr4 = 1U << 18;
    Check(ValidateNativeFpCapabilities(caps, NativeFpSse2) == NativeFpExtendedState, "OSXSAVE inheritance refused");
    Check(ValidateNativeFpCapabilities(caps, NativeFpDisabled) == NativeFpOk, "disabled policy ignores FP controls");
    caps.cr4 = 0; caps.cpuid = false;
    Check(ValidateNativeFpCapabilities(caps, NativeFpSse2) == NativeFpNoCpuid, "CPUID absent");
    caps.cpuid = true; caps.maximumLeaf = 0;
    Check(ValidateNativeFpCapabilities(caps, NativeFpSse2) == NativeFpNoCpuid, "CPUID leaf absent");
    Check(ValidateNativeFpCapabilities(caps, (NativeFpPolicy)99) == NativeFpBadPolicy, "unknown policy refused");
    for (uint32_t n = 0; n < 65536; ++n) {
        const uint32_t old = n | 0x80010000U;
        Check((NativeFpKernelCr0(old) & ~0x2EU) == (old & ~0x2EU)
            && (NativeFpKernelCr0(old) & 0x2EU) == 0x2AU, "CR0 unrelated bits preserved");
        Check((NativeFpCr4(old) & ~0x600U) == (old & ~0x600U)
            && (NativeFpCr4(old) & 0x600U) == 0x600U, "CR4 unrelated bits preserved");
    }
    for (uint32_t bit = 0; bit < 32; ++bit) {
        const uint32_t old = 1U << bit;
        Check((NativeFpKernelCr0(old) & ~0x2EU) == (old & ~0x2EU)
            && (NativeFpCr4(old) & ~0x600U) == (old & ~0x600U), "all unrelated high control bits preserved");
    }
    Check(NativeFpMxcsrMask(0) == 0xFFBF && NativeFpMxcsrMask(0xFFFF) == 0xFFFF
        && NativeFpMxcsrMask(0xFFFFFFFF) == 0xFFFF, "MXCSR fallback/reported/reserved masks");
    uint8_t environment[28];
    for (uint32_t i = 0; i < 28; ++i) environment[i] = 0xA5;
    NativeFpContext clean;
    NativeFpBuildCanonical(clean, environment, 0xFFBF);
    Check(NativeFpValidImage(clean, 0xFFBF), "built canonical valid");
    Check(NativeFpGet16(clean.fx) == 0x37F && NativeFpGet16(clean.env) == 0x37F
        && NativeFpGet16(clean.env + 8) == 0xFFFF, "canonical control and tags");
    for (uint32_t i = 32; i < 512; ++i) Check(clean.fx[i] == 0, "payload/reserved/software bytes zero");
    for (uint32_t i = 0; i < 4; ++i) Check(clean.padding[i] == 0, "alignment padding zero");
    for (uint32_t bit = 0; bit < 32; ++bit) {
        NativeFpPut32(clean.fx + 24, 1U << bit);
        Check(NativeFpValidImage(clean, 0xFFBF) == ((0xFFBFU & (1U << bit)) != 0), "reserved MXCSR refusal");
    }
    NativeFpPut32(clean.fx + 24, 0x1F80);
    NativeFpPut32(clean.env + 12, 0x44332211); NativeFpPut16(clean.env + 16, 0x23);
    NativeFpPut16(clean.env + 18, 0xB7E5); NativeFpPut32(clean.env + 20, 0xABCD1234);
    NativeFpPut16(clean.env + 24, 0x2B);
    Check(!NativeFpValidImage(clean, 0xFFBF), "incoherent environment rejected");
    NativeFpPatchPointers(clean);
    Check(NativeFpValidImage(clean, 0xFFBF) && NativeFpGet16(clean.fx + 6) == 0x7E5
        && NativeFpGet32(clean.fx + 8) == 0x44332211 && NativeFpGet32(clean.fx + 16) == 0xABCD1234,
        "AMD full environment patches precise legacy pointer/opcode fields");
    NativeFpCopy(slots[0].image, clean); slots[0].generation = 17; slots[0].initialized = 1;
    NativeFpTransition owner = {NativeFpUserLive, &slots[0], 17, 0};
    Check(NativeFpOwns(owner, &slots[0], 17), "exact owner generation");
    Check(!NativeFpOwns(owner, &slots[1], 17) && !NativeFpOwns(owner, &slots[0], 18)
        && !NativeFpOwns(owner, 0, 17) && !NativeFpOwns(owner, &slots[0], 0), "stale/wrong owner refused");
    for (uint32_t phase = NativeFpOff; phase <= NativeFpFailed; ++phase) {
        owner.phase = phase;
        Check(NativeFpCanEnter(owner, &slots[0], 17) == (phase == NativeFpUserLive), "entry only from live owner phase");
        Check(!NativeFpCanReturn(owner, &slots[0], 17), "no restore while any hardware owner exists");
        Check(!NativeFpCanInitialize(owner, slots[1], 18), "no admission while any hardware owner exists");
    }
    owner.phase = NativeFpKernelNeutral; owner.owner = 0; owner.generation = 0;
    Check(NativeFpCanReturn(owner, &slots[0], 17) && !NativeFpCanReturn(owner, &slots[0], 18),
        "neutral return requires initialized matching generation");
    Check(NativeFpCanInitialize(owner, slots[1], 18) && !NativeFpCanInitialize(owner, slots[1], 0)
        && !NativeFpCanInitialize(owner, slots[0], 18), "admission rejects wrap and occupied context");
    slots[0].initialized = 2;
    Check(!NativeFpCanReturn(owner, &slots[0], 17), "invalid initialized metadata refused");
    slots[0].initialized = 1;
    owner.generation = 17;
    Check(!NativeFpNeutral(owner), "neutral phase cannot retain dangling generation");
    owner.generation = 0;
    NativeFpScrub(&slots[0], sizeof(slots[0]));
    Check(!NativeFpOwns(owner, &slots[0], 17), "reaped generation invalidated");
    for (uint32_t i = 0; i < sizeof(slots[0]); ++i) Check(((uint8_t*)&slots[0])[i] == 0, "reaped record fully scrubbed");
    NativeFpBuildCanonical(clean, environment, 0xFFBF); NativeFpCopy(slots[0].image, clean);
    slots[0].generation = 18; slots[0].initialized = 1;
    Check(!NativeFpOwns(owner, &slots[0], 18), "reuse cannot inherit hardware owner");
    if (!failures) Print("PASS: native FP layout, capability/control policy, canonical image, MXCSR validation, pointer fidelity and generation ownership\n");
    return failures ? 1 : 0;
}
asm(".global _start\n_start:\n andl $-16, %esp\n call NativeFpTestsMain\n movl %eax, %ebx\n movl $1, %eax\n int $0x80\n");
