#ifndef __GTOS__PROCESS__NATIVE_FP_H
#define __GTOS__PROCESS__NATIVE_FP_H
#include <common/types.h>
namespace gtos { namespace memory { class KernelPaging; } namespace process {
    enum NativeFpPolicy : uint32_t { NativeFpDisabled, NativeFpSse2, NativeFpSse3 };
    enum NativeFpError : uint32_t {
        NativeFpOk, NativeFpBadPolicy, NativeFpNoCpuid, NativeFpMissingFeature,
        NativeFpExtendedState, NativeFpBadBuffer, NativeFpBadContext,
        NativeFpControlFailure, NativeFpCanonicalFailure
    };
    enum NativeFpPhase : uint32_t {
        NativeFpOff, NativeFpKernelNeutral, NativeFpUserLive,
        NativeFpSavingUser, NativeFpRestoringUser, NativeFpFailed
    };
    struct NativeFpCapabilities { bool cpuid; uint32_t maximumLeaf, edx, ecx, cr4; };
    struct NativeFpContext {
        uint8_t fx[512];
        uint8_t env[28];
        uint8_t padding[4];
    } __attribute__((aligned(16)));
    struct NativeFpRecord {
        NativeFpContext image;
        uint32_t generation, initialized;
    } __attribute__((aligned(16)));
    // The assembly return helper consumes this trusted, resident descriptor.
    // There is no user pointer or user-controlled state import interface.
    struct NativeFpTransition {
        volatile uint32_t phase;
        NativeFpRecord* owner;
        uint32_t generation;
        volatile uint32_t restores;
    };
    struct NativeFpStatistics {
        uint32_t saves, restores, initialized, invalidated, invariantFailures;
        uint32_t userKeyboardInterrupts, userMouseInterrupts;
    };
    static_assert(sizeof(NativeFpContext) == 544, "legacy FP payload size");
    static_assert(__alignof__(NativeFpContext) == 16, "FXSAVE alignment");
    static_assert(__builtin_offsetof(NativeFpContext, env) == 512, "x87 environment offset");
    static_assert(sizeof(NativeFpRecord) == 560, "aligned slot stride");
    static_assert(__builtin_offsetof(NativeFpRecord, image) == 0, "restore image offset");
    static_assert(__builtin_offsetof(NativeFpTransition, owner) == 4, "assembly owner offset");
    static_assert(__builtin_offsetof(NativeFpTransition, restores) == 12, "assembly counter offset");
    static_assert(NativeFpUserLive == 2, "assembly live phase");

