#ifndef GTOS_NATIVE_TEMPORARY_CASES_H
#define GTOS_NATIVE_TEMPORARY_CASES_H
    void TemporaryPair(const unsigned char* data,unsigned bytes,unsigned char* readback) {
        const int saved=errno;
        FILE* first=tmpfile();FILE* second=v8::base::OS::OpenTemporaryFile();
        Need(first && second && first!=second && fileno(first)!=fileno(second));
        Need(errno==saved && v8::base::OS::GetLastError()==saved);
        Need(fwrite(data,1,bytes,first)==bytes && fflush(first)==0);
        Need(fputc(0x5a,second)==0x5a && fflush(second)==0);
        Need(fseek(first,0,SEEK_SET)==0 && fread(readback,1,bytes,first)==bytes);
        for (unsigned i=0;i<bytes;i++) Need(readback[i]==data[i]);
        Need(getc(first)==EOF && feof(first) && !ferror(first));
        Need(fseek(second,0,SEEK_SET)==0 && getc(second)==0x5a && getc(second)==EOF);
        Need(fclose(first)==0 && fclose(second)==0);
        Need(errno==saved && v8::base::OS::GetLastError()==saved);
    }
#if defined(GTOS_TEMPORARY_THREADED_PROBE)
    void TemporaryWorker() {
        const unsigned char data[]={0,'t','m','p',0xff,'\n'};
        unsigned char readback[sizeof(data)]={};
        TemporaryPair(data,sizeof(data),readback);
        for (unsigned i=0;i<12;i++) {
            v8::base::gtos::Yield();Need(v8::base::OS::GetLastError()==errno);
        }
    }
#endif
    void TemporaryCases() {
        errno=EDOM;Need(v8::base::OS::GetLastError()==EDOM && errno==EDOM);
        Need(v8::base::OS::FOpen("/native/temporary-absent","r")==nullptr);
        Need(errno==ENOENT && v8::base::OS::GetLastError()==ENOENT);
        FILE* log=v8::base::OS::FOpen("/native/io-log.txt",v8::base::OS::LogFileOpenMode);
        Need(log && fputc('L',log)=='L' && fseek(log,0,SEEK_SET)==0 && getc(log)=='L' && fclose(log)==0);
        log=v8::base::OS::FOpen("/native/io-log.txt",v8::base::OS::LogFileOpenMode);
        Need(log && getc(log)==EOF && feof(log) && !ferror(log) && fclose(log)==0);

        // Force the first reserved name to collide. The old file must survive.
        char collision[64];
        Need(v8::base::OS::SNPrintF(collision,sizeof(collision),"/.gtos-tmp-%08x-%08x",
            v8::base::gtos::CurrentProcessInfo().process_id,1U)>0);
        FILE* sentinel=fopen(collision,"w+x");Need(sentinel && fputc('K',sentinel)=='K' && fclose(sentinel)==0);
        FILE* collided=tmpfile();Need(collided && fclose(collided)==0);
        sentinel=fopen(collision,"r");Need(sentinel && getc(sentinel)=='K' && getc(sentinel)==EOF && fclose(sentinel)==0);
        GtosFilePathRequest remove={GTOS_FILE_ABI_VERSION,(unsigned)collision,0};
        while (collision[remove.path_bytes]) ++remove.path_bytes;
        ++remove.path_bytes;Need(Raw(GTOS_SYS_FILE_REMOVE,(unsigned)&remove,sizeof(remove))==0);

        const unsigned count=4097;
        auto data=new unsigned char[count];auto readback=new unsigned char[count];Need(data && readback);
        for (unsigned i=0;i<count;i++) data[i]=(unsigned char)(i*37U+11U);
        errno=EDOM;
        for (unsigned i=0;i<4;i++) TemporaryPair(data,count,readback);
        delete[] data;delete[] readback;

        FILE* held[64]={};
        unsigned allocated=0;
        for (;allocated<64;allocated++) {
            errno=0;held[allocated]=tmpfile();
            if (!held[allocated]) break;
            Need(fputc((int)allocated,held[allocated])==(int)allocated);
        }
        native_file_record.mask=allocated;native_file_record.foreign=(unsigned)errno;
        Need(allocated>0 && allocated<64 && errno==ENOMEM && v8::base::OS::GetLastError()==ENOMEM);
        for (unsigned i=0;i<allocated;i++) Need(fclose(held[i])==0);
        native_file_record.temporary_heap_files=allocated;
        FILE* after_oom=tmpfile();Need(after_oom && fclose(after_oom)==0);

        const char raw_path[]="/.gtos-native-raw-temporary";
        GtosFileOpenRequest open={GTOS_FILE_ABI_VERSION,(unsigned)raw_path,sizeof(raw_path),
            GTOS_FILE_READ_WRITE|GTOS_FILE_CREATE|GTOS_FILE_EXCLUSIVE|GTOS_FILE_TEMPORARY};
        unsigned handles[64]={};
        for (unsigned i=0;i<64;i++) { const int handle=Raw(GTOS_SYS_FILE_OPEN,(unsigned)&open,sizeof(open));Need(handle>0);handles[i]=(unsigned)handle; }
        errno=0;Need(tmpfile()==nullptr && errno==EMFILE && v8::base::OS::GetLastError()==EMFILE);
        for (unsigned i=0;i<64;i++) { GtosFileControlRequest close={GTOS_FILE_ABI_VERSION,handles[i]};Need(Raw(GTOS_SYS_FILE_CLOSE,(unsigned)&close,sizeof(close))==0); }
        FILE* recovered=tmpfile();Need(recovered && fclose(recovered)==0);

        native_file_record.command=4;errno=0;
        Need(tmpfile()==nullptr && errno==EIO && v8::base::OS::GetLastError()==EIO);
        native_file_record.command=0;
        FILE* after_error=tmpfile();Need(after_error && fclose(after_error)==0);
        const unsigned valid=GTOS_FILE_READ_WRITE|GTOS_FILE_CREATE|GTOS_FILE_EXCLUSIVE|GTOS_FILE_TEMPORARY;
        const unsigned invalid[]={GTOS_FILE_TEMPORARY,valid&~GTOS_FILE_CREATE,valid&~GTOS_FILE_EXCLUSIVE,
            (valid&~GTOS_FILE_READ_WRITE)|GTOS_FILE_READ,(valid&~GTOS_FILE_READ_WRITE)|GTOS_FILE_WRITE,
            valid|GTOS_FILE_TRUNCATE,valid|GTOS_FILE_APPEND,valid|0x80000000U};
        native_file_record.invalid_io=1;
        for (unsigned flags:invalid) { open.flags=flags;Need(Raw(GTOS_SYS_FILE_OPEN,(unsigned)&open,sizeof(open))==-EINVAL); }
        native_file_record.invalid_io=0;
        native_file_record.command=5;errno=0;
        Need(tmpfile()==nullptr && errno==EROFS && v8::base::OS::GetLastError()==EROFS);
        native_file_record.command=0;
        FILE* after_readonly=tmpfile();Need(after_readonly && fclose(after_readonly)==0);
        native_file_record.temporary_checks=1;
    }
    void RetainTemporary() {
        FILE* retained=v8::base::OS::OpenTemporaryFile();
        unsigned char data[1025];for (unsigned i=0;i<sizeof(data);i++) data[i]=(unsigned char)i;
        Need(retained && fwrite(data,1,sizeof(data),retained)==sizeof(data) && fflush(retained)==0);
        native_file_record.temporary_live=1;
    }
#endif
