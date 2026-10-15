#ifndef GTOS_DEBUG_SHARED_CASES_H
#define GTOS_DEBUG_SHARED_CASES_H
    using DebugTrace=v8::base::debug::StackTrace;
    __attribute__((noinline)) uintptr_t DebugCallerA() {
        DebugTrace trace;size_t count=0;const void* const* pc=trace.Addresses(&count);
        Need(count==1 && pc && pc[0]);asm volatile("nop":::"memory");return (uintptr_t)pc[0];
    }
    __attribute__((noinline)) uintptr_t DebugCallerB() {
        asm volatile("nop; nop":::"memory");
        DebugTrace trace;size_t count=0;const void* const* pc=trace.Addresses(&count);
        Need(count==1 && pc && pc[0]);asm volatile("nop; nop; nop":::"memory");return (uintptr_t)pc[0];
    }
    void DebugSharedCases() {
        const int saved=errno;
        Need(!v8::base::debug::EnableInProcessStackDumping());
        v8::base::debug::DisableSignalStackDump();
        v8::base::debug::DisableSignalStackDump();
        const uintptr_t a=DebugCallerA(),b=DebugCallerB();
        Need(a!=b && a>=(uintptr_t)&DebugCallerA && a<(uintptr_t)&DebugCallerA+512
            && b>=(uintptr_t)&DebugCallerB && b<(uintptr_t)&DebugCallerB+512);
        const void* addresses[62];
        for (unsigned i=0;i<62;i++) addresses[i]=(const void*)(0x12345678U+i);
        DebugTrace empty(nullptr,0);size_t count=17;
        Need(empty.Addresses(&count)==nullptr && count==0 && empty.ToString().empty());
        DebugTrace bounded(addresses,(size_t)-1);
        const void* const* stored=bounded.Addresses(&count);Need(stored && count==62);
        for (unsigned i=0;i<62;i++) Need(stored[i]==addresses[i]);
        DebugTrace copied(bounded);DebugTrace assigned(nullptr,0);assigned=copied;
        Need(assigned.Addresses(&count) && count==62 && assigned.ToString()==bounded.ToString());
        const void* three[]={(const void*)0x12345678U,(const void*)0x87654321U,(const void*)0x40001000U};
        DebugTrace trace(three,3);
        const char expected[]="#0 0x12345678\n#1 0x87654321\n#2 0x40001000\n";
        Need(trace.ToString()==expected);
        std::ostringstream stream;stream<<std::hex<<std::setprecision(3)<<std::setfill('x')<<std::setw(7);
        const auto flags=stream.flags();trace.OutputToStream(&stream);
        Need(stream.good() && stream.str()==expected && stream.flags()==flags
            && stream.width()==7 && stream.precision()==3 && stream.fill()=='x');
        Need(freopen("/native/io-err.txt","w+",stderr)==stderr);
        trace.Print();Need(fflush(stderr)==0 && fseek(stderr,0,SEEK_SET)==0);
        for (unsigned i=0;i<sizeof(expected)-1;i++) Need(getc(stderr)==expected[i]);
        Need(getc(stderr)==EOF && !ferror(stderr));
        using v8::base::VirtualAddressSpace;
        using Permission=v8::PagePermissions;
        VirtualAddressSpace space;
        const auto handle=v8::SharedMemoryHandle::FromPlatformHandle(-1);
        const uintptr_t page=space.AllocatePages(0,4096,4096,Permission::kReadWrite);Need(page!=0);
        auto* bytes=(unsigned char*)page;for (unsigned i=0;i<4096;i++) bytes[i]=(unsigned char)i;
        for (uint64_t offset:{0ULL,4096ULL,0xfffffffffffff000ULL}) {
            Need(space.AllocateSharedPages(page,4096,Permission::kReadWrite,handle,offset)==0);
            Need(space.AllocateSharedPages(0,4096,Permission::kReadWrite,handle,offset)==0);
        }
        for (unsigned i=0;i<4096;i++) Need(bytes[i]==(unsigned char)i);
        bytes[4095]=0x5a;Need(bytes[4095]==0x5a);bytes[4095]=0xff;
        Need(space.SetPagePermissions(page,4096,Permission::kRead));
        for (unsigned i=0;i<4096;i++) Need(bytes[i]==(unsigned char)i);
        space.FreePages(page,4096);
        auto subspace=space.AllocateSubspace(0,8192,4096,Permission::kReadWrite);Need(subspace!=nullptr);
        Need(!subspace->SetName("GTOS-native-1"));
        const auto subpage=subspace->AllocatePages(0,4096,4096,Permission::kReadWrite);Need(subpage!=0);
        *(volatile unsigned*)subpage=0x1234abcdU;
        Need(subspace->AllocateSharedPages(0,4096,Permission::kReadWrite,handle,0)==0);
        Need(*(volatile unsigned*)subpage==0x1234abcdU);
        subspace->FreePages(subpage,4096);subspace.reset();errno=saved;
        __atomic_add_fetch(&native_file_record.debug_shared_calls,1U,__ATOMIC_RELAXED);
    }
    void DebugClosedError() {
        const void* address=(const void*)0x12345678U;DebugTrace trace(&address,1);
        Need(fclose(stderr)==0 && stderr==nullptr);
        errno=0;trace.Print();Need(errno==EBADF && stderr==nullptr);
        native_file_record.debug_closed=1;
    }
    void InvalidSharedFree() {
        v8::base::VirtualAddressSpace space;
        const uintptr_t page=space.AllocatePages(0,4096,4096,v8::PagePermissions::kReadWrite);Need(page!=0);
        for (unsigned i=0;i<4096;i++) ((unsigned char*)page)[i]=0xa5;
        native_file_record.mask=page;native_file_record.foreign=32;native_file_record.stage=7;
        space.FreeSharedPages(page,4096);Need(false);
    }
#endif
