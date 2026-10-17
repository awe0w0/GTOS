#include <process/abi.h>
#include <process/event_abi.h>
#include <process/thread_abi.h>
#include <process/vm_abi.h>
#include "record.h"
extern "C" __attribute__((section(".record"),used)) EventRecord record={1,0,0,0,0,0,0,0,{0,0},0,0,{0,0},{0,0},0};
static int Call(unsigned op,unsigned address=0,unsigned bytes=0) {
    int result=(int)op;asm volatile("int $0x80":"+a"(result):"b"(address),"c"(bytes):"memory","cc");return result;
}
static void Exit(unsigned code) __attribute__((noreturn));
static void Exit(unsigned code) {Call(GTOS_SYS_EXIT,code);for (;;) {}}
static void Fail(unsigned line) {record.failedLine=line;Exit(line);}
#define CHECK(condition) do {++record.checks;if (!(condition)) Fail(__LINE__);} while (0)
static void Yield() {CHECK(Call(GTOS_SYS_YIELD)==0);}
static unsigned Ticks() {return (unsigned)Call(GTOS_SYS_TICKS);}
static int Control(unsigned op,unsigned handle) {GtosEventControlRequest r={1,handle};return Call(op,(unsigned)&r,sizeof(r));}
static unsigned Create(unsigned flags) {
    unsigned handle=0;GtosEventCreateRequest r={1,flags,(unsigned)&handle};CHECK(Call(GTOS_SYS_EVENT_CREATE,(unsigned)&r,sizeof(r))==0&&handle);return handle;
}
static int Duplicate(unsigned handle,unsigned rights) {GtosEventDupRequest r={1,handle,rights};return Call(GTOS_SYS_EVENT_DUP,(unsigned)&r,sizeof(r));}
static int WaitArray(unsigned* handles,unsigned count,unsigned timeout) {GtosEventWaitRequest r={1,(unsigned)handles,count,timeout};return Call(GTOS_SYS_EVENT_WAIT,(unsigned)&r,sizeof(r));}
static int Wait(unsigned handle,unsigned timeout=0) {return WaitArray(&handle,1,timeout);}
static void Set(unsigned h) {CHECK(Control(GTOS_SYS_EVENT_SET,h)==0);}
static void Close(unsigned h) {CHECK(Control(GTOS_SYS_EVENT_CLOSE,h)==0);}
static GtosVmReserveResult Reserve(unsigned bytes) {
    GtosVmReserveResult result={};GtosVmReserveRequest r={1,bytes,4096,0,(unsigned)&result};CHECK(Call(GTOS_SYS_VM_RESERVE,(unsigned)&r,sizeof(r))==0);return result;
}
static void Permissions(const GtosVmReserveResult& region,unsigned offset,unsigned bytes,unsigned flags) {
    GtosVmRangeRequest r={1,region.handle,offset,bytes,flags};CHECK(Call(GTOS_SYS_VM_SET_PERMISSIONS,(unsigned)&r,sizeof(r))==0);
}
static void Release(const GtosVmReserveResult& region) {GtosVmControlRequest r={1,region.handle,0};CHECK(Call(GTOS_SYS_VM_RELEASE,(unsigned)&r,sizeof(r))==0);}
static void Worker(unsigned argument) __attribute__((noreturn));
static void Worker(unsigned argument) {
    unsigned index=argument&1U;record.workerStage[index]=1;
    if (argument>=10) {CHECK(Wait(record.gate,1000)==0);Fail(__LINE__);}
    if (argument==2) {CHECK(Wait(record.gate,100)==GTOS_EVENT_ERR_BAD_HANDLE);record.workerStage[0]=3;Set(record.ack[0]);}
    else if (argument==4) {
        CHECK(WaitArray((unsigned*)record.array,1,100)==0);record.workerStage[0]=3;Set(record.ack[0]);
    } else {
        CHECK(Wait(record.gate,100)==0);record.workerStage[index]=2;Set(record.ack[index]);
        CHECK(Wait(record.automatic,100)==0);record.workerStage[index]=3;Set(record.ack[index]);
    }
    Call(GTOS_SYS_THREAD_EXIT,0);for (;;) {}
}
static void Start(unsigned index,unsigned argument,const GtosVmReserveResult& stack) {
    record.workerStage[index]=0;GtosThreadCreateRequest r={1,0,(unsigned)&Worker,argument,stack.handle,(unsigned)&record.thread[index]};
    CHECK(Call(GTOS_SYS_THREAD_CREATE,(unsigned)&r,sizeof(r))==0&&record.thread[index]);
}
static void Join(unsigned index) {
    unsigned code=~0U;GtosThreadJoinRequest r={1,record.thread[index],(unsigned)&code};int result;
    while ((result=Call(GTOS_SYS_THREAD_TRY_JOIN,(unsigned)&r,sizeof(r)))==GTOS_THREAD_ERR_BUSY) Yield();
    CHECK(result==0&&code==0);
}
static void LetWorkersPark(unsigned count) {
    unsigned before=Ticks();
    for (;;) {bool ready=true;for (unsigned i=0;i<count;++i) ready=ready&&record.workerStage[i]==1;if (ready) break;CHECK(Ticks()-before<50);Yield();}
    // With this BSP round robin each complete yield cycle lets every ready
    // worker run from its published stage to the immediately following WAIT.
    for (unsigned i=0;i<3;++i) Yield();
}
static void InvalidWire() {
    unsigned output=0x12345678U;GtosEventCreateRequest r={1,0,(unsigned)&output};
    CHECK(Call(GTOS_SYS_EVENT_CREATE,(unsigned)&r,sizeof(r)-1)==GTOS_EVENT_ERR_INVALID&&output==0x12345678U);
    CHECK(Call(GTOS_SYS_EVENT_CREATE,0xBFFFEFFCU,sizeof(r))==GTOS_EVENT_ERR_BAD_ADDRESS);
    r.version=2;CHECK(Call(GTOS_SYS_EVENT_CREATE,(unsigned)&r,sizeof(r))==GTOS_EVENT_ERR_UNAVAILABLE&&output==0x12345678U);
    r.version=1;r.flags=4;CHECK(Call(GTOS_SYS_EVENT_CREATE,(unsigned)&r,sizeof(r))==GTOS_EVENT_ERR_INVALID&&output==0x12345678U);
    r.flags=0;r.result_va=(unsigned)&InvalidWire;CHECK(Call(GTOS_SYS_EVENT_CREATE,(unsigned)&r,sizeof(r))==GTOS_EVENT_ERR_BAD_ADDRESS);
    *(volatile unsigned short*)0xBFFFEFFEU=0x5aa5;r.result_va=0xBFFFEFFEU;
    CHECK(Call(GTOS_SYS_EVENT_CREATE,(unsigned)&r,sizeof(r))==GTOS_EVENT_ERR_BAD_ADDRESS&&*(volatile unsigned short*)0xBFFFEFFEU==0x5aa5);
    r.flags=3;r.result_va=(unsigned)&r.flags;
    CHECK(Call(GTOS_SYS_EVENT_CREATE,(unsigned)&r,sizeof(r))==0&&r.flags);CHECK(Wait(r.flags)==0&&Wait(r.flags)==0);Close(r.flags);
    unsigned h=Create(0);GtosEventWaitRequest wait={1,(unsigned)&h,1,0};
    CHECK(Call(GTOS_SYS_EVENT_WAIT,(unsigned)&wait,sizeof(wait)-1)==GTOS_EVENT_ERR_INVALID);
    wait.version=2;CHECK(Call(GTOS_SYS_EVENT_WAIT,(unsigned)&wait,sizeof(wait))==GTOS_EVENT_ERR_UNAVAILABLE);wait.version=1;
    wait.count=0;CHECK(Call(GTOS_SYS_EVENT_WAIT,(unsigned)&wait,sizeof(wait))==GTOS_EVENT_ERR_INVALID);
    wait.count=17;CHECK(Call(GTOS_SYS_EVENT_WAIT,(unsigned)&wait,sizeof(wait))==GTOS_EVENT_ERR_INVALID);
    wait.count=1;wait.timeout_ticks=0x80000000U;CHECK(Call(GTOS_SYS_EVENT_WAIT,(unsigned)&wait,sizeof(wait))==GTOS_EVENT_ERR_INVALID);
    wait.timeout_ticks=0;wait.handles_va=0xBFFFEFFEU;CHECK(Call(GTOS_SYS_EVENT_WAIT,(unsigned)&wait,sizeof(wait))==GTOS_EVENT_ERR_BAD_ADDRESS);
    Close(h);
}
extern "C" void _start() {
    CHECK(record.version==1&&Call(GTOS_SYS_ABI)==1);
    if (record.mode==1) {
        CHECK(Control(GTOS_SYS_EVENT_SET,record.foreign)==GTOS_EVENT_ERR_BAD_HANDLE);
        CHECK(Wait(record.shared)==0);CHECK(Control(GTOS_SYS_EVENT_RESET,record.shared)==GTOS_EVENT_ERR_PERMISSION);
        record.phase=10;for (;;) {CHECK(Wait(record.shared)==0);++record.progress;Yield();}
    }
    if (record.mode==2) {
        record.gate=Create(0);Create(1);Create(0);
        for (unsigned i=0;i<2;++i) {GtosVmReserveResult stack=Reserve(4*4096);Permissions(stack,4096,2*4096,GTOS_VM_READ_WRITE);Start(i,10+i,stack);}
        LetWorkersPark(2);record.phase=30;*(volatile unsigned*)0xBFFFCFFCU=1;Fail(__LINE__);
    }
    CHECK(Wait(record.shared)==0&&Control(GTOS_SYS_EVENT_SET,record.foreign)==GTOS_EVENT_ERR_BAD_HANDLE);
    InvalidWire();unsigned manual=Create(3),automatic=Create(0);
    CHECK(Wait(manual)==0&&Wait(manual)==0);CHECK(Control(GTOS_SYS_EVENT_RESET,manual)==0&&Wait(manual)==GTOS_EVENT_ERR_TIMEOUT);
    Set(automatic);Set(automatic);CHECK(Wait(automatic)==0&&Wait(automatic)==GTOS_EVENT_ERR_TIMEOUT);
    unsigned onlyWait=(unsigned)Duplicate(automatic,GTOS_EVENT_RIGHT_WAIT);CHECK((int)onlyWait>0);
    CHECK(Control(GTOS_SYS_EVENT_SET,onlyWait)==GTOS_EVENT_ERR_PERMISSION&&Control(GTOS_SYS_EVENT_RESET,onlyWait)==GTOS_EVENT_ERR_PERMISSION);
    CHECK(Duplicate(onlyWait,0)==GTOS_EVENT_ERR_PERMISSION&&Duplicate(automatic,16)==GTOS_EVENT_ERR_PERMISSION);
    unsigned noWait=(unsigned)Duplicate(automatic,GTOS_EVENT_RIGHT_SET);CHECK((int)noWait>0);
    Set(automatic);unsigned list[2]={automatic,0};CHECK(WaitArray(list,2,0)==GTOS_EVENT_ERR_BAD_HANDLE&&Wait(automatic)==0);
    Set(automatic);list[1]=noWait;CHECK(WaitArray(list,2,0)==GTOS_EVENT_ERR_PERMISSION&&Wait(automatic)==0);
    Set(automatic);list[1]=automatic;CHECK(WaitArray(list,2,0)==0&&Wait(automatic)==GTOS_EVENT_ERR_TIMEOUT);
    Set(manual);list[0]=automatic;list[1]=manual;CHECK(WaitArray(list,2,0)==1);Set(automatic);CHECK(WaitArray(list,2,0)==0);
    unsigned alias=(unsigned)Duplicate(automatic,0);CHECK((int)alias>0);Close(automatic);CHECK(Wait(automatic)==GTOS_EVENT_ERR_BAD_HANDLE);
    Set(alias);CHECK(Wait(onlyWait)==0&&Wait(alias)==GTOS_EVENT_ERR_TIMEOUT);Close(onlyWait);Close(noWait);
    // The fixture injects the documented scheduler half-range rollover at
    // this one TICKS trap, then real PIT IRQs must wake this three-tick wait.
    record.phase=40;unsigned before=Ticks();CHECK(before==0xfffffffeU);
    CHECK(Wait(alias,3)==GTOS_EVENT_ERR_TIMEOUT);unsigned elapsed=Ticks()-before;CHECK(elapsed>=3&&elapsed<20);record.phase=41;
    Set(alias);CHECK(Wait(alias)==0);Close(alias);Close(manual);
    alias=Create(0);record.phase=42;before=Ticks();
    CHECK(Wait(alias,3)==GTOS_EVENT_ERR_TIMEOUT&&Ticks()-before>=5);
    CHECK(Wait(alias)==0&&Wait(alias)==GTOS_EVENT_ERR_TIMEOUT);Close(alias);record.phase=43;
    record.gate=Create(1);record.automatic=Create(0);GtosVmReserveResult stacks[2];
    for (unsigned i=0;i<2;++i) {record.ack[i]=Create(0);stacks[i]=Reserve(4*4096);Permissions(stacks[i],4096,2*4096,GTOS_VM_READ_WRITE);Start(i,i,stacks[i]);}
    LetWorkersPark(2);Set(record.gate);
    CHECK(Wait(record.ack[0],100)==0&&Wait(record.ack[1],100)==0&&Wait(record.gate)==0&&Wait(record.gate)==0);
    Set(record.automatic);unsigned first[2]={record.ack[0],record.ack[1]};int ready=WaitArray(first,2,100);CHECK(ready==0||ready==1);
    CHECK((record.workerStage[0]==3)+(record.workerStage[1]==3)==1&&Wait(record.ack[1-ready])==GTOS_EVENT_ERR_TIMEOUT);
    Set(record.automatic);CHECK(Wait(record.ack[1-ready],100)==0);Join(0);Join(1);Close(record.gate);Close(record.automatic);
    record.gate=Create(0);Start(0,2,stacks[0]);LetWorkersPark(1);Close(record.gate);CHECK(Wait(record.ack[0],100)==0);Join(0);
    record.gate=Create(0);GtosVmReserveResult array=Reserve(4096);Permissions(array,0,4096,GTOS_VM_READ_WRITE);
    record.array=array.base;*(unsigned*)array.base=record.gate;Start(0,4,stacks[0]);LetWorkersPark(1);
    *(unsigned*)array.base=0;Release(array);Set(record.gate);CHECK(Wait(record.ack[0],100)==0);Join(0);Close(record.gate);
    for (unsigned i=0;i<2;++i) {Close(record.ack[i]);Release(stacks[i]);}
    // One original admission grant intentionally remains for owner Reap. The
    // independent peer owns its restricted alias to the same real event.
    record.phase=99;Exit(0);
}
