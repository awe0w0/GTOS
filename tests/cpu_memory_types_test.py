#!/usr/bin/env python3
"""Exercise production memory-type logic with a substituted privileged backend.

The production i386 object is compiled separately without substitution. The host
suite replaces only CR0/EFLAGS/CPUID/RDMSR and the final cache-transition assembly;
all capability, snapshot, comparison, and fail-closed logic comes from the actual
source under test. This is not a replacement for privileged QEMU/hardware tests.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path,
                    default=root / "src/hardwarecommunication/cpu_memory_types.cpp")
parser.add_argument("--include", type=Path, action="append",
                    help="Include root (repeat as needed; default: repository include/)")
parser.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
args = parser.parse_args()
source_path = args.source.resolve()
includes = args.include or [root / "include"]
source = source_path.read_text()
if "wrmsr" in source.lower():
    raise SystemExit("The memory-type compatibility helper must never write MSRs")
start=source.index('    uint32_t Control0()')
end=source.index('    void Clear(',start)
if source[start:end].count('asm volatile') != 5:
    raise SystemExit('Privileged backend changed; review the test substitution')
source=source[:start]+'''    uint32_t Control0() { return mockCr0; }
    uint32_t Flags() { return mockFlags; }
    bool CpuidAvailable() { return mockCpuid; }
    void Cpuid(uint32_t leaf, uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
        ++cpuidReads; a=b=c=d=0;
        if (leaf == 0) a=mockMaxLeaf;
        else if (leaf == 1) d=mockFeatures;
        else if (leaf == 0x80000000U) a=mockExtended;
        else if (leaf == 0x80000008U) a=mockBits;
        else Unexpected("unexpected CPUID leaf");
    }
    uint64_t ReadMsr(uint32_t index) {
        ++msrReads;
        if (!(mockFeatures & FeatureMsr)) Unexpected("MSR absent");
        if (index == 0x277) {
            if (!(mockFeatures & FeaturePat)) Unexpected("PAT absent");
        } else if (!(mockFeatures & FeatureMtrr)) Unexpected("MTRR absent");
        if (index >= 0x300) Unexpected("MSR out of bounds");
        if (index >= 0x200 && index < 0x240
            && index >= 0x200 + (msrs[0xFE] & 0xFF) * 2)
            Unexpected("variable MTRR read exceeds advertised count");
        if ((index == 0x250 || index == 0x258 || index == 0x259
             || (index >= 0x268 && index <= 0x26F)) && !(msrs[0xFE] & 0x100))
            Unexpected("fixed MTRR read without capability");
        ++reads[index]; return msrs[index];
    }
''' + source[end:]
old='''    asm volatile("movl %0,%%cr0; wbinvd; movl %1,%%cr0"
                 : : "r"(noFill), "r"(enabled) : "memory");'''
if source.count(old) != 1:
    raise SystemExit('Cache transition changed; review the test substitution')
source=source.replace(old, '    MockCacheTransition(noFill, enabled);')
prefix='''#include <hardwarecommunication/cpu_memory_types.h>
extern "C" int puts(const char*);
extern "C" int printf(const char*, ...);
extern "C" void exit(int);
uint32_t mockCr0, mockFlags, mockFeatures, mockMaxLeaf, mockExtended, mockBits;
bool mockCpuid;
uint64_t msrs[0x300];
uint32_t reads[0x300], cpuidReads, msrReads, cacheChanges, assertions;
void Unexpected(const char* s) { puts(s); exit(1); }
void MockCacheTransition(uint32_t noFill,uint32_t enabled) {
    if ((noFill&0xE0000000U)!=0x40000000U) Unexpected("invalid no-fill state");
    if (enabled&0xE0000000U) Unexpected("invalid enabled state");
    if ((noFill&~0x60000000U)!=(mockCr0&~0x60000000U)) Unexpected("CR0 not preserved");
    if (enabled!=(mockCr0&~0x60000000U)) Unexpected("enabled CR0 not preserved");
    ++cacheChanges; mockCr0=enabled;
}
#define CHECK(x) do { ++assertions; if (!(x)) { puts("FAIL: " #x); exit(1); } } while (0)
void Reset() {
    mockCr0=0x10011; mockFlags=0; mockFeatures=0x11020; mockMaxLeaf=1;
    mockExtended=0x80000008U; mockBits=36; mockCpuid=true;
    cpuidReads=msrReads=cacheChanges=0;
    for(uint32_t i=0;i<0x300;++i) { msrs[i]=0; reads[i]=0; }
    msrs[0xFE]=0x508; msrs[0x2FF]=0xC06; msrs[0x277]=0x0007040600070406ULL;
    for(uint32_t i=0;i<8;++i) msrs[0x200+i*2]=((uint64_t)(i+1)<<21)|6;
    msrs[0x250]=msrs[0x258]=msrs[0x259]=0x0606060606060606ULL;
    for(uint32_t i=0;i<8;++i) msrs[0x268+i]=0x0606060606060606ULL;
}
'''
test='''
void SetVariable(uint32_t index, uint64_t base, uint64_t size, uint32_t type) {
    const uint64_t addressMask=((1ULL<<mockBits)-1)&~0xFFFULL;
    msrs[0x200+index*2]=base|type;
    msrs[0x201+index*2]=(addressMask&~(size-1))|0x800;
}
void SetByte(uint64_t& target,uint32_t byte,uint32_t value) {
    target=(target&~(0xFFULL<<(byte*8)))|((uint64_t)value<<(byte*8));
}
void CheckRangesAndEncodings() {
    CpuMemoryTypes bsp;
    Reset(); CHECK(CaptureMemoryTypes(bsp));
    CHECK(MemoryTypeRangeIsWriteBack(bsp,0x200001,8190));
    CHECK(!MemoryTypeRangeIsWriteBack(bsp,0,0));
    CHECK(!MemoryTypeRangeIsWriteBack(bsp,0xFFFFFFFEU,3));
    CHECK(MemoryTypeRangeIsWriteBack(bsp,0xFFFFFFFFU,1));
    CHECK(MemoryTypeRangeIsWriteBack(bsp,0xFFFFF000U,4096));
    uint32_t before=msrReads;
    CHECK(MemoryTypeRangeIsWriteBack(bsp,0x300000,4096));
    CHECK(msrReads==before && cacheChanges==0); // The range API is pure.

    for(uint32_t type=0;type<8;++type) if(type!=6) {
        Reset(); SetByte(msrs[0x277],0,type);
        CHECK(!CaptureMemoryTypes(bsp) && !bsp.valid);
    }
    for(uint32_t type=1;type<8;++type) {
        Reset(); SetByte(msrs[0x277],3,type);
        CHECK(!CaptureMemoryTypes(bsp) && !bsp.valid);
    }
    Reset(); msrs[0x277]|=1ULL<<63; CHECK(!CaptureMemoryTypes(bsp));
    Reset(); SetByte(msrs[0x277],5,2); CHECK(!CaptureMemoryTypes(bsp));
    Reset(); CHECK(CaptureMemoryTypes(bsp)); SetByte(msrs[0x277],0,1);
    SetByte(msrs[0x277],3,6); bsp.pat=msrs[0x277];
    CHECK(!MemoryTypeRangeIsWriteBack(bsp,0x200000,4096));
    CHECK(!PrepareApplicationProcessorMemory(bsp) && cacheChanges==0);
    Reset(); mockFeatures&=~0x1000U; CHECK(CaptureMemoryTypes(bsp));
    CHECK(MemoryTypeRangeIsWriteBack(bsp,0x200000,8192));
    Reset(); mockFeatures=0; CHECK(CaptureMemoryTypes(bsp));
    CHECK(MemoryTypeRangeIsWriteBack(bsp,0x200000,8192));
    Reset(); mockFeatures&=~0x10000U; CHECK(CaptureMemoryTypes(bsp));
    CHECK(MemoryTypeRangeIsWriteBack(bsp,0x200000,8192));

    // Exercise every field in all 11 fixed MTRRs, including both sizes and all
    // boundaries where the addressing formula changes (80000/A0000/C0000).
    uint32_t fixedIndex[11]={0x250,0x258,0x259,0x268,0x269,0x26A,
                            0x26B,0x26C,0x26D,0x26E,0x26F};
    for(uint32_t reg=0;reg<11;++reg) for(uint32_t byte=0;byte<8;++byte) {
        Reset(); SetByte(msrs[fixedIndex[reg]],byte,0); CHECK(CaptureMemoryTypes(bsp));
        uint32_t size=reg==0?0x10000:(reg<3?0x4000:0x1000);
        uint32_t base=reg==0?0:(reg<3?0x80000+(reg-1)*0x20000:0xC0000+(reg-3)*0x8000);
        uint32_t address=base+byte*size;
        CHECK(!MemoryTypeRangeIsWriteBack(bsp,address,1));
        CHECK(!MemoryTypeRangeIsWriteBack(bsp,address+size-1,1));
        CHECK(MemoryTypeRangeIsWriteBack(bsp,address+size,1));
        if(address) {
            CHECK(MemoryTypeRangeIsWriteBack(bsp,address-1,1));
            CHECK(!MemoryTypeRangeIsWriteBack(bsp,address-1,2));
        }
    }
    Reset(); msrs[0x250]=0; msrs[0x2FF]&=~0x400ULL; CHECK(CaptureMemoryTypes(bsp));
    CHECK(MemoryTypeRangeIsWriteBack(bsp,0,0x80000)); // FE=0 ignores fixed UC.
    Reset(); msrs[0xFE]&=~0x100ULL; msrs[0x2FF]&=~0x400ULL;
    CHECK(CaptureMemoryTypes(bsp)); CHECK(MemoryTypeRangeIsWriteBack(bsp,0,0x100000));
    Reset(); SetVariable(0,0,0x100000,0); CHECK(CaptureMemoryTypes(bsp));
    CHECK(MemoryTypeRangeIsWriteBack(bsp,0,0x100000)); // Fixed WB overrides variable UC.
    msrs[0x2FF]&=~0x400ULL; CHECK(CaptureMemoryTypes(bsp));
    CHECK(!MemoryTypeRangeIsWriteBack(bsp,0,4096));

    Reset(); msrs[0x2FF]=0x800; SetVariable(0,0x400000,0x200000,6);
    CHECK(CaptureMemoryTypes(bsp));
    CHECK(MemoryTypeRangeIsWriteBack(bsp,0x400000,0x200000));
    CHECK(MemoryTypeRangeIsWriteBack(bsp,0x400001,0x1FFFFE));
    CHECK(!MemoryTypeRangeIsWriteBack(bsp,0x3FFFFF,2));
    CHECK(!MemoryTypeRangeIsWriteBack(bsp,0x5FFFFF,2));
    CHECK(!MemoryTypeRangeIsWriteBack(bsp,0x600000,1));
    SetVariable(1,0x500000,4096,0); CHECK(CaptureMemoryTypes(bsp));
    CHECK(!MemoryTypeRangeIsWriteBack(bsp,0x400000,0x200000));
    CHECK(MemoryTypeRangeIsWriteBack(bsp,0x400000,0x100000));
    CHECK(MemoryTypeRangeIsWriteBack(bsp,0x501000,0xFF000));

    // All ordered combinations distinguish defined overlaps from unsupported
    // combinations. Probe the production resolver too, since every non-WB
    // result intentionally has the same false result from the public predicate.
    uint32_t legalTypes[5]={0,1,4,5,6};
    for(uint32_t a=0;a<5;++a) for(uint32_t b=0;b<5;++b) {
        Reset(); msrs[0x2FF]=0x806;
        SetVariable(0,0x400000,0x200000,legalTypes[a]);
        SetVariable(1,0x400000,0x200000,legalTypes[b]);
        CHECK(CaptureMemoryTypes(bsp));
        uint32_t expected=0xFF;
        if(legalTypes[a]==legalTypes[b]) expected=legalTypes[a];
        else if(legalTypes[a]==0 || legalTypes[b]==0) expected=0;
        else if((legalTypes[a]==4&&legalTypes[b]==6)||(legalTypes[a]==6&&legalTypes[b]==4)) expected=4;
        CHECK(MemoryTypeAt(bsp,0x400000)==expected);
        CHECK(MemoryTypeRangeIsWriteBack(bsp,0x400000,4096)==(expected==6));
    }
    for(uint32_t uc=0;uc<3;++uc) {
        Reset(); msrs[0x2FF]=0x806;
        SetVariable(uc,0x400000,0x200000,0);
        SetVariable((uc+1)%3,0x400000,0x200000,1);
        SetVariable((uc+2)%3,0x400000,0x200000,6);
        CHECK(CaptureMemoryTypes(bsp)); CHECK(MemoryTypeAt(bsp,0x400000)==0);
        CHECK(!MemoryTypeRangeIsWriteBack(bsp,0x400000,4096));
    }
    for(uint32_t i=0;i<5;++i) {
        Reset(); msrs[0x2FF]=0x800|legalTypes[i]; CHECK(CaptureMemoryTypes(bsp));
        CHECK(MemoryTypeRangeIsWriteBack(bsp,0x400000,4096)==(legalTypes[i]==6));
    }
    Reset(); msrs[0x2FF]&=~0x800ULL; CHECK(CaptureMemoryTypes(bsp));
    CHECK(!MemoryTypeRangeIsWriteBack(bsp,0x200000,4096)); // Disabled MTRRs are UC.
    CHECK(!MemoryTypeRangeIsWriteBack(bsp,0,4096));
    Reset(); SetVariable(0,0x400000,0x200000,1); msrs[0x201]&=~0x800ULL;
    CHECK(CaptureMemoryTypes(bsp)); CHECK(MemoryTypeRangeIsWriteBack(bsp,0x400000,4096));
    Reset(); msrs[0x2FF]=0x806; SetVariable(0,1ULL<<32,0x200000,1);
    CHECK(CaptureMemoryTypes(bsp)); CHECK(MemoryTypeRangeIsWriteBack(bsp,0,4096));
    CHECK(MemoryTypeAt(bsp,1ULL<<32)==1);
    Reset(); msrs[0x2FF]=0x800; SetVariable(0,0,1ULL<<36,6);
    CHECK(CaptureMemoryTypes(bsp)); CHECK(MemoryTypeRangeIsWriteBack(bsp,0xFFFFF000U,4096));

    // Reserved bits/types, unsupported WC, sparse masks, and misaligned bases
    // must not enter an allegedly valid snapshot in the first place.
    for(uint32_t scenario=0;scenario<14;++scenario) {
        Reset(); SetVariable(0,0x400000,0x200000,6);
        if(scenario==0) msrs[0x200]|=0x100;
        if(scenario==1) msrs[0x201]|=1;
        if(scenario==2) msrs[0x200]|=1ULL<<36;
        if(scenario==3) msrs[0x201]|=1ULL<<36;
        if(scenario==4) SetByte(msrs[0x200],0,2);
        if(scenario==5) SetByte(msrs[0x250],0,7);
        if(scenario==6) msrs[0x2FF]|=0x100;
        if(scenario==7) msrs[0x2FF]|=1ULL<<32;
        if(scenario==8) msrs[0xFE]&=~0x100ULL; // FE set without FIX.
        if(scenario==9) msrs[0x201]^=1U<<16; // Sparse active mask.
        if(scenario==10) msrs[0x200]|=4096; // Misaligned active range.
        if(scenario==11) { msrs[0xFE]&=~0x400ULL; SetByte(msrs[0x200],0,1); }
        if(scenario==12) { msrs[0xFE]&=~0x400ULL; SetByte(msrs[0x250],0,1); }
        if(scenario==13) { msrs[0xFE]&=~0x400ULL; SetByte(msrs[0x2FF],0,1); }
        CHECK(!CaptureMemoryTypes(bsp) && !bsp.valid && cacheChanges==0);
    }
    Reset(); CHECK(CaptureMemoryTypes(bsp)); bsp.variableCount=33;
    CHECK(!MemoryTypeRangeIsWriteBack(bsp,0x200000,4096));
    Reset(); CHECK(CaptureMemoryTypes(bsp)); bsp.physicalAddressBits=64;
    CHECK(!MemoryTypeRangeIsWriteBack(bsp,0x200000,4096));
    Reset(); CHECK(CaptureMemoryTypes(bsp)); bsp.valid=0;
    CHECK(!MemoryTypeRangeIsWriteBack(bsp,0x200000,4096));
}
int main() {
    CpuMemoryTypes bsp;
    Reset(); CHECK(CaptureMemoryTypes(bsp)); CHECK(bsp.valid==1);
    CHECK(bsp.variableCount==8 && bsp.physicalAddressBits==36);
    CHECK(msrReads==30 && reads[0x277]==1 && reads[0x26F]==1);
    mockCr0 |= 0x60000000U; CHECK(PrepareApplicationProcessorMemory(bsp));
    CHECK(mockCr0==0x10011 && cacheChanges==1);

    // Every implemented register, including both halves and disabled ranges,
    // must be compared before the first cache-control change.
    uint32_t indices[30]={0xFE,0x2FF,0x277,0x250,0x258,0x259,0x268,0x269,
        0x26A,0x26B,0x26C,0x26D,0x26E,0x26F,0x200,0x201,0x202,0x203,
        0x204,0x205,0x206,0x207,0x208,0x209,0x20A,0x20B,0x20C,0x20D,0x20E,0x20F};
    for(uint32_t i=0;i<30;++i) for(uint32_t half=0;half<2;++half) {
        Reset(); CHECK(CaptureMemoryTypes(bsp)); msrs[indices[i]] ^= 1ULL<<(half*32);
        mockCr0 |= 0x60000000U; CHECK(!PrepareApplicationProcessorMemory(bsp));
        CHECK(cacheChanges==0 && mockCr0==0x60010011U);
    }
    Reset(); mockFeatures=0x10020; CHECK(CaptureMemoryTypes(bsp)); CHECK(msrReads==1);
    CHECK(bsp.variableCount==0 && bsp.defaultType==0 && bsp.capability==0);
    mockCr0|=0x60000000U; CHECK(PrepareApplicationProcessorMemory(bsp));
    Reset(); mockFeatures=0x1020; CHECK(CaptureMemoryTypes(bsp)); CHECK(reads[0x277]==0);
    CHECK(PrepareApplicationProcessorMemory(bsp));
    Reset(); mockFeatures=0; CHECK(CaptureMemoryTypes(bsp)); CHECK(msrReads==0);
    CHECK(PrepareApplicationProcessorMemory(bsp)); CHECK(msrReads==0 && cacheChanges==1);
    Reset(); mockFeatures=0x11000; CHECK(!CaptureMemoryTypes(bsp)); CHECK(!bsp.valid && msrReads==0);
    Reset(); mockCpuid=false; CHECK(!CaptureMemoryTypes(bsp)); CHECK(cpuidReads==0 && msrReads==0);
    Reset(); mockMaxLeaf=0; CHECK(!CaptureMemoryTypes(bsp)); CHECK(msrReads==0);
    Reset(); msrs[0xFE]=0x520; CHECK(CaptureMemoryTypes(bsp)); CHECK(bsp.variableCount==32);
    CHECK(reads[0x23F]==1 && PrepareApplicationProcessorMemory(bsp));
    Reset(); msrs[0xFE]=0x521; CHECK(!CaptureMemoryTypes(bsp)); CHECK(msrReads==1 && !bsp.valid);
    Reset(); msrs[0xFE]|=0x200; CHECK(!CaptureMemoryTypes(bsp)); CHECK(msrReads==1);
    Reset(); msrs[0xFE]|=1ULL<<32; CHECK(!CaptureMemoryTypes(bsp)); CHECK(msrReads==1);
    Reset(); msrs[0xFE]=0; msrs[0x2FF]=0x806; CHECK(CaptureMemoryTypes(bsp)); CHECK(msrReads==3);
    CHECK(PrepareApplicationProcessorMemory(bsp));
    Reset(); mockExtended=0x80000000U; CHECK(CaptureMemoryTypes(bsp)); CHECK(bsp.physicalAddressBits==36);
    Reset(); mockBits=31; CHECK(!CaptureMemoryTypes(bsp)); CHECK(msrReads==0);
    Reset(); mockBits=53; CHECK(!CaptureMemoryTypes(bsp)); CHECK(msrReads==0);
    uint32_t modes[]={0x80010011U,0x20010011U,0x40010011U,0x60010011U,0x10010U};
    for(uint32_t i=0;i<5;++i) {
        Reset(); bsp.valid=1; mockCr0=modes[i]; CHECK(!CaptureMemoryTypes(bsp));
        CHECK(!bsp.valid && msrReads==0 && cacheChanges==0 && mockCr0==modes[i]);
    }
    for(uint32_t caseId=0;caseId<7;++caseId) {
        Reset(); CHECK(CaptureMemoryTypes(bsp)); mockCr0|=0x60000000U;
        if(caseId==0) mockFlags=0x200;
        if(caseId==1) mockCr0|=0x80000000U;
        if(caseId==2) mockCr0&=~1U;
        if(caseId==3) bsp.valid=0;
        if(caseId==4) mockFeatures^=0x10000;
        if(caseId==5) mockBits=40;
        if(caseId==6) bsp.variableCount=0xFFFFFFFFU;
        CHECK(!PrepareApplicationProcessorMemory(bsp)); CHECK(cacheChanges==0);
    }
    Reset(); mockFeatures=0x20; CHECK(CaptureMemoryTypes(bsp)); CHECK(msrReads==0);
    CHECK(PrepareApplicationProcessorMemory(bsp));
    Reset(); CHECK(CaptureMemoryTypes(bsp)); mockFeatures&=~0x1000U;
    CHECK(!PrepareApplicationProcessorMemory(bsp) && cacheChanges==0);
    Reset(); mockFeatures&=~0x1000U; CHECK(CaptureMemoryTypes(bsp)); mockFeatures|=0x1000U;
    CHECK(!PrepareApplicationProcessorMemory(bsp) && cacheChanges==0);
    Reset(); CHECK(CaptureMemoryTypes(bsp)); mockCpuid=false;
    CHECK(!PrepareApplicationProcessorMemory(bsp) && cacheChanges==0);
    Reset(); CHECK(CaptureMemoryTypes(bsp)); bsp.valid=2;
    CHECK(!PrepareApplicationProcessorMemory(bsp) && cacheChanges==0);
    Reset(); msrs[0xFE]=0x520; CHECK(CaptureMemoryTypes(bsp)); msrs[0x23F]^=1ULL<<63;
    CHECK(!PrepareApplicationProcessorMemory(bsp) && cacheChanges==0);
    Reset(); mockBits=32; CHECK(CaptureMemoryTypes(bsp)); CHECK(PrepareApplicationProcessorMemory(bsp));
    Reset(); mockBits=52; CHECK(CaptureMemoryTypes(bsp)); CHECK(PrepareApplicationProcessorMemory(bsp));
    CheckRangesAndEncodings();
    printf("memory-type mock checks passed: %u assertions, including 60 MSR mismatches\\n", assertions);
    return 0;
}
'''
if "asm volatile" in source:
    raise SystemExit("Unsubstituted assembly remains; review the privileged backend")
compiler = shlex.split(args.cxx)
common = (root / "tools/kernel-cxxflags").read_text().split() + ["-O2", "-std=c++11", "-Wall", "-Wextra", "-Werror"]
include_flags = [flag for path in includes for flag in ["-I", str(path.resolve())]]
with tempfile.TemporaryDirectory(prefix="gtos-memory-types-") as directory:
    build = Path(directory)
    production_object = build / "cpu_memory_types.o"
    subprocess.run(compiler + common + ["-m32", "-ffreestanding", "-fno-exceptions",
                   "-fno-rtti", "-fno-pie", "-fno-stack-protector"] + include_flags
                   + ["-c", str(source_path), "-o", str(production_object)], check=True)
    undefined = subprocess.check_output(["nm", "-u", str(production_object)], text=True)
    if undefined.strip():
        raise SystemExit("Unexpected production-object dependencies:\n" + undefined)
    print("production i386 compile passed; no undefined symbols", flush=True)
    mock_source = build / "cpu_memory_types_mock.cpp"
    mock_source.write_text(prefix + source + test)
    executable = build / "cpu_memory_types_mock"
    subprocess.run(compiler + common + include_flags
                   + [str(mock_source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