    // Pure policy helpers shared by production and freestanding source tests.
    inline NativeFpError ValidateNativeFpCapabilities(const NativeFpCapabilities& c, NativeFpPolicy p) {
        if (p == NativeFpDisabled) return NativeFpOk;
        if (p != NativeFpSse2 && p != NativeFpSse3) return NativeFpBadPolicy;
        if (!c.cpuid || c.maximumLeaf < 1) return NativeFpNoCpuid;
        const uint32_t required = (1U << 0) | (1U << 23) | (1U << 24) | (1U << 25) | (1U << 26);
        if ((c.edx & required) != required || (p == NativeFpSse3 && !(c.ecx & 1)))
            return NativeFpMissingFeature;
        return c.cr4 & (1U << 18) ? NativeFpExtendedState : NativeFpOk;
    }
    inline uint32_t NativeFpKernelCr0(uint32_t value) { return (value | 0x2AU) & ~4U; }
    inline uint32_t NativeFpCr4(uint32_t value) { return value | 0x600U; }
    // MXCSR_MASK is a 32-bit hardware capability field. AMD MisAlignSse uses
    // bit17; clipping to 16 bits can reject legal saved user state on return.
    inline uint32_t NativeFpMxcsrMask(uint32_t reported) { return reported ? reported : 0xFFBFU; }
    inline uint16_t NativeFpGet16(const uint8_t* p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }
    inline uint32_t NativeFpGet32(const uint8_t* p) { return NativeFpGet16(p) | ((uint32_t)NativeFpGet16(p + 2) << 16); }
    inline void NativeFpPut16(uint8_t* p, uint16_t n) { p[0] = (uint8_t)n; p[1] = (uint8_t)(n >> 8); }
    inline void NativeFpPut32(uint8_t* p, uint32_t n) { NativeFpPut16(p, (uint16_t)n); NativeFpPut16(p + 2, (uint16_t)(n >> 16)); }
    inline void NativeFpScrub(void* p, uint32_t n) {
        volatile uint8_t* bytes = (volatile uint8_t*)p;
        while (n--) *bytes++ = 0;
    }
    inline void NativeFpCopy(NativeFpContext& to, const NativeFpContext& from) {
        for (uint32_t i = 0; i < sizeof(to); ++i) ((uint8_t*)&to)[i] = ((const uint8_t*)&from)[i];
    }
    inline void NativeFpPatchPointers(NativeFpContext& image) {
        NativeFpPut16(image.fx + 6, NativeFpGet16(image.env + 18) & 0x7FFU);
        NativeFpPut32(image.fx + 8, NativeFpGet32(image.env + 12));
        NativeFpPut16(image.fx + 12, NativeFpGet16(image.env + 16));
        NativeFpPut32(image.fx + 16, NativeFpGet32(image.env + 20));
        NativeFpPut16(image.fx + 20, NativeFpGet16(image.env + 24));
    }
    inline void NativeFpBuildCanonical(NativeFpContext& image, const uint8_t* cleanEnvironment, uint32_t mask) {
        NativeFpScrub(&image, sizeof(image));
        // FNINIT/FNSTENV provides only reserved representation, never inherited
        // payloads. All defined user-visible fields are built independently.
        for (uint32_t i = 0; i < 28; ++i) image.env[i] = cleanEnvironment[i];
        NativeFpPut16(image.env, 0x37F); NativeFpPut16(image.env + 4, 0);
        NativeFpPut16(image.env + 8, 0xFFFF);
        for (uint32_t i = 12; i < 26; ++i) image.env[i] = 0;
        NativeFpPut16(image.fx, 0x37F);
        NativeFpPut32(image.fx + 24, 0x1F80); NativeFpPut32(image.fx + 28, mask);
    }
    inline bool NativeFpValidImage(const NativeFpContext& image, uint32_t mask) {
        return !((uint32_t)&image & 15U) && !(NativeFpGet32(image.fx + 24) & ~mask)
            && NativeFpGet16(image.fx) == NativeFpGet16(image.env)
            && NativeFpGet16(image.fx + 2) == NativeFpGet16(image.env + 4)
            && NativeFpGet16(image.fx + 6) == (NativeFpGet16(image.env + 18) & 0x7FFU)
            && NativeFpGet32(image.fx + 8) == NativeFpGet32(image.env + 12)
            && NativeFpGet16(image.fx + 12) == NativeFpGet16(image.env + 16)
            && NativeFpGet32(image.fx + 16) == NativeFpGet32(image.env + 20)
            && NativeFpGet16(image.fx + 20) == NativeFpGet16(image.env + 24);
    }
    inline bool NativeFpOwns(const NativeFpTransition& t, const NativeFpRecord* r, uint32_t generation) {
        return r && generation && r->initialized == 1 && r->generation == generation
            && t.owner == r && t.generation == generation;
    }
    inline bool NativeFpNeutral(const NativeFpTransition& t) {
        return t.phase == NativeFpKernelNeutral && !t.owner && !t.generation;
    }
    inline bool NativeFpCanEnter(const NativeFpTransition& t, const NativeFpRecord* r, uint32_t generation) {
        return t.phase == NativeFpUserLive && NativeFpOwns(t, r, generation);
    }
    inline bool NativeFpCanInitialize(const NativeFpTransition& t, const NativeFpRecord& r, uint32_t generation) {
        return NativeFpNeutral(t) && !r.initialized && generation;
    }
    inline bool NativeFpCanReturn(const NativeFpTransition& t, const NativeFpRecord* r, uint32_t generation) {
        return NativeFpNeutral(t) && r && r->initialized == 1 && generation && r->generation == generation;
    }
    class NativeFp {
        NativeFpTransition transition;
        NativeFpContext canonical, scratch;
        NativeFpRecord* records[4];
        uint32_t count, mask, saves, initializations, invalidations, failures;
        uint32_t userKeyboardInterrupts, userMouseInterrupts;
        NativeFpPolicy policy;
        NativeFpError error;
        bool Registered(const NativeFpRecord*) const;
        bool Controls(bool user) const;
        NativeFp(const NativeFp&);
        NativeFp& operator=(const NativeFp&);
    public:
        NativeFp();
        // Caller has verified BSP, IF=0, sealed kernel CR3 and runtime lifetime.
        bool PrepareBsp(NativeFpPolicy, memory::KernelPaging&, NativeFpRecord* const*, uint32_t count);
        bool Initialize(NativeFpRecord&, uint32_t generation);
        void Invalidate(NativeFpRecord&);
        void EnterKernel(NativeFpRecord*, uint32_t generation, bool user, uint32_t vector);
        NativeFpTransition* PrepareReturn(NativeFpRecord*, uint32_t generation, bool user);
        bool Enabled() const { return policy != NativeFpDisabled; }
        NativeFpError Error() const { return error; }
        NativeFpStatistics Statistics() const;
        void Panic(const char* reason) __attribute__((noreturn));
    };
} }
extern "C" void native_fp_probe_asm(gtos::process::NativeFpContext*);
extern "C" void native_fp_save_asm(gtos::process::NativeFpContext*);
extern "C" void native_fp_neutral_asm(const gtos::process::NativeFpContext*);
extern "C" void native_fp_restore_asm(gtos::process::NativeFpTransition*);
#endif
