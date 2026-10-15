#ifndef GTOS_NATIVE_TEMPORARY_OBSERVER_H
#define GTOS_NATIVE_TEMPORARY_OBSERVER_H
    uint32_t temporaryOpens[32];
    void TrapReadable(uint32_t cr3,uint32_t address,uint32_t bytes) {
        FileNeed(bytes && address>=0x40000000U && address<0xc0000000U && bytes<=0xc0000000U-address,"temporary request user range");
        const uint32_t last=(address+bytes-1)>>12;
        for (uint32_t page=address>>12;page<=last;page++) {
            const uint32_t pde=((const uint32_t*)cr3)[page>>10];
            FileNeed((pde&5)==5 && !(pde&0x80) && fileFrames->isAllocated(pde&~4095U),"temporary request user PDE");
            const uint32_t pte=((const uint32_t*)(pde&~4095U))[page&1023];
            FileNeed((pte&5)==5 && fileFrames->isAllocated(pte&~4095U),"temporary request user PTE");
        }
    }
    void ObserveTemporary(uint32_t id,uint32_t cr3,const CPUState& before,int32_t result) {
        if (before.eax!=GTOS_SYS_FILE_OPEN || result<0) return;
        TrapReadable(cr3,before.ebx,sizeof(GtosFileOpenRequest));
        const GtosFileOpenRequest request=*(const GtosFileOpenRequest*)before.ebx;
        if (!(request.flags&GTOS_FILE_TEMPORARY)) return;
        FileNeed(request.version==GTOS_FILE_ABI_VERSION && request.flags==(GTOS_FILE_READ_WRITE|GTOS_FILE_CREATE|GTOS_FILE_EXCLUSIVE|GTOS_FILE_TEMPORARY)
            && request.path_bytes>=2 && request.path_bytes<=GTOS_FILE_PATH_LIMIT,"actual exclusive temporary request");
        TrapReadable(cr3,request.path,request.path_bytes);
        char path[GTOS_FILE_PATH_LIMIT];
        for (uint32_t i=0;i<request.path_bytes;i++) path[i]=((const char*)request.path)[i];
        FileNeed(!path[request.path_bytes-1],"actual temporary path ends in NUL");
        FileStore::Info info={};
        FileNeed(store->Stat(path,info)==LFS_ERR_NOENT,"temporary name removed inside the same native call");
        FileNeed(store->HandleInfo(id,(uint32_t)result,info)==0 && info.type==LFS_TYPE_REG && !info.size,"temporary handle has the real owner and empty regular file");
        temporaryOpens[id]++;
    }
#endif
