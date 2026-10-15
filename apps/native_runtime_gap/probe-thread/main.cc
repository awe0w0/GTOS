#include <iostream>
#include <iomanip>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <new>
#include <wchar.h>
#include <math.h>
#include <fenv.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <process/abi.h>
#include <process/file_abi.h>
#include <process/realtime_abi.h>
#include <mutex>
#include <condition_variable>
#include "src/base/platform/platform.h"
#include "src/base/platform/gtos-native-services.h"
#include "src/__support/File/file.h"
#include "heap.h"
#include "thread-exit.h"
#include "record.h"
extern "C" void* __dso_handle;
extern "C" volatile unsigned native_v8_integration_stage;
extern "C" unsigned native_helper_call_alignment();
asm(".section .text.native_helper_call_alignment,\"ax\"\n.global native_helper_call_alignment\nnative_helper_call_alignment:\nleal 4(%esp),%eax\nandl $15,%eax\nret\n.text\n");
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
    void NeedAt(bool pass,unsigned line) { __atomic_add_fetch(&native_file_record.checks,1U,__ATOMIC_RELAXED);if (!pass) Fail(line); }
#define Need(value) NeedAt((value),__LINE__)
#include "../runtime-cases-v4.h"
#include "platform-helper-cases.h"
#define GTOS_TEMPORARY_THREADED_PROBE 1
#include "temporary-cases.h"
    unsigned Load(const volatile unsigned& value) { return __atomic_load_n(&value,__ATOMIC_ACQUIRE); }
    void Store(volatile unsigned& value,unsigned next) { __atomic_store_n(&value,next,__ATOMIC_RELEASE); }
    void Add(volatile unsigned& value) { __atomic_add_fetch(&value,1U,__ATOMIC_ACQ_REL); }
    void WaitCount(const volatile unsigned& value,unsigned wanted) {
        while (Load(value)<wanted) v8::base::gtos::Yield();
        Need(Load(value)==wanted);
    }
    void Rounds() { for (unsigned i=0;i<12;i++) v8::base::gtos::Yield(); }
    void WaitStage(unsigned wanted) {
        for (unsigned i=0;i<3;i++) while (Load(native_file_record.thread_stage[i])<wanted) v8::base::gtos::Yield();
    }
    FILE* shared_file;
    std::mutex shared_mutex,notify_mutex;
    std::recursive_mutex shared_recursive;
    std::condition_variable notify_condition;
    std::once_flag shared_once,nested_once;
    std::__libcpp_exec_once_flag external_once=0;
    volatile unsigned allow_once,allow_external,once_magic,external_magic;
    const char thread_tokens[3][8]={{'0',':',0,'G','T','O','S','\n'},
        {'1',':',0,'G','T','O','S','\n'},{'2',':',0,'G','T','O','S','\n'}};
    void OnceCallback() {
        Add(native_file_record.thread_once_calls);
        std::call_once(nested_once,[] { Store(once_magic,0x13579); });
        while (!Load(allow_once)) v8::base::gtos::Yield();
        Store(once_magic,0xc011c011);
    }
    void ExternalCallback() {
        Add(native_file_record.thread_external_calls);
        Need(std::__libcpp_execute_once(&external_once,ExternalCallback)==EDEADLK);
        while (!Load(allow_external)) v8::base::gtos::Yield();
        Store(external_magic,0x123abc);
    }
    class Writer : public v8::base::Thread {
        unsigned index_;
    public:
        explicit Writer(unsigned index):Thread(Options("gtos-io-writer")),index_(index) {}
        void Run() override {
            const auto info=v8::base::gtos::CurrentProcessInfo();
            Need(info.maximum_parallel_threads==4 && info.thread_id!=info.process_id);
            Store(native_file_record.thread_ids[index_],info.thread_id);
            errno=(int)(EDOM+index_);
            TemporaryWorker();Need(v8::base::OS::GetLastError()==(int)(EDOM+index_));
            Need(!shared_mutex.try_lock() && std::__libcpp_mutex_unlock(shared_mutex.native_handle())==EPERM);
            Store(native_file_record.thread_stage[index_],1);
            Need(fwrite(thread_tokens[index_],1,8,shared_file)==8);Add(native_file_record.thread_file_done);
            Store(native_file_record.thread_stage[index_],2);
            std::cout.write(thread_tokens[index_],8);Need(std::cout.good());
            Need(printf("%c:%cPRNT\n",'0'+index_,0)==8);Add(native_file_record.thread_stdout_done);
            Store(native_file_record.thread_stage[index_],3);
            shared_mutex.lock();
            Need(shared_mutex.native_handle()->owner==info.thread_id && shared_mutex.native_handle()->depth==1);
            Need(std::__libcpp_mutex_lock(shared_mutex.native_handle())==EDEADLK);
            const unsigned previous=Load(native_file_record.thread_mutex_done);Rounds();
            Need(Load(native_file_record.thread_mutex_done)==previous);Add(native_file_record.thread_mutex_done);shared_mutex.unlock();
            Store(native_file_record.thread_stage[index_],4);
            shared_recursive.lock();shared_recursive.lock();Need(shared_recursive.try_lock());
            const unsigned recursive_before=Load(native_file_record.thread_recursive_done);Rounds();
            Need(Load(native_file_record.thread_recursive_done)==recursive_before);Add(native_file_record.thread_recursive_done);
            shared_recursive.unlock();shared_recursive.unlock();shared_recursive.unlock();
            Add(native_file_record.thread_once_enter);std::call_once(shared_once,OnceCallback);
            Need(Load(once_magic)==0xc011c011);Add(native_file_record.thread_once_done);
            Add(native_file_record.thread_external_enter);Need(std::__libcpp_execute_once(&external_once,ExternalCallback)==0);
            Need(Load(external_magic)==0x123abc);Add(native_file_record.thread_external_done);
            std::unique_lock<std::mutex> lock(notify_mutex);notify_condition.wait(lock);
            Need(lock.owns_lock() && notify_mutex.native_handle()->owner==info.thread_id);
            Add(native_file_record.thread_notify_done);lock.unlock();
            Need(errno==(int)(EDOM+index_));Store(native_file_record.thread_stage[index_],5);
        }
    };
    void ThreadCases() {
        const auto info=v8::base::gtos::CurrentProcessInfo();Need(info.maximum_parallel_threads==4 && info.thread_id==info.process_id);
        native_file_record.thread_profile=info.maximum_parallel_threads;
        Need(sizeof(std::__libcpp_mutex_t)==8 && sizeof(std::__libcpp_condvar_t)==8 && sizeof(std::once_flag)==4 && sizeof(__llvm_libc_cr::Mutex)==12);
        Need(freopen("/native/io-thread-out.txt","w",stdout)==stdout && setvbuf(stdout,nullptr,_IONBF,0)==0);
        shared_file=fopen("/native/io-thread-file.txt","w");Need(shared_file!=nullptr && setvbuf(shared_file,nullptr,_IONBF,0)==0);
        auto file=reinterpret_cast<__llvm_libc_cr::File*>(shared_file);
        auto output=reinterpret_cast<__llvm_libc_cr::File*>(stdout);
        file->lock();output->lock();shared_mutex.lock();shared_recursive.lock();shared_recursive.lock();
        Writer first(0),second(1),third(2);Need(first.Start() && second.Start() && third.Start());
        WaitStage(1);Rounds();Need(!Load(native_file_record.thread_file_done));Add(native_file_record.thread_blocked);file->unlock();
        WaitStage(2);Rounds();Need(Load(native_file_record.thread_file_done)==3 && !Load(native_file_record.thread_stdout_done));
        Add(native_file_record.thread_blocked);output->unlock();
        WaitStage(3);Rounds();Need(Load(native_file_record.thread_stdout_done)==3 && !Load(native_file_record.thread_mutex_done));
        Add(native_file_record.thread_blocked);shared_mutex.unlock();
        WaitStage(4);Rounds();Need(Load(native_file_record.thread_mutex_done)==3 && !Load(native_file_record.thread_recursive_done));
        shared_recursive.unlock();Rounds();Need(!Load(native_file_record.thread_recursive_done));
        Add(native_file_record.thread_blocked);shared_recursive.unlock();
        WaitCount(native_file_record.thread_once_enter,3);WaitCount(native_file_record.thread_once_calls,1);Rounds();
        Need(!Load(native_file_record.thread_once_done));Add(native_file_record.thread_blocked);Store(allow_once,1);
        WaitCount(native_file_record.thread_external_enter,3);WaitCount(native_file_record.thread_external_calls,1);Rounds();
        Need(Load(native_file_record.thread_once_done)==3 && !Load(native_file_record.thread_external_done));
        Add(native_file_record.thread_blocked);Store(allow_external,1);
        WaitCount(native_file_record.thread_external_done,3);
        WaitCount(notify_condition.native_handle()->waiters,3);notify_mutex.lock();notify_condition.notify_one();Rounds();
        Need(!Load(native_file_record.thread_notify_done) && Load(notify_condition.native_handle()->waiters)==2);
        Add(native_file_record.thread_blocked);notify_mutex.unlock();WaitCount(native_file_record.thread_notify_done,1);
        native_file_record.thread_signal_wakes=Load(native_file_record.thread_notify_done);
        notify_mutex.lock();Need(Load(notify_condition.native_handle()->waiters)==2);notify_condition.notify_all();Rounds();
        Need(Load(native_file_record.thread_notify_done)==1 && !Load(notify_condition.native_handle()->waiters));
        Add(native_file_record.thread_blocked);notify_mutex.unlock();WaitCount(native_file_record.thread_notify_done,3);
        native_file_record.thread_broadcast_wakes=Load(native_file_record.thread_notify_done);
        first.Join();Add(native_file_record.thread_joins);second.Join();Add(native_file_record.thread_joins);third.Join();Add(native_file_record.thread_joins);
        WaitStage(5);Need(fclose(shared_file)==0 && fflush(stdout)==0);
        Need(Load(native_file_record.thread_once_calls)==1 && Load(native_file_record.thread_external_calls)==1);
        for (unsigned i=0;i<3;i++) {
            Need(Load(native_file_record.thread_ids[i])!=info.thread_id);
            for (unsigned j=i+1;j<3;j++) Need(Load(native_file_record.thread_ids[i])!=Load(native_file_record.thread_ids[j]));
        }
    }
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
    Need(native_file_record.mode<11);
    errno=EDOM;PlatformHelpers();
    if (native_file_record.mode>=4) {
        RetainTemporary();native_file_record.stage=7;
        if (native_file_record.mode==4) asm volatile("ud2");
        if (native_file_record.mode>=6) InvalidPlatformCopy(native_file_record.mode);
        for (;;) v8::base::gtos::Yield();
    }
    PlatformHelperPages();
    RuntimePagesAndAllocation();
    FileEntrypoints();
    IncrementalDecode();
    TemporaryCases();
    RuntimeOutput();
    ThreadCases();
    RetainTemporary();
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
        Need(fclose(stdout)==0);errno=0;Need(printf("%d",42)==-1 && errno==EBADF);
        std::cout<<"X";std::cout.flush();Need(std::cout.bad());
        native_file_record.closed_failure=1;native_file_record.stage=7;
        Raw(GTOS_SYS_EXIT,33);for (;;) asm volatile("ud2");
    }
    if (native_file_record.mode==3) {
        native_file_record.stage=7;abort();
    }
    if (native_file_record.mode==1) exit(29);
    return 17;
}
