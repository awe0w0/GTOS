#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include "heap.h"
#include "record.h"
#include "fixture.h"
#include "third_party/zlib/gzguts.h"
#include "src/base/platform/gtos-raw-io.h"
#include "src/base/platform/gtos-native-services.h"

#ifndef GTOS_ZLIB_MODE
#define GTOS_ZLIB_MODE 0
#endif
__attribute__((section(".data.file_record"))) volatile FileRecord native_file_record={1,GTOS_ZLIB_MODE,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
extern "C" volatile unsigned native_v8_integration_stage,native_v8_integration_result;
extern "C" int __cxa_thread_atexit(void (*)(void*),void*,void*) noexcept;
extern "C" void* __dso_handle;
namespace {
    typedef unsigned U;
    FILE* buffered;
    char fileBuffer[32];
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
    int Read(gzFile file,void* buffer,U bytes) {
        native_file_record.read_count=native_file_record.read_count+1;
        return gzread(file,buffer,bytes);
    }
    void Bytes(const unsigned char* bytes,U offset,U count) {
        for (U i=0;i<count;i++) Need(bytes[i]==FixtureByte(offset+i));
    }
    gzFile Open(const char* path,const char* mode) {
        gzFile file=gzopen(path,mode);Need(file&&gzbuffer(file,512)==0);return file;
    }
    __attribute__((unused)) void Decode(gzFile file) {
        unsigned char bytes[1024];U count=0;uLong checksum=adler32(0,nullptr,0);
        for (;;) {
            int got=Read(file,bytes,sizeof(bytes));Need(got>=0);
            if (!got) break;
            Need(count+static_cast<U>(got)<=FixtureBytes);Bytes(bytes,count,static_cast<U>(got));
            checksum=adler32(checksum,bytes,static_cast<U>(got));count+=static_cast<U>(got);
        }
        Need(count==FixtureBytes&&checksum==FixtureAdler&&gzeof(file));
        native_file_record.feature=static_cast<U>(checksum);
        Need(gzrewind(file)==0&&gzseek(file,1234,SEEK_SET)==1234);
        Need(Read(file,bytes,127)==127);Bytes(bytes,1234,127);
    }
    struct AllocationTrace {
        void* pointers[5];U bytes[5],calls,freed;
        alloc_func allocate;free_func release;voidpf opaque;
    };
    __attribute__((unused)) voidpf ObserveAllocate(voidpf opaque,uInt items,uInt size) {
        AllocationTrace* trace=static_cast<AllocationTrace*>(opaque);
        Need(trace->calls<5&&items&&size&&items<=UINT32_MAX/size);
        U index=trace->calls++;trace->bytes[index]=items*size;
        trace->pointers[index]=trace->allocate(trace->opaque,items,size);
        native_file_record.codec_calls=trace->calls;
        native_file_record.codec_bytes=native_file_record.codec_bytes+trace->bytes[index];
        if (!trace->pointers[index]) native_file_record.codec_failed=trace->bytes[index];
        return trace->pointers[index];
    }
    __attribute__((unused)) void ObserveFree(voidpf opaque,voidpf pointer) {
        AllocationTrace* trace=static_cast<AllocationTrace*>(opaque);bool found=false;
        for (U i=0;i<trace->calls;i++) {
            if (pointer&&trace->pointers[i]==pointer) { trace->pointers[i]=nullptr;found=true;break; }
        }
        Need(found);trace->freed=trace->freed+1;native_file_record.codec_frees=trace->freed;
        trace->release(trace->opaque,pointer);
    }
    __attribute__((unused)) U LargestAllocation() {
        U low=0,high=GTOS_NATIVE_HEAP_BYTES-GTOS_NATIVE_HEAP_HEADER_BYTES;
        while (low<high) {
            U middle=low+(high-low+1)/2;void* pointer=malloc(middle);
            if (pointer) { free(pointer);low=middle; } else high=middle-1;
        }
        return low;
    }
    __attribute__((unused)) void CompressionLimit() {
        z_stream stream={};errno=0;native_file_record.codec_before=LargestAllocation();errno=0;
        int result=deflateInit2(&stream,6,Z_DEFLATED,16+9,1,Z_DEFAULT_STRATEGY);
        native_file_record.codec_result=static_cast<U>(result);
        Need(result==Z_MEM_ERROR&&errno==ENOMEM&&stream.state==nullptr);
        AllocationTrace trace={};trace.allocate=stream.zalloc;trace.release=stream.zfree;trace.opaque=stream.opaque;
        Need(trace.allocate&&trace.release);
        stream.zalloc=ObserveAllocate;stream.zfree=ObserveFree;stream.opaque=&trace;
        errno=0;Need(deflateInit2(&stream,6,Z_DEFLATED,16+9,1,Z_DEFAULT_STRATEGY)==result
            &&errno==ENOMEM&&stream.state==nullptr);
        Need(trace.calls==5&&trace.freed==4&&native_file_record.codec_failed==65536);
        Need(trace.bytes[0]==5924&&trace.bytes[1]==1040&&trace.bytes[2]==1024
            &&trace.bytes[3]==65536&&trace.bytes[4]==640&&native_file_record.codec_bytes==74164);
        for (U i=0;i<trace.calls;i++) Need(trace.pointers[i]==nullptr);
        native_file_record.codec_after=LargestAllocation();
        Need(native_file_record.codec_before>32768&&native_file_record.codec_after==native_file_record.codec_before);
        unsigned char* recovered=static_cast<unsigned char*>(malloc(native_file_record.codec_after));Need(recovered);
        recovered[0]=0x5a;recovered[native_file_record.codec_after-1]=0xa5;
        Need(recovered[0]==0x5a&&recovered[native_file_record.codec_after-1]==0xa5);free(recovered);
    }
    __attribute__((unused)) void ShortIo() {
        unsigned char* bytes=static_cast<unsigned char*>(malloc(8192));Need(bytes);
        for (U i=0;i<8192;i++) bytes[i]=FixtureByte(i);
        gzFile writer=Open("/native/short.bin","wT");
        Need(gzwrite(writer,bytes,8192)==8192&&gzclose(writer)==Z_OK);
        int handle=gtos_zlib_open("/native/short.bin",O_RDONLY,0);Need(handle>0);
        Need(gtos_zlib_read(handle,bytes,8192)==4096);Bytes(bytes,0,4096);
        Need(gtos_zlib_read(handle,bytes,8192)==4096);Bytes(bytes,4096,4096);
        Need(gtos_zlib_seek(handle,-3,SEEK_END)==8189);
        Need(gtos_zlib_read(handle,bytes,8)==3);Bytes(bytes,8189,3);
        Need(gtos_zlib_read(handle,bytes,1)==0&&gtos_zlib_close(handle)==0);free(bytes);
    }
    __attribute__((unused)) void AppendStream(unsigned char* bytes) {
        gzFile file=Open("/native/append.gz","r");U count=0;
        for (U attempts=0;attempts<32;attempts++) {
            int got=Read(file,bytes,1024);Need(got>=0);
            if (!got) break;
            Need(count+static_cast<U>(got)<=FixtureBytes);Bytes(bytes,count,static_cast<U>(got));
            count+=static_cast<U>(got);
        }
        int error=0;Need(count==FixtureBytes&&gzeof(file)&&gzerror(file,&error)&&error==Z_BUF_ERROR);
        int writer=gtos_zlib_open("/native/append.gz",O_WRONLY,0);Need(writer>0);
        Need(gtos_zlib_seek(writer,0,SEEK_END)==static_cast<int64_t>(sizeof(FixtureGzip)-4));
        Need(gtos_zlib_write(writer,FixtureGzip+sizeof(FixtureGzip)-4,4)==4&&gtos_zlib_close(writer)==0);
        gzclearerr(file);int got=Read(file,bytes,1);Need(gzerror(file,&error));
        if (got!=0||error!=Z_OK||!gzeof(file)) {
            int reader=reinterpret_cast<gz_statep>(file)->fd;
            native_file_record.codec_result=static_cast<U>(error);native_file_record.codec_calls=static_cast<U>(got);
            native_file_record.codec_before=static_cast<U>(gtos_zlib_seek(reader,0,SEEK_CUR));
            native_file_record.codec_bytes=static_cast<U>(gtos_zlib_seek(reader,0,SEEK_END));
            int fresh=gtos_zlib_open("/native/append.gz",O_RDONLY,0);Need(fresh>0);
            native_file_record.codec_failed=static_cast<U>(gtos_zlib_seek(fresh,0,SEEK_END));
            Need(gtos_zlib_close(fresh)==0);
        }
        Need(got==0&&error==Z_OK&&gzeof(file));
        Need(gzclose(file)==Z_OK);
    }
    __attribute__((unused)) void BadStreams() {
        errno=0;U before=LargestAllocation();
        const char* paths[]={"/native/truncated.gz","/native/corrupt.gz"};
        const int errors[]={Z_BUF_ERROR,Z_DATA_ERROR};
        unsigned char bytes[1024];
        for (U i=0;i<2;i++) {
            gzFile file=Open(paths[i],"r");
            for (U attempts=0;attempts<32;attempts++) { if (Read(file,bytes,sizeof(bytes))<=0) break; }
            int error=0;const char* message=gzerror(file,&error);
            native_file_record.codec_result=static_cast<U>(error);native_file_record.codec_calls=i+1;
            Need(message&&error==errors[i]);
            if (i==0) {
                Need(Read(file,bytes,1)==0&&gzerror(file,&error)&&error==Z_BUF_ERROR);
                Need(gzseek(file,FixtureBytes+1,SEEK_SET)==FixtureBytes+1);
                Need(Read(file,bytes,1)==0&&gzerror(file,&error)&&error==Z_BUF_ERROR);
            }
            Need(gzclose(file)==(i==0?Z_BUF_ERROR:Z_OK));
            native_file_record.codec_result=0;native_file_record.codec_calls=0;
        }
        AppendStream(bytes);
        errno=0;Need(gzopen("/native/missing.gz","r")==nullptr&&errno==ENOENT);
        for (U i=0;i<32;i++) {
            errno=0;Need(gzopen("/native/input.gz","rN")==nullptr&&errno==ENOSYS);
            errno=0;Need(gzopen("/native/input.gz","re")==nullptr&&errno==ENOSYS);
        }
        gzFile writer=Open("/native/oom.gz","w");
        Need(gzwrite(writer,"payload",7)==0);int error=0;Need(gzerror(writer,&error)&&error==Z_MEM_ERROR);
        Need(gzclose(writer)==Z_MEM_ERROR);
        gzFile failing=gzopen("/native/input.gz","r");Need(failing&&gzbuffer(failing,4096)==0);
        U command=native_file_record.command;native_file_record.command=1;
        errno=0;Need(Read(failing,bytes,1)==-1&&errno==EIO);native_file_record.command=command;
        const char* message=gzerror(failing,&error);
        Need(error==Z_ERRNO&&message&&strstr(message,strerror(EIO))&&gzclose(failing)==Z_OK);
        Need(LargestAllocation()==before);
    }
    __attribute__((unused)) void Boundaries() {
        unsigned char bytes[16];int handle=gtos_zlib_open("/native/input.gz",O_RDONLY,0);Need(handle>0);
        native_file_record.invalid_io=1;
        errno=0;Need(gtos_zlib_read(handle,nullptr,0)==-1&&errno==EFAULT);
        errno=0;Need(gtos_zlib_read(handle,reinterpret_cast<void*>(native_file_record.base+65535),2)==-1&&errno==EFAULT);
        errno=0;Need(gtos_zlib_read(handle,const_cast<uint32_t*>(readonlyWords),4)==-1&&errno==EFAULT);
        errno=0;Need(gtos_zlib_read(handle,reinterpret_cast<void*>(0xfffffffcU),8)==-1&&errno==EOVERFLOW);
        errno=0;Need(gtos_zlib_seek(handle,INT64_MAX,SEEK_SET)==-1&&errno==EOVERFLOW);
        errno=0;Need(gtos_zlib_seek(handle,INT64_MIN,SEEK_CUR)==-1&&errno==EINVAL);
        errno=0;Need(gtos_zlib_read(static_cast<int>(native_file_record.foreign_handle),bytes,1)==-1&&errno==EBADF);
        native_file_record.invalid_io=0;
        Need(gtos_zlib_seek(handle,0,SEEK_CUR)==0&&gtos_zlib_close(handle)==0);
        native_file_record.invalid_io=1;
        errno=0;Need(gtos_zlib_read(handle,bytes,1)==-1&&errno==EBADF);
        native_file_record.invalid_io=0;
        char path[513];for (U i=0;i<512;i++) path[i]='x';path[512]=0;
        errno=0;Need(gtos_zlib_open(path,O_RDONLY,0)==-1&&errno==ENAMETOOLONG);
    }
}
extern "C" int NativeV8IsolateMain() {
    native_file_record.stage=1;Need(native_file_record.mask==1&&native_v8_integration_stage==2);
    char path[]="/native/buffer0";path[14]=char('0'+GTOS_ZLIB_MODE);
    buffered=fopen(path,"w");Need(buffered&&fileno(buffered)>0);native_file_record.file=static_cast<U>(fileno(buffered));
    Need(setvbuf(buffered,fileBuffer,_IOFBF,sizeof(fileBuffer))==0&&fwrite("buffered-payload",1,16,buffered)==16);
    char durable[]="/native/durable0";durable[15]=char('0'+GTOS_ZLIB_MODE);
    FILE* committed=fopen(durable,"w");Need(committed&&fileno(committed)>0);native_file_record.directory=static_cast<U>(fileno(committed));
    Need(fwrite("committed",1,9,committed)==9&&fflush(committed)==0);
    Need(__cxa_thread_atexit(TlsDestructor,nullptr,__dso_handle)==0&&malloc(48)!=nullptr);
    GtosVmRegionInfo heap={};Need(gtos_native_heap_query(&heap)==1&&heap.length==65536&&heap.resident_pages==16);
    native_file_record.base=heap.base;native_file_record.vm_handle=heap.handle;
    native_file_record.foreign=v8::base::gtos::CurrentProcessInfo().thread_id;
    native_file_record.command=reinterpret_cast<U>(&native_v8_integration_stage);
    native_file_record.result_address=reinterpret_cast<U>(&native_v8_integration_result);
    gzFile file=Open("/native/input.gz","r");native_file_record.gzip_handle=static_cast<U>(reinterpret_cast<gz_statep>(file)->fd);
    unsigned char prefix[1024];Need(Read(file,prefix,sizeof(prefix))==1024);Bytes(prefix,0,sizeof(prefix));
    native_file_record.feature=static_cast<U>(adler32(adler32(0,nullptr,0),prefix,sizeof(prefix)));
    Need(native_file_record.feature==FixturePrefixAdler&&gzrewind(file)==0);
    Automatic automatic;
#if GTOS_ZLIB_MODE == 0
    Decode(file);Need(gzclose(file)==Z_OK);native_file_record.gzip_handle=0;CompressionLimit();ShortIo();
#elif GTOS_ZLIB_MODE == 1
    Need(gzclose(file)==Z_OK);native_file_record.gzip_handle=0;BadStreams();Boundaries();
#endif
    native_file_record.stage=3;
#if GTOS_ZLIB_MODE == 2
    native_file_record.fault_address=heap.base+65536;*reinterpret_cast<volatile U*>(heap.base+65536)=1;
#elif GTOS_ZLIB_MODE == 3
    native_file_record.fault_address=reinterpret_cast<U>(readonlyWords);*const_cast<volatile U*>(readonlyWords)=1;
#elif GTOS_ZLIB_MODE == 4
    v8::base::gtos::ExitImmediately(29);
#elif GTOS_ZLIB_MODE == 5
    for (;;) {
        Need(gzrewind(file)==0&&Read(file,prefix,sizeof(prefix))==1024);Bytes(prefix,0,sizeof(prefix));
        native_file_record.progress=native_file_record.progress+1;v8::base::gtos::Yield();
    }
#endif
#if GTOS_ZLIB_MODE < 4
    return 17;
#endif
}
