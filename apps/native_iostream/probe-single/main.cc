#include <iostream>
#include <iomanip>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <new>
#include <wchar.h>
#include <process/abi.h>
#include "heap.h"
#include "thread-exit.h"
#include "record.h"
extern "C" void* __dso_handle;
extern "C" volatile unsigned native_v8_integration_stage;
extern "C" {
volatile FileRecord native_file_record __attribute__((section(".data.file_record")))={1};
}
namespace {
    int Raw(unsigned op,unsigned address=0,unsigned bytes=0) {
        asm volatile("int $0x80":"+a"(op):"b"(address),"c"(bytes):"memory","cc");return (int)op;
    }
    [[noreturn]] void Fail(unsigned line) {
        native_file_record.ack=line;native_file_record.error=native_file_record.checks;
        Raw(GTOS_SYS_EXIT,91);for (;;) asm volatile("ud2");
    }
    void NeedAt(bool pass,unsigned line) { native_file_record.checks=native_file_record.checks+1;if (!pass) Fail(line); }
#define Need(value) NeedAt((value),__LINE__)
    void IncrementalDecode() {
        const unsigned char invalid[][2]={{0xc0},{0xc1},{0xf5},{0xf7},
            {0xe0,0x80},{0xed,0xa0},{0xf0,0x80},{0xf4,0x90}};
        const unsigned lengths[]={1,1,1,1,2,2,2,2};
        for (unsigned i=0;i<8;i++) {
            mbstate_t state={};wchar_t value=0x1234;errno=0;
            Need(mbrtowc(&value,(const char*)invalid[i],lengths[i],&state)==(size_t)-1);
            Need(errno==EILSEQ && value==0x1234);
            const auto bytes=(const unsigned char*)&state;
            for (unsigned j=0;j<sizeof(state);j++) Need(bytes[j]==0);
        }
        const unsigned char valid[][4]={{0xc2,0x80},{0xe0,0xa0,0x80},{0xed,0x9f,0xbf},
            {0xee,0x80,0x80},{0xf0,0x90,0x80,0x80},{0xf4,0x8f,0xbf,0xbf}};
        const unsigned valid_lengths[]={2,3,3,3,4,4};
        const unsigned scalars[]={0x80,0x800,0xd7ff,0xe000,0x10000,0x10ffff};
        for (unsigned i=0;i<6;i++) {
            mbstate_t state={};wchar_t value=0x1234;errno=EDOM;
            for (unsigned j=0;j<valid_lengths[i];j++) {
                const size_t result=mbrtowc(&value,(const char*)&valid[i][j],1,&state);
                const bool complete=j+1==valid_lengths[i];
                Need(result==(complete?1:(size_t)-2));
                Need(errno==EDOM && value==(wchar_t)(complete?scalars[i]:0x1234));
            }
            const auto bytes=(const unsigned char*)&state;
            for (unsigned j=0;j<sizeof(state);j++) Need(bytes[j]==0);
        }
    }
    void FileEntrypoints() {
        const unsigned before=native_file_record.checks;
        FILE* fp=fopen("/native/io-direct.txt","w+");Need(fp!=nullptr);
        Need(fputwc(L'A',fp)==L'A' && fputwc(0x3a9,fp)==0x3a9 && fputwc(0x1f600,fp)==0x1f600);
        Need(fputwc(0,fp)==0 && fputwc(L'Z',fp)==L'Z' && fflush(fp)==0);
        Need(fseek(fp,0,SEEK_SET)==0);errno=0;
        Need(getc(fp)==EOF && errno==EINVAL && ferror(fp));clearerr(fp);
        Need(getwc(fp)==L'A' && getwc(fp)==0x3a9);
        Need(ungetwc(0x3a9,fp)==0x3a9 && getwc(fp)==0x3a9);
        Need(getwc(fp)==0x1f600 && getwc(fp)==0 && getwc(fp)==L'Z');
        Need(getwc(fp)==WEOF && feof(fp));
        Need(ungetwc(0x1f600,fp)==0x1f600 && !feof(fp) && getwc(fp)==0x1f600);
        Need(getwc(fp)==WEOF && feof(fp));
        Need(ungetwc(WEOF,fp)==WEOF && feof(fp));
        errno=0;Need(ungetwc(0x110000,fp)==WEOF && errno==EILSEQ);
        Need(fseek(fp,0,SEEK_END)==0);errno=0;
        const wint_t written=fputwc(0xd800,fp);const int write_errno=errno,write_error=ferror(fp);
        native_file_record.mask=(unsigned)written;native_file_record.foreign=(unsigned)write_errno;
        native_file_record.bad_calls=(unsigned)write_error;
        Need(written==WEOF && write_errno==EILSEQ && write_error);clearerr(fp);
        Need(freopen("/native/io-direct.txt","r",fp)==fp);
        const unsigned char expected[]={'A',0xce,0xa9,0xf0,0x9f,0x98,0x80,0,'Z'};
        for (unsigned i=0;i<sizeof(expected);i++) Need(getc(fp)==expected[i]);
        Need(getc(fp)==EOF && feof(fp) && !ferror(fp));
        Need(ungetwc(L'Q',fp)==WEOF && ferror(fp));clearerr(fp);Need(fclose(fp)==0);
        fp=fopen("/native/io-invalid.txt","w");Need(fp!=nullptr);
        const unsigned char partial[]={0xe2,0x82};Need(fwrite(partial,1,sizeof(partial),fp)==sizeof(partial));
        Need(freopen("/native/io-invalid.txt","r",fp)==fp);errno=0;
        Need(getwc(fp)==WEOF && errno==EILSEQ && ferror(fp) && feof(fp));Need(fclose(fp)==0);
        errno=0;Need(getwc(stdin)==WEOF && errno==ENOSYS && ferror(stdin));clearerr(stdin);
        const unsigned char malformed[][4]={{0xc0,0x80},{0xe0,0x80,0x80},{0xed,0xa0,0x80},
            {0xf4,0x90,0x80,0x80},{0x80},{0xe2,0x28,0xa1},{0xf0,0x80,0x80,0x80}};
        const unsigned lengths[]={2,3,3,4,1,3,4};
        for (unsigned i=0;i<sizeof(lengths)/sizeof(lengths[0]);i++) {
            fp=fopen("/native/io-reject.txt","w");Need(fp!=nullptr);
            Need(fwrite(malformed[i],1,lengths[i],fp)==lengths[i]);
            Need(freopen("/native/io-reject.txt","r",fp)==fp);errno=0;
            const wint_t decoded=getwc(fp);const int decode_errno=errno,decode_error=ferror(fp);
            native_file_record.mask=(unsigned)decoded;native_file_record.foreign=(unsigned)decode_errno;
            native_file_record.bad_calls=(unsigned)decode_error;native_file_record.progress=i;
            Need(decoded==WEOF && decode_errno==EILSEQ && decode_error);Need(fclose(fp)==0);
        }
        fp=fopen("/native/io-scalar.txt","w+");Need(fp!=nullptr);
        const unsigned scalars[]={0x7f,0x80,0x7ff,0x800,0xd7ff,0xe000,0xffff,0x10000,0x10ffff};
        for (unsigned value:scalars) Need(fputwc((wchar_t)value,fp)==(wint_t)value);
        Need(fseek(fp,0,SEEK_SET)==0);
        for (unsigned value:scalars) Need(getwc(fp)==(wint_t)value);
        Need(getwc(fp)==WEOF && feof(fp));Need(fseek(fp,0,SEEK_END)==0);
        const unsigned invalid_scalars[]={0xdfff,0x110000,0xffffffff};
        for (unsigned value:invalid_scalars) {
            errno=0;Need(fputwc((wchar_t)value,fp)==WEOF && errno==EILSEQ && ferror(fp));clearerr(fp);
        }
        Need(fclose(fp)==0);
        native_file_record.png_bytes=native_file_record.checks-before;
    }
    class ForwardBuffer : public std::streambuf {
        std::streambuf* real_;unsigned kind_;
    public:
        ForwardBuffer(std::streambuf* real,unsigned kind):real_(real),kind_(kind) { Need(real_!=nullptr); }
    protected:
        std::streamsize xsputn(const char* p,std::streamsize n) override { return real_->sputn(p,n); }
        int_type overflow(int_type c) override {
            if (traits_type::eq_int_type(c,traits_type::eof())) return traits_type::not_eof(c);
            return real_->sputc(traits_type::to_char_type(c));
        }
        int sync() override {
            if (native_file_record.directory==2) {
                if (kind_==1) native_file_record.cout_sync=native_file_record.cout_sync+1;
                else native_file_record.clog_sync=native_file_record.clog_sync+1;
            }
            return real_->pubsync();
        }
    };
    // Retained observation buffers forward every operation to real FILE-backed
    // buffers. They outlive DoIOSInit; no fake output or lifetime provider.
    alignas(ForwardBuffer) unsigned char cout_storage[sizeof(ForwardBuffer)],clog_storage[sizeof(ForwardBuffer)];
    struct Lifetime {
        Lifetime() {
            Need(native_v8_integration_stage==1);
            Need(std::cout.rdbuf() && std::cerr.rdbuf() && std::cin.rdbuf() && std::clog.rdbuf());
            Need(std::wcout.rdbuf() && std::wcerr.rdbuf() && std::wcin.rdbuf() && std::wclog.rdbuf());
            Need(std::cin.tie()==&std::cout && std::cerr.tie()==&std::cout);
            Need(std::wcin.tie()==&std::wcout && std::wcerr.tie()==&std::wcout);
            Need((std::cerr.flags()&std::ios::unitbuf)!=0 && (std::wcerr.flags()&std::ios::unitbuf)!=0);
            std::cout<<"IO PREMAIN\n";std::cout.flush();Need(std::cout.good());
            native_file_record.preinit=1;native_file_record.directory=1;
        }
        ~Lifetime() {
            Need(native_v8_integration_stage==3 && native_file_record.raw_calls==3);
            std::cout<<"G";std::clog<<"g";Need(std::cout.good() && std::clog.good());
            native_file_record.raw_calls=7;native_file_record.directory=2;
        }
    } lifetime;
    void OnTls(void*) {
        Need(native_v8_integration_stage==3 && native_file_record.raw_calls==0);
        std::cout<<"T";Need(std::cout.good());native_file_record.raw_calls=1;
    }
    void OnC() {
        Need(native_file_record.raw_calls==1);std::cout<<"C";Need(std::cout.good());native_file_record.raw_calls=3;
    }
    void FinalCheck() {
        Need(native_file_record.raw_calls==7 && native_file_record.directory==2);
        Need(native_file_record.cout_sync==1 && native_file_record.clog_sync==1);
        native_file_record.stage=2;
    }
    using Initializer=void(*)();
    __attribute__((section(".fini_array.02000"),used)) Initializer final_check=FinalCheck;
}
extern "C" int NativeV8IsolateMain() {
    Need(native_v8_integration_stage==2 && native_file_record.preinit==1);
    Need(native_file_record.mode<4);
    FileEntrypoints();
    IncrementalDecode();
    Need(freopen("/native/io-out.txt","w",stdout)==stdout);
    Need(freopen("/native/io-err.txt","w",stderr)==stderr);
    Need(setvbuf(stdout,nullptr,_IOFBF,128)==0 && setvbuf(stderr,nullptr,_IOFBF,128)==0);
    auto out=new(cout_storage) ForwardBuffer(std::cout.rdbuf(),1);
    auto log=new(clog_storage) ForwardBuffer(std::clog.rdbuf(),2);
    std::cout.rdbuf(out);std::clog.rdbuf(log);
    std::cout<<std::hex<<std::setw(4)<<std::setfill('0')<<42<<":"<<std::dec<<std::fixed<<std::setprecision(2)<<1.25<<"\n";
    std::cerr<<"E";Need(std::cerr.good() && std::cout.good());
    std::wcout<<L"W\u03a9\n";std::wcout.flush();Need(std::wcout.good());
    std::wcerr<<L"R\u03a9";Need(std::wcerr.good());
    std::clog<<"L";std::cout<<"M";Need(std::clog.good() && std::cout.good());
    Need(atexit(OnC)==0 && __cxa_thread_atexit(OnTls,nullptr,&__dso_handle)==0);
    GtosVmRegionInfo heap={};Need(gtos_native_heap_query(&heap)==1);
    native_file_record.base=heap.base;native_file_record.vm_handle=heap.handle;native_file_record.foreign=1;
    if (native_file_record.mode==2) {
        Need(fclose(stdout)==0);std::cout<<"X";std::cout.flush();Need(std::cout.bad());
        native_file_record.closed_failure=1;native_file_record.stage=7;
        Raw(GTOS_SYS_EXIT,33);for (;;) asm volatile("ud2");
    }
    if (native_file_record.mode==3) {
        native_file_record.stage=7;abort();
    }
    if (native_file_record.mode==1) exit(29);
    return 17;
}
