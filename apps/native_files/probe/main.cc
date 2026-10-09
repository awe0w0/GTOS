#include <process/abi.h>
#include <process/file_abi.h>
#include <process/vm_abi.h>
#include <process/resource_abi.h>
#include "record.h"
#ifndef GTOS_FILE_MODE
#define GTOS_FILE_MODE 0
#endif
__attribute__((section(".data.file_record"))) volatile FileRecord native_file_record={1,GTOS_FILE_MODE,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
namespace {
    typedef unsigned U;typedef unsigned char B;
    GtosFileInfo info;U words[80],handles[64];char path[513];B comparison[256];
    int Raw(U op,U address,U bytes) {
        U value=op;asm volatile("int $0x80":"+a"(value):"b"(address),"c"(bytes):"memory","cc");
        if (op>=GTOS_SYS_FILE_OPEN&&op<=GTOS_SYS_FILE_HANDLE_INFO) {
            native_file_record.raw_calls++;native_file_record.mask|=1U<<(op-GTOS_SYS_FILE_OPEN);
        }
        return (int)value;
    }
    int Call(U op,const void* request,U bytes) { return Raw(op,(U)request,bytes); }
    void Exit(U code) { Raw(GTOS_SYS_EXIT,code,0);for (;;) asm volatile("ud2"); }
    void Need(bool value) {
        native_file_record.checks++;if (!value) { native_file_record.error=native_file_record.checks;Exit(91); }
    }
    int Invalid(U op,U request,U bytes,int expected) {
        native_file_record.invalid_io=1;int result=Raw(op,request,bytes);native_file_record.invalid_io=0;
        native_file_record.bad_calls++;Need(result==expected);return result;
    }
    U Length(const char* p) { U n=0;while (p[n]) n++;return n+1; }
    void Fill(void* p,U n,B value) { B* a=(B*)p;for (U i=0;i<n;i++) a[i]=value; }
    void Copy(void* to,const void* from,U n) { B* d=(B*)to;const B* s=(const B*)from;for (U i=0;i<n;i++) d[i]=s[i]; }
    bool Equal(const void* a,const void* b,U n) { const B* x=(const B*)a;const B* y=(const B*)b;for (U i=0;i<n;i++) if (x[i]!=y[i]) return false;return true; }
    int Open(const char* p,U flags) { GtosFileOpenRequest r={1,(U)p,Length(p),flags};return Call(GTOS_SYS_FILE_OPEN,&r,sizeof(r)); }
    int Path(U op,const char* p) { GtosFilePathRequest r={1,(U)p,Length(p)};return Call(op,&r,sizeof(r)); }
    int Control(U op,U h) { GtosFileControlRequest r={1,h};return Call(op,&r,sizeof(r)); }
    int Transfer(U op,U h,void* p,U n) { GtosFileTransferRequest r={1,h,(U)p,n};return Call(op,&r,sizeof(r)); }
    int Seek(U h,U low=0,U high=0,U whence=0) { GtosFileSeekRequest r={1,h,low,high,whence};return Call(GTOS_SYS_FILE_SEEK,&r,sizeof(r)); }
    int Info(U op,U h,void* p=&info) { GtosFileInfoRequest r={1,h,(U)p,sizeof(info)};return Call(op,&r,sizeof(r)); }
    int Truncate(U h,U n) { GtosFileTruncateRequest r={1,h,n};return Call(GTOS_SYS_FILE_TRUNCATE,&r,sizeof(r)); }
    int Stat(const char* p,void* out=&info) { GtosFileStatRequest r={1,(U)p,Length(p),(U)out,sizeof(info)};return Call(GTOS_SYS_FILE_STAT,&r,sizeof(r)); }
    int Rename(const char* p,const char* q) { GtosFileRenameRequest r={1,(U)p,Length(p),(U)q,Length(q)};return Call(GTOS_SYS_FILE_RENAME,&r,sizeof(r)); }
    void Permissions(U offset,U bytes,U protection) {
        GtosVmRangeRequest r={1,native_file_record.vm_handle,offset,bytes,protection};
        Need(Call(GTOS_SYS_VM_SET_PERMISSIONS,&r,sizeof(r))==0);
    }
    void Reserve() {
        GtosVmReserveResult out={};GtosVmReserveRequest r={1,16384,4096,0x80000000,(U)&out};
        Need(Call(GTOS_SYS_VM_RESERVE,&r,sizeof(r))==0&&out.base==0x80000000&&out.length==16384&&out.handle);
        native_file_record.base=out.base;native_file_record.vm_handle=out.handle;Permissions(4096,8192,3);
    }
    void Foreign(U handle) {
        GtosFileControlRequest c={1,handle};GtosFileTransferRequest t={1,handle,(U)comparison,1};
        GtosFileSeekRequest s={1,handle,0,0,0};GtosFileTruncateRequest x={1,handle,0};
        GtosFileInfoRequest i={1,handle,(U)&info,sizeof(info)};
        Invalid(GTOS_SYS_FILE_READ,(U)&t,sizeof(t),-9);Invalid(GTOS_SYS_FILE_WRITE,(U)&t,sizeof(t),-9);
        Invalid(GTOS_SYS_FILE_SEEK,(U)&s,sizeof(s),-9);Invalid(GTOS_SYS_FILE_TRUNCATE,(U)&x,sizeof(x),-9);
        Invalid(GTOS_SYS_FILE_CLOSE,(U)&c,sizeof(c),-9);Invalid(GTOS_SYS_FILE_SYNC,(U)&c,sizeof(c),-9);
        Invalid(GTOS_SYS_FILE_SIZE,(U)&c,sizeof(c),-9);Invalid(GTOS_SYS_FILE_DIR_REWIND,(U)&c,sizeof(c),-9);
        Invalid(GTOS_SYS_FILE_HANDLE_INFO,(U)&i,sizeof(i),-9);Invalid(GTOS_SYS_FILE_DIR_READ,(U)&i,sizeof(i),-9);
    }
    void BadRequests() {
        const U sizes[16]={16,16,16,20,8,8,12,20,12,12,20,12,16,8,8,16};
        Fill(words,sizeof(words),0);words[0]=2;
        for (U i=0;i<16;i++) {
            Invalid(0x4720+i,0,sizes[i]-1,-22);
            Invalid(0x4720+i,0x80000000,sizes[i],-14);
            Invalid(0x4720+i,(U)words,sizes[i],-38);
            Invalid(0x4720+i,0xfffffff8,sizes[i],-14);
            Invalid(0x4720+i,0x100000,sizes[i],-14);
        }
        Need(Raw(0x4730,0,0)==-38);
    }
    void Png(B* buffer) {
        U h=Open("/fixtures/image.png",1);Need((int)h>0&&Control(GTOS_SYS_FILE_SIZE,h)==1108);
        Need(Transfer(GTOS_SYS_FILE_READ,h,buffer,1108)==1108);
        for (U offset=0;offset<1108;offset+=256) {
            U n=1108-offset;if (n>256) n=256;
            GtosResourceReadRequest r={1,1,offset,(U)comparison,n};
            Need(Call(GTOS_SYS_RESOURCE_READ,&r,sizeof(r))==(int)n);
            for (U i=0;i<n;i++) Need(buffer[offset+i]==comparison[i]);
        }
        Need(Control(GTOS_SYS_FILE_CLOSE,h)==0);native_file_record.png_bytes=1108;
    }
    B Pattern(U i) { return (B)(i*37+i/251+17); }
    void Persistent(B* buffer) {
        U h=Open("/native/session.bin",GTOS_FILE_READ_WRITE|GTOS_FILE_CREATE|GTOS_FILE_TRUNCATE);Need((int)h>0);
        for (U offset=0;offset<8193;offset+=4096) {
            U n=8193-offset;if (n>4096) n=4096;
            for (U i=0;i<n;i++) buffer[i]=Pattern(offset+i);
            Need(Transfer(GTOS_SYS_FILE_WRITE,h,buffer,n)==(int)n);
        }
        Need(Control(GTOS_SYS_FILE_SYNC,h)==0&&Control(GTOS_SYS_FILE_SIZE,h)==8193);
        Need(Seek(h)==0);
        for (U offset=0;offset<8193;offset+=4096) {
            U n=8193-offset;if (n>4096) n=4096;
            Need(Transfer(GTOS_SYS_FILE_READ,h,buffer,n)==(int)n);
            for (U i=0;i<n;i++) Need(buffer[i]==Pattern(offset+i));
        }
        U other=Open("/native/session.bin",1);Need((int)other>0&&other!=h);
        Need(Transfer(GTOS_SYS_FILE_READ,other,comparison,1)==1&&comparison[0]==Pattern(0));
        Need(Transfer(GTOS_SYS_FILE_READ,h,comparison,1)==0);
        GtosFileTransferRequest t={1,other,(U)comparison,1};Invalid(GTOS_SYS_FILE_WRITE,(U)&t,sizeof(t),-9);
        GtosFileTruncateRequest tr={1,other,0};Invalid(GTOS_SYS_FILE_TRUNCATE,(U)&tr,sizeof(tr),-9);
        Need(Control(GTOS_SYS_FILE_CLOSE,other)==0&&Control(GTOS_SYS_FILE_CLOSE,h)==0);
        U stale=h;h=Open("/native/session.bin",GTOS_FILE_WRITE|GTOS_FILE_APPEND);Need((int)h>0&&h!=stale);
        Need(Seek(h)==0&&Transfer(GTOS_SYS_FILE_WRITE,h,(void*)"GTOS",4)==4&&Control(GTOS_SYS_FILE_SIZE,h)==8197);
        GtosFileControlRequest c={1,stale};Invalid(GTOS_SYS_FILE_CLOSE,(U)&c,sizeof(c),-9);
        t.handle=h;Invalid(GTOS_SYS_FILE_READ,(U)&t,sizeof(t),-9);Need(Control(GTOS_SYS_FILE_CLOSE,h)==0);
    }
    void Boundary(B* b) {
        U h=Open("/native/session.bin",3);Need((int)h>0&&Seek(h)==0);
        const U bad[7]={0,0x100000,0x40000000,0x80000000,0x80003ffc,0xbfffffff,0xfffffff8};
        Fill(b+4096-8,8,0xA5);
        b[4096]=Pattern(0);
        Permissions(8192,4096,1);
        for (U i=0;i<7;i++) {
            GtosFileTransferRequest r={1,h,bad[i],16};
            Invalid(GTOS_SYS_FILE_READ,(U)&r,sizeof(r),-14);
            if (bad[i]!=0x40000000) Invalid(GTOS_SYS_FILE_WRITE,(U)&r,sizeof(r),-14);
            Need(Seek(h,0,0,1)==0&&Control(GTOS_SYS_FILE_SIZE,h)==8197);
        }
        GtosFileTransferRequest r={1,h,(U)(b+4096-8),16};
        Invalid(GTOS_SYS_FILE_READ,(U)&r,sizeof(r),-14);
        for (U i=0;i<8;i++) Need(b[4096-8+i]==0xA5);
        Need(Seek(h,0,0,1)==0);
        // Read-only user bytes are valid WRITE sources; only destinations need RW.
        Need(Transfer(GTOS_SYS_FILE_WRITE,h,(void*)"X",1)==1);
        Need(Seek(h)==0&&Transfer(GTOS_SYS_FILE_WRITE,h,(void*)(b+4096),1)==1&&Seek(h)==0);
        r.bytes=4097;r.buffer=0;Invalid(GTOS_SYS_FILE_READ,(U)&r,sizeof(r),-7);Invalid(GTOS_SYS_FILE_WRITE,(U)&r,sizeof(r),-7);
        r.bytes=0;Invalid(GTOS_SYS_FILE_READ,(U)&r,sizeof(r),-14);
        Need(Transfer(GTOS_SYS_FILE_READ,h,(void*)0x80000000,0)==0);
        Permissions(8192,4096,0);r.buffer=(U)(b+4096-8);r.bytes=16;
        Invalid(GTOS_SYS_FILE_READ,(U)&r,sizeof(r),-14);Invalid(GTOS_SYS_FILE_WRITE,(U)&r,sizeof(r),-14);
        // Request snapshot crossing into a NONE page must also fail before I/O.
        GtosFileOpenRequest o={1,(U)"/native/session.bin",20,1};
        Copy(b+4096-8,&o,8);Invalid(GTOS_SYS_FILE_OPEN,(U)(b+4096-8),sizeof(o),-14);
        Permissions(8192,4096,3);
        Need(Seek(h)==0&&Transfer(GTOS_SYS_FILE_WRITE,h,(void*)"\x11",1)==1&&Seek(h)==0);
        // Read result overwrites its own request only after a complete snapshot.
        r.handle=h;r.buffer=(U)b;r.bytes=16;Copy(b,&r,sizeof(r));
        Need(Raw(GTOS_SYS_FILE_READ,(U)b,sizeof(r))==16);
        for (U i=0;i<16;i++) Need(b[i]==Pattern(i));
        Need(Seek(h,8197)==8197);Fill(comparison,sizeof(comparison),0xA7);
        Need(Transfer(GTOS_SYS_FILE_READ,h,comparison,16)==0&&comparison[0]==0xA7);
        r.buffer=0x80003ffc;r.bytes=16;Invalid(GTOS_SYS_FILE_READ,(U)&r,sizeof(r),-14);
        GtosFileSeekRequest s={1,h,0xffffffff,0x7fffffff,0};Invalid(GTOS_SYS_FILE_SEEK,(U)&s,sizeof(s),-75);
        s.offset_low=0;s.offset_high=0x80000000;s.whence=1;Invalid(GTOS_SYS_FILE_SEEK,(U)&s,sizeof(s),-22);
        Need(Seek(h,0,0,1)==8197);
        Need(Control(GTOS_SYS_FILE_CLOSE,h)==0);
        // Overlapping write descriptor/source uses the original snapshotted bytes.
        h=Open("/native/alias.bin",3|0x100|0x400);Need((int)h>0);
        r.handle=h;r.buffer=(U)b;r.bytes=sizeof(r);Copy(b,&r,sizeof(r));Copy(words,&r,sizeof(r));
        Need(Raw(GTOS_SYS_FILE_WRITE,(U)b,sizeof(r))==(int)sizeof(r)&&Seek(h)==0);
        Need(Transfer(GTOS_SYS_FILE_READ,h,comparison,sizeof(r))==(int)sizeof(r)&&Equal(comparison,words,sizeof(r)));
        Need(Control(GTOS_SYS_FILE_CLOSE,h)==0&&Path(GTOS_SYS_FILE_REMOVE,"/native/alias.bin")==0);
        o.path=0x80003ffc;o.path_bytes=8;o.flags=1;Copy(b+8192-4,"/x\0",4);
        Invalid(GTOS_SYS_FILE_OPEN,(U)&o,sizeof(o),-14);
        o.path=(U)path;o.path_bytes=513;Invalid(GTOS_SYS_FILE_OPEN,(U)&o,sizeof(o),-7);
        o.path_bytes=512;Fill(path,512,'x');Invalid(GTOS_SYS_FILE_OPEN,(U)&o,sizeof(o),-22);
        path[511]=0;path[10]=0;Invalid(GTOS_SYS_FILE_OPEN,(U)&o,sizeof(o),-22);
        o.path_bytes=1;Invalid(GTOS_SYS_FILE_OPEN,(U)&o,sizeof(o),-22);
        // Output overlaps both STAT descriptor and input path.
        Copy(b+128,"/native/session.bin",20);
        GtosFileStatRequest st={1,(U)(b+128),20,(U)b,sizeof(info)};Copy(b,&st,sizeof(st));
        Need(Raw(GTOS_SYS_FILE_STAT,(U)b,sizeof(st))==0);
        Copy(&info,b,sizeof(info));Need(info.version==1&&info.type==1&&info.bytes==8197);
        st.path=(U)"/native/session.bin";st.result=0x40000000;Invalid(GTOS_SYS_FILE_STAT,(U)&st,sizeof(st),-14);
        st.result=(U)&info;st.result_bytes--;Invalid(GTOS_SYS_FILE_STAT,(U)&st,sizeof(st),-22);
    }
    void Namespace(B* b) {
        Need(Path(GTOS_SYS_FILE_MKDIR,"/native/temp")==0);
        Need(Path(GTOS_SYS_FILE_MKDIR,"/native/temp")==-17);
        U h=Open("/native/temp/hole.bin",3|0x100|0x200);Need((int)h>0);
        Need(Open("/native/temp/hole.bin",2|0x100|0x200)==-17);
        Need(Transfer(GTOS_SYS_FILE_WRITE,h,(void*)"HEAD",4)==4&&Seek(h,4098)==4098);
        Need(Transfer(GTOS_SYS_FILE_WRITE,h,(void*)"END",3)==3&&Control(GTOS_SYS_FILE_SIZE,h)==4101);
        Need(Seek(h,4)==4&&Transfer(GTOS_SYS_FILE_READ,h,b,4094)==4094);
        for (U i=0;i<4094;i++) Need(b[i]==0);
        Need(Truncate(h,32)==0&&Truncate(h,2048)==0&&Control(GTOS_SYS_FILE_SIZE,h)==2048);
        Need(Info(GTOS_SYS_FILE_HANDLE_INFO,h)==0&&info.version==1&&info.type==1&&info.bytes==2048&&!info.name[0]);
        Need(Control(GTOS_SYS_FILE_CLOSE,h)==0&&Rename("/native/temp/hole.bin","/native/hole.bin")==0);
        h=Open("/native/中文.txt",2|0x100|0x400);Need((int)h>0&&Transfer(GTOS_SYS_FILE_WRITE,h,(void*)"原生文件",12)==12&&Control(GTOS_SYS_FILE_CLOSE,h)==0);
        Need(Path(GTOS_SYS_FILE_REMOVE,"/native/temp")==0&&Stat("/native/hole.bin")==0&&info.bytes==2048);
        U d=Path(GTOS_SYS_FILE_DIR_OPEN,"/native");Need((int)d>0);
        Need(Info(GTOS_SYS_FILE_HANDLE_INFO,d)==0&&info.type==2&&!info.bytes&&!info.name[0]);
        Need(Control(GTOS_SYS_FILE_DIR_REWIND,d)==0&&Info(GTOS_SYS_FILE_DIR_READ,d)==1);
        GtosFileInfo saved=info;Need(Control(GTOS_SYS_FILE_DIR_REWIND,d)==0);
        GtosFileInfoRequest ir={1,d,0x40000000,sizeof(info)};
        Invalid(GTOS_SYS_FILE_DIR_READ,(U)&ir,sizeof(ir),-14);
        Need(Info(GTOS_SYS_FILE_DIR_READ,d)==1&&Equal(&info,&saved,sizeof(info)));
        U entries=1;int result;
        while ((result=Info(GTOS_SYS_FILE_DIR_READ,d))>0) { entries++;Need(entries<32); }
        Need(result==0&&entries>=5);Fill(&info,sizeof(info),0xA3);
        Need(Info(GTOS_SYS_FILE_DIR_READ,d)==0&&((B*)&info)[0]==0xA3&&((B*)&info)[267]==0xA3);
        Need(Control(GTOS_SYS_FILE_CLOSE,d)==0);
        // Fill all global handle slots; the concurrently live peer holds two.
        for (U i=0;i<62;i++) { int opened=Open("/native/session.bin",1);Need(opened>0);handles[i]=opened; }
        Need(Open("/native/session.bin",1)==-24);
        for (U i=0;i<62;i++) Need(Control(GTOS_SYS_FILE_CLOSE,handles[i])==0);
        for (U i=0;i<100;i++) { int opened=Open("/native/session.bin",1);Need(opened>0&&Control(GTOS_SYS_FILE_CLOSE,opened)==0); }
    }
    void IoFailures(B* b) {
        U h=Open("/native/session.bin",1);Need((int)h>0);
        Fill(b,4096,0xA5);native_file_record.command=1;
        Need(Transfer(GTOS_SYS_FILE_READ,h,b,4096)==-5);native_file_record.command=0;
        for (U i=0;i<4096;i++) Need(b[i]==0xA5);
        Need(Control(GTOS_SYS_FILE_CLOSE,h)==0);
        h=Open("/native/write-error.bin",3|0x100|0x400);Need((int)h>0);
        native_file_record.command=2;Need(Transfer(GTOS_SYS_FILE_WRITE,h,b,4096)==-5);native_file_record.command=0;
        Need(Control(GTOS_SYS_FILE_CLOSE,h)==0&&Path(GTOS_SYS_FILE_REMOVE,"/native/write-error.bin")==0);
        h=Open("/native/close-error.bin",3|0x100|0x400);Need((int)h>0&&Transfer(GTOS_SYS_FILE_WRITE,h,(void*)"FAILED",6)==6);
        native_file_record.command=3;Need(Control(GTOS_SYS_FILE_CLOSE,h)==-5);native_file_record.command=0;
        GtosFileControlRequest c={1,h};Invalid(GTOS_SYS_FILE_CLOSE,(U)&c,sizeof(c),-9);
        Need(Path(GTOS_SYS_FILE_REMOVE,"/native/close-error.bin")==0);
    }
    void Retain() {
        Copy(path,"/native/reap0.bin",18);path[12]='0'+GTOS_FILE_MODE;
        int h=Open(path,3|0x100|0x400);Need(h>0);
        Need(Transfer(GTOS_SYS_FILE_WRITE,h,(void*)"REAPED",6)==6);
        int d=Path(GTOS_SYS_FILE_DIR_OPEN,"/native");Need(d>0);
        native_file_record.file=h;native_file_record.directory=d;
    }
}
extern "C" void NativeEntry() {
    if (GTOS_FILE_MODE==4) {
        int h=Open("/peer.bin",3|0x100|0x400);Need(h>0&&Transfer(GTOS_SYS_FILE_WRITE,h,(void*)"PEER",4)==4&&Control(GTOS_SYS_FILE_SYNC,h)==0);
        int d=Path(GTOS_SYS_FILE_DIR_OPEN,"/");Need(d>0);native_file_record.file=h;native_file_record.directory=d;native_file_record.stage=1;
        for (;;) {
            native_file_record.progress++;
            U command=native_file_record.command;
            if (command&&command!=native_file_record.ack) {
                Foreign(native_file_record.foreign);
                Need(Seek(h)==0&&Transfer(GTOS_SYS_FILE_READ,h,comparison,4)==4&&Equal(comparison,"PEER",4));
                native_file_record.ack=command;
            }
        }
    }
    Reserve();B* b=(B*)0x80001000;
    if (GTOS_FILE_MODE==5) {
        U h=Open("/native/session.bin",1);Need((int)h>0&&Control(GTOS_SYS_FILE_SIZE,h)==8197);
        for (U offset=0;offset<8197;offset+=4096) {
            U n=8197-offset;if (n>4096) n=4096;
            Need(Transfer(GTOS_SYS_FILE_READ,h,b,n)==(int)n);
            for (U i=0;i<n;i++) Need(b[i]==(offset+i<8193?Pattern(offset+i):(B)"GTOS"[offset+i-8193]));
        }
        Need(Control(GTOS_SYS_FILE_CLOSE,h)==0);Png(b);native_file_record.stage=2;Exit(0);
    }
    BadRequests();Png(b);Persistent(b);Boundary(b);Namespace(b);IoFailures(b);Retain();
    native_file_record.stage=3;
    if (GTOS_FILE_MODE==1) { volatile B fault=*(volatile B*)0x80003000;(void)fault;Exit(92); }
    if (GTOS_FILE_MODE==2||GTOS_FILE_MODE==3) for (;;) native_file_record.progress++;
    native_file_record.stage=2;Exit(0);
}
