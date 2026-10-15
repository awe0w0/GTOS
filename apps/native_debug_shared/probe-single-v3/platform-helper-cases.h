#ifndef GTOS_PLATFORM_HELPER_CASES_H
#define GTOS_PLATFORM_HELPER_CASES_H
    GtosRealtimeReadResult HelperRealtime() {
        GtosRealtimeReadResult value={};
        GtosRealtimeReadRequest request={GTOS_REALTIME_ABI_VERSION,0,(unsigned)&value,sizeof(value)};
        Need(Raw(GTOS_SYS_REALTIME_READ,(unsigned)&request,sizeof(request))==0);
        Need(value.version==GTOS_REALTIME_ABI_VERSION && value.unit==GTOS_REALTIME_UNIT_MICROSECONDS
            && value.source==GTOS_REALTIME_SOURCE_CMOS_PIT && value.capabilities==GTOS_REALTIME_REQUIRED_CAPABILITIES
            && value.microseconds>1700000000000000ULL && value.microseconds<2000000000000000ULL);
        return value;
    }
    void PlatformHelpers() {
        RuntimeValues();
        using v8::base::OS;
        const int saved=errno;
        { auto libraries=OS::GetSharedLibraryAddresses();Need(libraries.empty()); }
        OS::SignalCodeMovingGC();
        Need(OS::DirectorySeparator()=='/');
        for (unsigned byte=0;byte<256;byte++) Need(OS::isDirectorySeparator((char)byte)==(byte=='/'));
        Need(OS::ActivationFrameAlignment()==16 && native_helper_call_alignment()==0);
        OS::StrNCpy(nullptr,0,nullptr,0);
        char source[32],destination[34];
        const unsigned lengths[]={0,1,15,32},counts[]={0,1,2,16,32};
        for (unsigned length:lengths) {
            for (unsigned i=0;i<32;i++) source[i]=(char)(i+1);
            if (length<32) source[length]=0;
            for (unsigned count:counts) {
                for (char& byte:destination) byte=(char)0xa5;
                OS::StrNCpy(destination+1,32,source,count);
                Need((unsigned char)destination[0]==0xa5 && (unsigned char)destination[33]==0xa5);
                for (unsigned i=0;i<32;i++) Need((unsigned char)destination[i+1]
                    ==(i>=count?0xa5U:i>=length?0U:i+1));
            }
        }
        char bytes[255],copy[257];
        for (unsigned i=0;i<255;i++) bytes[i]=(char)(i+1);
        for (char& byte:copy) byte=(char)0xa5;
        OS::StrNCpy(copy+1,255,bytes,255);
        Need((unsigned char)copy[0]==0xa5 && (unsigned char)copy[256]==0xa5);
        for (unsigned i=0;i<255;i++) Need((unsigned char)copy[i+1]==i+1);
        for (unsigned i=0;i<3;i++) {
            const auto before=HelperRealtime();
            const double milliseconds=OS::TimeCurrentMillis();
            const auto after=HelperRealtime();
            Need(before.microseconds<=after.microseconds && __builtin_isfinite(milliseconds)
                && (double)(long long)before.microseconds/1000.0<=milliseconds
                && milliseconds<=(double)(long long)after.microseconds/1000.0);
            v8::base::gtos::Yield();
        }
        Need(errno==saved && OS::GetLastError()==saved);
        __atomic_add_fetch(&native_file_record.helper_calls,1U,__ATOMIC_RELAXED);
    }
    void PlatformHelperPages() {
        const int saved=errno;
        GtosVmReserveResult result_vm={};
        GtosVmReserveRequest reserve={GTOS_VM_ABI_VERSION,8192,4096,0,(unsigned)&result_vm};
        Need(Raw(GTOS_SYS_VM_RESERVE,(unsigned)&reserve,sizeof(reserve))==0);
        Need(result_vm.version==GTOS_VM_ABI_VERSION && result_vm.handle && result_vm.length==8192);
        GtosVmRangeRequest access={GTOS_VM_ABI_VERSION,result_vm.handle,0,4096,GTOS_VM_READ_WRITE};
        Need(Raw(GTOS_SYS_VM_SET_PERMISSIONS,(unsigned)&access,sizeof(access))==0);
        auto* pages=(char*)result_vm.base;
        char* source=pages+4093;source[0]='Q';source[1]='R';source[2]=0;
        char result[10];v8::base::OS::StrNCpy(result,10,source,10);
        Need(result[0]=='Q' && result[1]=='R');
        for (unsigned i=2;i<10;i++) Need(result[i]==0);
        v8::base::OS::StrNCpy(pages+4094,2,"UVW",2);
        Need(pages[4094]=='U' && pages[4095]=='V');
        GtosVmControlRequest release={GTOS_VM_ABI_VERSION,result_vm.handle,0};
        Need(Raw(GTOS_SYS_VM_RELEASE,(unsigned)&release,sizeof(release))==0 && errno==saved);
    }
    void InvalidPlatformCopy(unsigned mode) {
        char destination[32];for (char& byte:destination) byte=(char)0xa5;
        native_file_record.mask=(unsigned)destination;native_file_record.foreign=sizeof(destination);
        native_file_record.stage=7;
        using v8::base::OS;
        if (mode==6) OS::StrNCpy(destination,4,"abcdefgh",8);
        if (mode==7) OS::StrNCpy(destination,-1,"x",0);
        if (mode==8) OS::StrNCpy(nullptr,1,"x",1);
        if (mode==9) OS::StrNCpy(destination,32,nullptr,1);
        if (mode==10) OS::StrNCpy(destination,32,"x",(size_t)-1);
        Need(false);
    }
#endif
