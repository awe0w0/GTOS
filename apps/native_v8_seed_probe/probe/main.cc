#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "heap.h"
#include "record.h"
#include "absl/random/internal/seed_material.h"
#include "src/base/platform/gtos-entropy.h"
#include "src/base/platform/gtos-raw-io.h"

#ifndef GTOS_SEED_MODE
#define GTOS_SEED_MODE 0
#endif
__attribute__((section(".data.file_record"))) volatile FileRecord native_file_record={1,GTOS_SEED_MODE};
extern "C" volatile unsigned native_v8_integration_stage,native_v8_integration_result;
extern "C" int __cxa_thread_atexit(void (*)(void*),void*,void*) noexcept;
extern "C" void* __dso_handle;
namespace {
    typedef unsigned U;
    FILE* buffered;
    char buffer[32];
    const uint32_t readonlyWords[2]={0x12345678,0x9abcdef0};
    void NeedAt(bool value,U line) {
        native_file_record.checks=native_file_record.checks+1;
        if (!value) { native_file_record.error=line;v8::base::gtos::ExitImmediately(91); }
    }
#define Need(value) NeedAt((value),__LINE__)
    struct Automatic {
        ~Automatic() { native_file_record.raw_calls=native_file_record.raw_calls+1; }
    };
    void TlsDestructor(void*) {
        Need(fflush(buffered)==0);native_file_record.bad_calls=native_file_record.bad_calls+1;
    }
    __attribute__((constructor)) void Initialize() { native_file_record.mask=native_file_record.mask+1; }
    __attribute__((destructor)) void Finalize() { native_file_record.ack=native_file_record.ack+1; }
    void Fill(uint8_t* data,U bytes) { for (U i=0;i<bytes;i++) data[i]=0x5a; }
    bool Equal(const uint8_t* data,U bytes) { for (U i=0;i<bytes;i++) if (data[i]!=0x5a) return false;return true; }
    bool Read(uint32_t* data,U words) {
        native_file_record.read_count=native_file_record.read_count+1;
        return absl::random_internal::ReadSeedMaterialFromOSEntropy(absl::Span<uint32_t>(data,words));
    }
}
extern "C" int NativeV8IsolateMain() {
    native_file_record.stage=1;Need(native_file_record.mask==1&&native_v8_integration_stage==2);
    char path[]="/native/buffer0";path[14]=char('0'+GTOS_SEED_MODE);
    buffered=fopen(path,"w");Need(buffered&&fileno(buffered)>0);native_file_record.file=static_cast<U>(fileno(buffered));
    Need(setvbuf(buffered,buffer,_IOFBF,sizeof(buffer))==0&&fwrite("buffered-payload",1,16,buffered)==16);
    char durable[]="/native/durable0";durable[15]=char('0'+GTOS_SEED_MODE);
    FILE* committed=fopen(durable,"w");Need(committed&&fileno(committed)>0);native_file_record.directory=static_cast<U>(fileno(committed));
    Need(fwrite("committed",1,9,committed)==9&&fflush(committed)==0);
    Need(__cxa_thread_atexit(TlsDestructor,nullptr,__dso_handle)==0&&malloc(48)!=nullptr);
    GtosVmRegionInfo heap={};Need(gtos_native_heap_query(&heap)==1&&heap.length==65536&&heap.resident_pages==16);
    native_file_record.base=heap.base;native_file_record.vm_handle=heap.handle;
    native_file_record.foreign=v8::base::gtos::CurrentProcessInfo().thread_id;
    native_file_record.command=reinterpret_cast<U>(&native_v8_integration_stage);
    native_file_record.result_address=reinterpret_cast<U>(&native_v8_integration_result);
    const bool have=v8::base::gtos::HasHardwareEntropy();native_file_record.feature=have?1U:0U;
    uint32_t values[8];Fill(reinterpret_cast<uint8_t*>(values),sizeof(values));
    Need(Read(values,8)==have);Need(Equal(reinterpret_cast<uint8_t*>(values),sizeof(values))==!have);
    Automatic automatic;native_file_record.stage=3;
#if GTOS_SEED_MODE == 0
    uint8_t* cross=reinterpret_cast<uint8_t*>(heap.base+4092);
    Fill(cross-4,2056);Need(Read(reinterpret_cast<uint32_t*>(cross),512)==have);
    Need(Equal(cross-4,4)&&Equal(cross+2048,4)&&Equal(cross,2048)==!have);
#elif GTOS_SEED_MODE == 1
    Need(Read(values,0)&&!Read(nullptr,1)&&!Read(values,0x40000000));
    Need(v8::base::gtos::ReadHardwareEntropy(nullptr,0)&&!v8::base::gtos::ReadHardwareEntropy(nullptr,1));
    Need(!v8::base::gtos::ReadHardwareEntropy(reinterpret_cast<void*>(0xfffffffcU),8));
    Need(!v8::base::gtos::ReadHardwareEntropy(reinterpret_cast<void*>(0x1000U),4));
    Need(!v8::base::gtos::ReadHardwareEntropy(reinterpret_cast<void*>(0xbffffffeU),4));
    uint8_t* bytes=reinterpret_cast<uint8_t*>(heap.base+8192);
    const U sizes[]={1,3,4,5,17,255,256};
    for (U size:sizes) {
        Fill(bytes,260);Need(v8::base::gtos::ReadHardwareEntropy(bytes+1,size)==have);
        Need(Equal(bytes,1)&&Equal(bytes+1+size,259-size)&&Equal(bytes+1,size)==!have);
    }
#elif GTOS_SEED_MODE == 2
    native_file_record.fault_address=heap.base+65536;
    Need(Read(reinterpret_cast<uint32_t*>(heap.base+65532),2)==have);
#elif GTOS_SEED_MODE == 3
    native_file_record.fault_address=reinterpret_cast<U>(readonlyWords);
    Need(Read(const_cast<uint32_t*>(readonlyWords),2)==have);
#elif GTOS_SEED_MODE == 4
    v8::base::gtos::ExitImmediately(29);
#else
    for (;;) {
        Need(Read(values,8)==have);native_file_record.progress=native_file_record.progress+1;v8::base::gtos::Yield();
    }
#endif
    return 17;
}
