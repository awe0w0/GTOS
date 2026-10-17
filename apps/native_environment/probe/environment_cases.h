#ifndef ENVIRONMENT_CASES_H
#define ENVIRONMENT_CASES_H
#include <process/environment_abi.h>
static void EnvironmentMatches(const GtosEnvironmentReadResult& value) {
    CHECK(value.version==1 && !value.reserved && value.count<=64 && value.data_bytes<=3824);
    if (cpu_record.environmentCase==7) CHECK(!value.count && !value.data_bytes);
    else if (cpu_record.environmentCase!=0) {
        const char* expected=0;unsigned bytes=0,count=2;
        switch (cpu_record.environmentCase) {
        case 1: {static const char e[]="FORCE_COLOR=\0NO_COLOR=1\0PEER=alone\0";expected=e;bytes=sizeof(e)-1;count=3;break;}
        case 2: {static const char e[]="FORCE_COLOR=0\0NO_COLOR=1\0";expected=e;bytes=sizeof(e)-1;break;}
        case 3: {static const char e[]="FORCE_COLOR=1\0NO_COLOR=1\0";expected=e;bytes=sizeof(e)-1;break;}
        case 4: {static const char e[]="FORCE_COLOR=\0NO_COLOR=\0";expected=e;bytes=sizeof(e)-1;break;}
        case 5: {static const char e[]="NO_COLOR=1\0";expected=e;bytes=sizeof(e)-1;count=1;break;}
        case 6: {static const char e[]="FORCE_COLOR=1\0";expected=e;bytes=sizeof(e)-1;count=1;break;}
        default: CHECK(false);
        }
        CHECK(value.count==count && value.data_bytes==bytes);
        for (unsigned i=0;i<bytes;++i) CHECK(value.data[i]==expected[i]);
    } else {
        const char expected[]="FORCE_COLOR=1\0NO_COLOR=\0A=x=y\0EMPTY=\0";
        CHECK(value.count==64 && value.data_bytes==3824);
        for (unsigned i=0;i<sizeof(expected)-1;++i) CHECK(value.data[i]==expected[i]);
        for (unsigned i=4;i<64;++i) {
            const unsigned at=value.offsets[i], number=i-4;
            CHECK(at+4<value.data_bytes && value.data[at]=='N'
                && value.data[at+1]=='0'+number/10 && value.data[at+2]=='0'+number%10 && value.data[at+3]=='=');
            if (i<63) CHECK(!value.data[at+4]);
            else {
                for (unsigned j=at+4;j<value.data_bytes-1;++j) CHECK(value.data[j]=='Z');
                CHECK(!value.data[value.data_bytes-1]);
            }
        }
    }
    unsigned at=0;
    for (unsigned i=0;i<value.count;++i) {
        CHECK(value.offsets[i]==at);
        while (at<value.data_bytes && value.data[at]) ++at;
        CHECK(at<value.data_bytes); ++at;
    }
    CHECK(at==value.data_bytes);
    for (unsigned i=value.count;i<64;++i) CHECK(!value.offsets[i]);
    for (unsigned i=value.data_bytes;i<3824;++i) CHECK(!value.data[i]);
}
static void EnvironmentCases(bool boundaries) {
    GtosVmReserveResult region={}; GtosVmReserveRequest reserve={1,16384,4096,0,(unsigned)&region};
    CHECK(Call(GTOS_SYS_VM_RESERVE,(unsigned)&reserve,sizeof(reserve))==0);
    GtosVmRangeRequest commit={1,region.handle,4096,8192,GTOS_VM_READ_WRITE};
    CHECK(Call(GTOS_SYS_VM_SET_PERMISSIONS,(unsigned)&commit,sizeof(commit))==0);
    unsigned char* buffer=(unsigned char*)(region.base+4096);
    GtosEnvironmentReadResult* value=(GtosEnvironmentReadResult*)(buffer+64);
    GtosEnvironmentReadRequest request={1,0,(unsigned)value,4096};
    Fill(buffer,8192);
    CHECK(Call(GTOS_SYS_ENVIRONMENT_READ,(unsigned)&request,16)==0);
    EnvironmentMatches(*value);
    CHECK(Sentinel(buffer,64) && Sentinel(buffer+4160,8192-4160));
    value->data[0]='!';
    CHECK(Call(GTOS_SYS_ENVIRONMENT_READ,(unsigned)&request,16)==0);
    EnvironmentMatches(*value);
    if (boundaries) {
        const unsigned sizes[]={0,15,17};
        for (unsigned i=0;i<3;++i) {
            Fill(buffer,8192); CHECK(Call(GTOS_SYS_ENVIRONMENT_READ,(unsigned)&request,sizes[i])==-22);
            CHECK(Sentinel(buffer,8192));
        }
        Fill(buffer,8192); request.version=2;
        CHECK(Call(GTOS_SYS_ENVIRONMENT_READ,(unsigned)&request,16)==-38 && Sentinel(buffer,8192)); request.version=1;
        request.flags=1;
        CHECK(Call(GTOS_SYS_ENVIRONMENT_READ,(unsigned)&request,16)==-22 && Sentinel(buffer,8192)); request.flags=0;
        request.result_bytes=4095;
        CHECK(Call(GTOS_SYS_ENVIRONMENT_READ,(unsigned)&request,16)==-22 && Sentinel(buffer,8192)); request.result_bytes=4096;
        CHECK(Call(GTOS_SYS_ENVIRONMENT_READ,0,16)==-14);
        CHECK(Call(GTOS_SYS_ENVIRONMENT_READ,0,15)==-22);
        const unsigned addresses[]={0,0x40000000U,0xBFFFCFF0U,0xFFFFFFF0U};
        for (unsigned i=0;i<4;++i) {
            request.result_va=addresses[i]; CHECK(Call(GTOS_SYS_ENVIRONMENT_READ,(unsigned)&request,16)==-14);
        }
        unsigned char* tail=(unsigned char*)(CPU_RECORD_VA+4096-16); Fill(tail,16);
        request.result_va=(unsigned)tail;
        CHECK(Call(GTOS_SYS_ENVIRONMENT_READ,(unsigned)&request,16)==-14 && Sentinel(tail,16));
        CHECK(Call(GTOS_SYS_ENVIRONMENT_READ,(unsigned)(tail+8),16)==-14 && Sentinel(tail,16));
        for (unsigned shift=0;shift<5;++shift) {
            Fill(buffer,8192); GtosEnvironmentReadRequest* overlap=(GtosEnvironmentReadRequest*)(buffer+64);
            *overlap=GtosEnvironmentReadRequest{1,0,(unsigned)(buffer+64+shift),4096};
            CHECK(Call(GTOS_SYS_ENVIRONMENT_READ,(unsigned)overlap,16)==0);
            CHECK(Sentinel(buffer,64) && Sentinel(buffer+4160+shift,8192-4160-shift));
        }
    }
    GtosVmControlRequest release={1,region.handle,0};
    CHECK(Call(GTOS_SYS_VM_RELEASE,(unsigned)&release,sizeof(release))==0);
}
#endif
