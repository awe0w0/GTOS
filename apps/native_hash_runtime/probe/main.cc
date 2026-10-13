#include <algorithm>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <stdint.h>
#include <stdlib.h>
#include "heap.h"
#include <process/abi.h>
#include "record.h"
__attribute__((section(".data.file_record"))) volatile FileRecord native_file_record={1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
namespace {
    using U=unsigned;using B=unsigned char;
    static_assert(sizeof(size_t)==4,"actual IA32 hash selection");
    void* pressure[80];const U readonly_values[]={2,1};
    U (*volatile next_prime)(U)=std::__next_prime;
    decltype(&std::__hash_memory) volatile hash_memory=&std::__hash_memory;
    void (*volatile sort_unsigned)(U*,U*,std::__less<U>&)=std::__sort<std::__less<U>&,U*>;
    int Raw(U op,U address,U bytes) {
        U result=op;asm volatile("int $0x80":"+a"(result):"b"(address),"c"(bytes):"memory","cc");return (int)result;
    }
    [[noreturn]] void Fail(U line) {
        native_file_record.ack=line;native_file_record.error=native_file_record.checks;
        Raw(GTOS_SYS_EXIT,91,0);for (;;) asm volatile("ud2");
    }
    void NeedAt(bool value,U line) { native_file_record.checks=native_file_record.checks+1;if (!value) Fail(line); }
#define Need(value) NeedAt((value),__LINE__)
    bool Prime(U value) { if (value<2) return false;for (U p=2;p<=value/p;p++) if (!(value%p)) return false;return true; }
    U PrimeOracle(U value) { if (!value) return 0;while (!Prime(value)) value++;return value; }
    void PrimeChecks() {
        const U before=native_file_record.checks;
        for (U value=0;value<=4096;value++) Need(next_prime(value)==PrimeOracle(value));
        const U vectors[][2]={{65520,65521},{65521,65521},{65522,65537},{999999,1000003},{1000000,1000003},
            {2147483646,2147483647},{2147483647,2147483647},{4294967289U,4294967291U},{4294967291U,4294967291U}};
        for (const auto& row:vectors) Need(next_prime(row[0])==row[1]);
        native_file_record.mask=native_file_record.checks-before;Need(native_file_record.mask==4106);
    }
    void HashChecks() {
        const U before=native_file_record.checks;
        const U lengths[]={0,1,2,3,4,5,7,8,15,16,31,32,63,64};
        const U expected[]={0,0x1e4b54e2,0xa4de52eb,0x86e26bba,0xbb6e2e72,0x40811349,0xaf934b43,
            0x087510b6,0x39e13f57,0xb8f2f9bf,0xfc59a6e1,0x8e2d5b07,0xabc8e6d6,0x7259b293};
        static_assert(sizeof(lengths)==sizeof(expected),"independent fixed hash vectors");
        B storage[80];
        for (U offset=0;offset<8;offset++) {
            for (U i=0;i<64;i++) storage[offset+i]=(B)(i*37+17);
            for (U i=0;i<14;i++) Need(hash_memory(storage+offset,lengths[i])==expected[i]);
        }
        Need(hash_memory(nullptr,0)==0);
        B* edge=(B*)0x40040ff0;for (U i=0;i<16;i++) edge[i]=(B)(i*37+17);
        Need(hash_memory(edge,16)==0xb8f2f9bf);
        native_file_record.raw_calls=native_file_record.checks-before;Need(native_file_record.raw_calls==114);
    }
    void SortChecks() {
        const U before=native_file_record.checks;std::__less<U> less;
        U* values=(U*)malloc(256*sizeof(U));Need(values);
        const U counts[]={0,1,2,3,7,16,31,64,128,256};
        for (U count:counts) for (U pattern=0;pattern<5;pattern++) {
            uint64_t sum=0;U xor_value=0;
            for (U i=0;i<count;i++) {
                values[i]=pattern==0?i:pattern==1?count-i:pattern==2?7:pattern==3?(i%2?0xffffffffU:0U):(i*71U)%19U;
                sum+=values[i];xor_value^=values[i];
            }
            sort_unsigned(values,values+count,less);uint64_t after=0;U after_xor=0;
            for (U i=0;i<count;i++) { if (i) Need(values[i-1]<=values[i]);after+=values[i];after_xor^=values[i]; }
            Need(sum==after&&xor_value==after_xor);
            if (pattern<4) for (U i=0;i<count;i++) {
                U expected=pattern==0?i:pattern==1?i+1:pattern==2?7:(i<(count+1)/2?0U:0xffffffffU);
                Need(values[i]==expected);
            }
            if (pattern==4) for (U value=0;value<19;value++) {
                U old_count=0,new_count=0;
                for (U i=0;i<count;i++) { old_count+=((i*71U)%19U)==value;new_count+=values[i]==value; }
                Need(old_count==new_count);
            }
        }
        free(values);native_file_record.directory=native_file_record.checks-before;
    }
    struct MemoryHash { size_t operator()(U value) const { return hash_memory(&value,sizeof(value)); } };
    void ContainerChecks() {
        const U before=native_file_record.checks;
        {
            std::unordered_map<U,U,MemoryHash> map;std::unordered_set<U,MemoryHash> set;
            for (U i=0;i<257;i++) { Need(map.emplace(i,i*3+7).second);Need(set.insert(i).second); }
            map.rehash(521);set.reserve(521);Need(map.size()==257&&set.size()==257);
            for (U i=0;i<257;i++) { Need(!map.emplace(i,99).second&&!set.insert(i).second);Need(map.at(i)==i*3+7&&set.count(i)==1); }
            for (U i=0;i<257;i+=2) Need(map.erase(i)==1&&set.erase(i)==1);
            for (U i=0;i<257;i++) Need(map.count(i)==i%2&&set.count(i)==i%2);
            map.clear();set.clear();Need(map.empty()&&set.empty());
        }
        native_file_record.bad_calls=native_file_record.checks-before;GtosVmRegionInfo heap={};Need(gtos_native_heap_query(&heap)==0);
    }
    void HeapRecord() {
        GtosVmRegionInfo heap={};Need(gtos_native_heap_query(&heap)==1&&heap.base==0x80000000&&heap.resident_pages==16);
        native_file_record.base=heap.base;native_file_record.vm_handle=heap.handle;native_file_record.foreign=1;
    }
}
extern "C" int NativeV8IsolateMain() {
    Need(native_file_record.mode<8);PrimeChecks();HashChecks();SortChecks();ContainerChecks();
    const char marker[]="HASH RUNTIME CHECKS PASS\n";Need(Raw(GTOS_SYS_WRITE,(U)marker,sizeof(marker)-1)==(int)sizeof(marker)-1);
    const U mode=native_file_record.mode;
    if (!mode||mode==7) {
        if (mode==7) {
            U count=0;while (count<80) { void* value=malloc(1024);if (!value) break;pressure[count++]=value; }
            Need(count>0&&count<80);for (U i=0;i<count;i++) free(pressure[i]);ContainerChecks();
        }
        native_file_record.stage=2;return 0;
    }
    if (mode==1) { native_file_record.stage=3;(void)next_prime(0xfffffffcU);Fail(__LINE__); }
    if (mode==2) { native_file_record.stage=3;(void)hash_memory((B*)0x40040ff0,17);Fail(__LINE__); }
    if (mode==3) {
        native_file_record.progress=(U)readonly_values;native_file_record.stage=3;std::__less<U> less;
        sort_unsigned(const_cast<U*>(readonly_values),const_cast<U*>(readonly_values)+2,less);Fail(__LINE__);
    }
    if (mode==5) {
        U count=0;while (count<80) { void* value=malloc(1024);if (!value) break;pressure[count++]=value; }
        Need(count>0&&count<80);HeapRecord();native_file_record.stage=3;
        std::unordered_map<U,U,MemoryHash> map;for (U i=0;i<128;i++) map.emplace(i,i+1);Fail(__LINE__);
    }
    std::unordered_map<U,U,MemoryHash> retained;for (U i=0;i<128;i++) retained.emplace(i,i+1);
    B* bytes=(B*)malloc(4096);Need(bytes);for (U i=0;i<4096;i++) bytes[i]=(B)i;HeapRecord();native_file_record.stage=3;
    if (mode==6) { *(volatile U*)0x100000=1;Fail(__LINE__); }
    for (;;) { (void)hash_memory(bytes,4096);native_file_record.progress=native_file_record.progress+1; }
}
