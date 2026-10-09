#include <storage/filestore.h>
#include <drivers/ata.h>
using namespace gtos;
using namespace gtos::storage;
using namespace gtos::drivers;
extern "C" uint8_t filesystem_stack_bottom,filesystem_stack_top;
namespace {
    uint32_t checks;
    uint32_t cutCountdown;
    uint8_t data[9000],copy[9000],sector[512];
    char longPath[513];
    uint32_t handles[FileStore::MaximumHandles];
    void Print(const char* text) { while (*text) asm volatile("outb %0,$0xe9" : : "a"(*text++)); }
    void Hex(uint32_t value) {
        for (int shift=28;shift>=0;shift-=4) {
            char c="0123456789ABCDEF"[(value>>shift)&15];asm volatile("outb %0,$0xe9" : : "a"(c));
        }
    }
    void Finish(bool pass) __attribute__((noreturn));
    void Finish(bool pass) {
        Print(pass?"FILESTORE GUEST PASS\n":"FILESTORE GUEST FAIL\n");
        asm volatile("outl %0,%1" : : "a"(pass?0x10U:0x20U),"Nd"((uint16_t)0xf4));
        for (;;) asm volatile("cli; hlt");
    }
    void Require(bool value,const char* text) {
        checks++;if (!value) { Print("FAILED ");Print(text);Print("\n");Finish(false); }
    }
    bool Equal(const void* a,const void* b,uint32_t size) {
        const uint8_t* x=(const uint8_t*)a;const uint8_t* y=(const uint8_t*)b;
        for (uint32_t i=0;i<size;i++) if (x[i]!=y[i]) return false;
        return true;
    }
    bool Contains(const char* text,const char* word) {
        if (!text) return false;
        for (;*text;text++) {
            uint32_t i=0;while (word[i]&&text[i]==word[i]) i++;if (!word[i]) return true;
        }
        return false;
    }
    class ObservedDisk:public BlockDevice {
        AdvancedTechnologyAttachment device;
    public:
        uint32_t reads,writes,flushes,lowStack;
        bool failRead,failWrite,failFlush;
        ObservedDisk():device(0x1f0,true),reads(0),writes(0),flushes(0),lowStack(0xffffffffU),
            failRead(false),failWrite(false),failFlush(false) {}
        void Stack() {
            uint32_t sp;asm volatile("mov %%esp,%0":"=r"(sp));
            if (sp<lowStack) lowStack=sp;
            Require(sp>(uint32_t)&filesystem_stack_bottom+512,"ATA stack remains inside fixed 16 KiB fixture");
        }
        virtual bool Identify() { return device.Identify(); }
        virtual uint32_t SectorCount() const { return device.SectorCount(); }
        virtual bool ReadSector(uint32_t s,uint8_t* p) {
            Stack();reads++;return !failRead&&device.ReadSector(s,p);
        }
        virtual bool WriteSector(uint32_t s,const uint8_t* p) {
            Stack();writes++;bool result=!failWrite&&device.WriteSector(s,p);
            if (result&&cutCountdown&&!--cutCountdown) {
                Print("CUT AFTER COMPLETE ATA SECTOR writes=");Hex(writes);Print("\n");
                asm volatile("outl %0,%1" : : "a"(0x30U),"Nd"((uint16_t)0xf4));
                for (;;) asm volatile("cli; hlt");
            }
            return result;
        }
        virtual bool Flush() { Stack();flushes++;return !failFlush&&device.Flush(); }
    };
    void GuardSectors(ObservedDisk& disk) {
        for (uint32_t s=0;s<8;s++) {
            Require(disk.ReadSector(s,sector),"prefix read");
            for (uint32_t i=0;i<512;i++) Require(sector[i]==(uint8_t)(0xA5+s),"prefix outside volume unchanged");
        }
        for (uint32_t s=2056;s<2064;s++) {
            Require(disk.ReadSector(s,sector),"suffix read");
            for (uint32_t i=0;i<512;i++) Require(sector[i]==(uint8_t)(0x5A+s-2056),"suffix outside volume unchanged");
        }
    }
    void Verify(FileStore& files) {
        int32_t f=files.Open(7,"/browser/session.bin",FileStore::Read);
        Require(f>0,"persisted session open");
        Require(files.Size(7,f)==8197,"persisted session size");
        Require(files.ReadFile(7,f,copy,9000)==8197,"persisted session full read");
        for (uint32_t i=0;i<8193;i++) Require(copy[i]==(uint8_t)((i*37+i/251+17)&255),"persisted session pattern");
        Require(Equal(copy+8193,"GTOS",4),"append survived cold boot");
        Require(files.ReadFile(7,f,copy,1)==0&&files.Close(7,f)==0,"EOF and close");
        f=files.Open(7,"/browser/hole.bin",FileStore::Read);
        Require(f>0&&files.ReadFile(7,f,copy,9000)==2048,"persisted hole full read");
        Require(Equal(copy,"HEAD",4),"hole prefix");
        for (uint32_t i=4;i<2048;i++) Require(copy[i]==0,"seek and truncate extension zero filled");
        Require(files.Close(7,f)==0,"hole close");
        f=files.Open(7,"/browser/中文.txt",FileStore::Read);
        Require(f>0&&files.ReadFile(7,f,copy,9000)==12&&Equal(copy,"原生文件",12),"UTF8 path and payload");
        Require(files.Close(7,f)==0,"UTF8 close");
        f=files.Open(7,"/browser/history/owner.bin",FileStore::Read);
        Require(f>0&&files.ReadFile(7,f,copy,9000)==6&&Equal(copy,"REAPED",6),"reclaim synced owned file");
        Require(files.Close(7,f)==0,"reclaimed persisted close");
        FileStore::Info info={};
        Require(files.Stat("/browser",info)==0&&info.type==LFS_TYPE_DIR,"directory stat");
        Require(files.Stat("/browser/session.bin",info)==0&&info.size==8197&&info.type==LFS_TYPE_REG,"file stat");
        int32_t d=files.OpenDirectory(7,"/browser");
        Require(d>0,"open directory");uint32_t entries=0;int32_t result;
        while ((result=files.ReadDirectory(7,d,info))>0) entries++;
        Require(result==0&&entries>=6,"directory iteration includes namespace");
        info.size=0xdec0de;
        Require(files.ReadDirectory(7,d,info)==0&&info.size==0xdec0de,"EOF does not alter output");
        Require(files.RewindDirectory(7,d)==0&&files.ReadDirectory(7,d,info)==1,"directory rewind");
        Require(files.ReadFile(7,d,copy,1)==LFS_ERR_BADF&&files.Close(7,d)==0,"directory typed handle");
        Require(files.OpenCount()==0,"all verification handles closed");
    }
    void WriteFixture(FileStore& files,ObservedDisk& disk) {
        const uint32_t before=disk.writes;
        Require(files.Mount(&disk,8,2048)==LFS_ERR_CORRUPT&&!files.Mounted(),"blank mount fails without formatting");
        Require(disk.writes==before,"blank mount writes nothing");
        Require(files.Mount(&disk,0xffffffffU,16)==LFS_ERR_INVAL,"overflow first sector");
        Require(files.Mount(&disk,8,0xfffffff8U)==LFS_ERR_INVAL,"overflow volume length");
        Require(files.Mount(&disk,1,2048)==LFS_ERR_INVAL&&files.Mount(&disk,8,2047)==LFS_ERR_INVAL,"unaligned geometry");
        Require(files.Mount(&disk,8,8)==LFS_ERR_INVAL&&files.Mount(0,8,2048)==LFS_ERR_INVAL,"undersized and null device");
        Require(disk.writes==before,"invalid geometry never writes");
        Require(files.Format(&disk,8,2048)==0,"explicit format dedicated fixture");
        Require(files.Mount(&disk,8,2048)==0,"RW mount");
        Require(files.Mount(&disk,8,2048)==FileStore::Busy&&files.Format(&disk,8,2048)==FileStore::Busy,"active volume rejects mount and format");
        Require(files.MakeDirectory("/browser")==0&&files.MakeDirectory("/browser/cache")==0,"create real directories");
        Require(files.MakeDirectory("/browser/cache")==LFS_ERR_EXIST,"duplicate directory");
        Require(files.Open(0,"/browser/session.bin",FileStore::Read)==LFS_ERR_BADF,"zero owner rejected");
        Require(files.Open(1,0,FileStore::Read)==LFS_ERR_INVAL&&files.Open(1,"",1)==LFS_ERR_INVAL,"null and empty paths");
        for (uint32_t i=0;i<sizeof(longPath);i++) longPath[i]='x';
        Require(files.Open(1,longPath,1)==LFS_ERR_NAMETOOLONG,"bounded missing path terminator");
        Require(files.Open(1,"/bad",0)==LFS_ERR_INVAL&&files.Open(1,"/bad",0x10000|3)==LFS_ERR_INVAL,"invalid access and flag");
        Require(files.Open(1,"/bad",1|FileStore::Exclusive)==LFS_ERR_INVAL,"exclusive requires create");
        Require(files.Open(1,"/bad",1|FileStore::Truncate)==LFS_ERR_INVAL,"readonly truncate rejected before upstream assertion");
        int32_t f=files.Open(11,"/browser/session.bin",FileStore::ReadWrite|FileStore::Create|FileStore::Exclusive);
        Require(f>0,"create session");
        Require(files.Open(11,"/browser/session.bin",FileStore::Write|FileStore::Create|FileStore::Exclusive)==LFS_ERR_EXIST,"exclusive existing");
        Require(files.Unmount()==FileStore::Busy,"open handles prevent unmount");
        Require(files.WriteFile(12,f,"BAD",3)==LFS_ERR_BADF&&files.Close(12,f)==LFS_ERR_BADF,"foreign owner cannot modify or close");
        Require(files.ReadFile(11,f,0,1)==LFS_ERR_INVAL&&files.WriteFile(11,f,0,1)==LFS_ERR_INVAL,"null buffers");
        Require(files.ReadFile(11,f,0,0)==0&&files.WriteFile(11,f,0,0)==0,"zero transfers");
        Require(files.ReadFile(11,f,copy,0xffffffffU)==LFS_ERR_INVAL&&files.WriteFile(11,f,data,0xffffffffU)==LFS_ERR_INVAL,"oversized transfer count");
        Require(files.Seek(11,f,-1,0)==LFS_ERR_INVAL&&files.Seek(11,f,0,3)==LFS_ERR_INVAL,"negative and invalid seek origin");
        Require(files.Seek(11,f,0x7fffffffffffffffLL,0)==FileStore::Overflow,"INT64_MAX seek no overflow");
        Require(files.Seek(11,f,(-0x7fffffffffffffffLL-1),1)==LFS_ERR_INVAL,"INT64_MIN seek no overflow");
        Require(files.TruncateFile(11,f,0xffffffffU)==LFS_ERR_FBIG,"oversized truncate");
        for (uint32_t i=0;i<8193;i++) data[i]=(i*37+i/251+17)&255;
        Require(files.WriteFile(11,f,data,8193)==8193,"multi-block write");
        Require(files.Seek(11,f,-8193,1)==0&&files.ReadFile(11,f,copy,8193)==8193&&Equal(data,copy,8193),"seek and exact data");
        Require(files.Sync(11,f)==0&&files.Close(11,f)==0,"explicit sync and close");
        uint32_t stale=f;
        f=files.Open(11,"/browser/session.bin",FileStore::Write|FileStore::Append);
        Require(f>0&&(uint32_t)f!=stale,"reused slot gets new handle");
        Require(files.ReadFile(11,f,copy,1)==LFS_ERR_BADF&&files.Close(11,stale)==LFS_ERR_BADF,"write only and stale handle");
        Require(files.Seek(11,f,0,0)==0&&files.WriteFile(11,f,"GTOS",4)==4&&files.Size(11,f)==8197,"append ignores prior seek for writing");
        Require(files.Close(11,f)==0,"append close");
        int32_t a=files.Open(11,"/browser/session.bin",FileStore::Read),b=files.Open(11,"/browser/session.bin",FileStore::Read);
        Require(a>0&&b>0&&a!=b,"independent descriptors");
        Require(files.ReadFile(11,a,copy,31)==31&&files.ReadFile(11,b,copy+31,1)==1&&copy[31]==data[0],"independent offsets");
        Require(files.WriteFile(11,a,"BAD",3)==LFS_ERR_BADF&&files.TruncateFile(11,a,0)==LFS_ERR_BADF,"read only descriptor permission");
        Require(files.Close(11,a)==0&&files.Close(11,b)==0,"independent close");
        f=files.Open(11,"/browser/hole.bin",FileStore::ReadWrite|FileStore::Create|FileStore::Truncate);
        Require(f>0&&files.WriteFile(11,f,"HEAD",4)==4&&files.Seek(11,f,4098,0)==4098,"seek beyond EOF");
        Require(files.WriteFile(11,f,"END",3)==3&&files.Size(11,f)==4101,"write hole extends file");
        Require(files.Seek(11,f,4,0)==4&&files.ReadFile(11,f,copy,4094)==4094,"hole read");
        for (uint32_t i=0;i<4094;i++) Require(copy[i]==0,"hole bytes zero");
        Require(files.TruncateFile(11,f,32)==0&&files.Size(11,f)==32&&files.TruncateFile(11,f,2048)==0,"shrink and extend");
        Require(files.Close(11,f)==0,"hole close");
        f=files.Open(11,"/browser/中文.txt",FileStore::Write|FileStore::Create);
        Require(f>0&&files.WriteFile(11,f,"原生文件",12)==12&&files.Close(11,f)==0,"UTF8 file");
        f=files.Open(33,"/browser/cache/owner.bin",FileStore::Write|FileStore::Create);
        Require(f>0&&files.WriteFile(33,f,"REAPED",6)==6,"owner pending write");
        int32_t d=files.OpenDirectory(33,"/browser");
        int32_t peer=files.Open(44,"/browser/session.bin",FileStore::Read);
        Require(d>0&&peer>0&&files.OpenCount()==3,"file and directory owner set");
        FileStore::ReclaimResult result=files.Reclaim(33);
        Require(result.released==2&&result.error==0&&files.OpenCount()==1,"owner reclaim closes and commits only owned handles");
        Require(files.Close(33,f)==LFS_ERR_BADF&&files.Close(33,d)==LFS_ERR_BADF,"reclaimed handles invalid");
        Require(files.ReadFile(44,peer,copy,1)==1&&files.Close(44,peer)==0,"peer survives reclaim");
        Require(files.Rename("/browser/cache","/browser/history")==0,"directory rename with content");
        for (uint32_t i=0;i<FileStore::MaximumHandles;i++) {
            int32_t h=files.Open(55,"/browser/session.bin",FileStore::Read);Require(h>0,"fill real descriptor table");handles[i]=h;
        }
        Require(files.Open(55,"/browser/session.bin",1)==FileStore::TooManyHandles,"descriptor exhaustion explicit");
        Require(files.OpenDirectory(55,"/browser")==FileStore::TooManyHandles,"directories share descriptor limit");
        result=files.Reclaim(55);
        Require(result.released==FileStore::MaximumHandles&&!result.error&&!files.OpenCount(),"full table reclaim returns every slot");
        for (uint32_t i=0;i<FileStore::MaximumHandles;i++) Require(files.Close(55,handles[i])==LFS_ERR_BADF,"all old table handles stale");
        for (uint32_t i=0;i<100;i++) {
            f=files.Open(66,"/browser/session.bin",1);Require(f>0&&files.Close(66,f)==0,"repeated allocate and release");
        }
        f=files.Open(11,"/temporary",FileStore::Write|FileStore::Create);
        Require(f>0&&files.WriteFile(11,f,"TMP",3)==3&&files.Close(11,f)==0,"temporary file");
        Require(files.Rename("/temporary","/renamed")==0&&files.Remove("/renamed")==0,"rename and remove file");
        Require(files.Remove("/browser")==LFS_ERR_NOTEMPTY,"nonempty directory protected");
        f=files.Open(11,"/browser/session.bin",FileStore::Read);
        Require(f>0&&files.Seek(11,f,-4,2)==8193&&files.ReadFile(11,f,copy,4)==4&&Equal(copy,"GTOS",4),"SEEK_END relative read");
        Require(files.Seek(11,f,0x7fffffffffffffffLL,1)==FileStore::Overflow,"relative INT64_MAX rejects before addition");
        Require(files.Close(11,f)==0,"end seek close");
        for (uint32_t i=0;i<255;i++) longPath[i]='z';
        longPath[255]=0;
        f=files.Open(11,longPath,FileStore::Write|FileStore::Create);
        Require(f>0&&files.Close(11,f)==0&&files.Remove(longPath)==0,"full upstream 255-byte filename");
        longPath[255]='z';longPath[256]=0;
        Require(files.Open(11,longPath,FileStore::Write|FileStore::Create)==LFS_ERR_NAMETOOLONG,"256-byte filename rejected");
        Require(files.Open(11,"/browser",FileStore::Read)==LFS_ERR_ISDIR,"file open rejects directory");
        f=files.Open(11,"/full.bin",FileStore::Write|FileStore::Create);
        Require(f>0,"full-volume fixture open");
        uint32_t filled=0;int32_t written=0;
        do { written=files.WriteFile(11,f,data,9000);filled++; } while (written==9000&&filled<200);
        Require(written==LFS_ERR_NOSPC&&filled<200,"actual allocator reports bounded volume exhaustion");
        Require(files.Close(11,f)==0&&files.Remove("/full.bin")==0,"failed full-volume file releases allocation");
        f=files.Open(11,"/after-full.bin",FileStore::Write|FileStore::Create);
        Require(f>0&&files.WriteFile(11,f,data,8193)==8193&&files.Close(11,f)==0&&files.Remove("/after-full.bin")==0,"space reused after exhaustion");
        Require(files.MakeDirectory("/empty")==0&&files.Remove("/empty")==0,"empty directory removal");
        Verify(files);GuardSectors(disk);
        Require(files.Unmount()==0,"writer unmount");
        Require(files.Mount(&disk,8,2048,true)==0,"same object readonly remount");
        f=files.Open(11,"/browser/session.bin",FileStore::Read);
        Require(f>0&&(uint32_t)f>stale&&files.Close(11,stale)==LFS_ERR_BADF,"remount does not resurrect previous handle");
        Require(files.Close(11,f)==0&&files.Unmount()==0,"remount close");
    }
    void ReadOnlyFixture(FileStore& files,ObservedDisk& disk) {
        Require(files.Mount(&disk,8,2048,true)==0,"read only mount");
        uint32_t before=disk.writes;
        int32_t host=files.Open(7,"/host.txt",FileStore::Read);
        Require(host>0&&files.ReadFile(7,host,copy,9000)==19&&Equal(copy,"HOST-LITTLEFS-2.11.3",19),"upstream host-created file read by actual GTOS");
        Require(files.Close(7,host)==0,"host-created file close");Verify(files);
        Require(files.Open(7,"/browser/session.bin",FileStore::Write)==FileStore::ReadOnly,"RO volume rejects writing descriptor");
        Require(files.Open(7,"/new",FileStore::Read|FileStore::Create)==FileStore::ReadOnly,"RO volume rejects create");
        Require(files.MakeDirectory("/new")==FileStore::ReadOnly&&files.Remove("/browser/session.bin")==FileStore::ReadOnly,"RO namespace changes");
        Require(files.Rename("/browser","/new")==FileStore::ReadOnly,"RO rename");
        Require(files.Unmount()==0&&disk.writes==before,"RO operations issued no writes");
    }
    void CutFixture(FileStore& files,ObservedDisk& disk,uint32_t limit) {
        Require(files.Mount(&disk,8,2048)==0,"cut mount original image");
        cutCountdown=limit;
        int32_t f=files.Open(9,"/browser/session.bin",FileStore::Write|FileStore::Truncate);
        Require(f>0,"cut session open");
        for (uint32_t i=0;i<8197;i++) data[i]=(i*19+i/113+61)&255;
        Require(files.WriteFile(9,f,data,8197)==8197&&files.Sync(9,f)==0&&files.Close(9,f)==0,"cut full replacement");
        Require(!cutCountdown,"requested cut point reached");
        Print("CUT COMPLETE writes=");Hex(disk.writes);Print("\n");
    }
    void RecoverFixture(FileStore& files,ObservedDisk& disk) {
        Require(files.Mount(&disk,8,2048,true)==0,"recover interrupted metadata");
        int32_t f=files.Open(9,"/browser/session.bin",FileStore::Read);
        Require(f>0&&files.ReadFile(9,f,copy,9000)==8197,"recover complete file size");
        bool old=true,replacement=true;
        for (uint32_t i=0;i<8197;i++) {
            uint8_t expected=i<8193?(uint8_t)((i*37+i/251+17)&255):(uint8_t)"GTOS"[i-8193];
            if (copy[i]!=expected) old=false;
            if (copy[i]!=(uint8_t)((i*19+i/113+61)&255)) replacement=false;
        }
        Require(old||replacement,"interrupted replacement is entirely old or entirely new");
        Print("RECOVER CONTENT=");Print(old?"old":"new");Print("\n");
        Require(files.Close(9,f)==0&&files.Unmount()==0,"recovery close");
        GuardSectors(disk);
    }
    void FailureFixture(FileStore& files,ObservedDisk& disk) {
        uint32_t before=disk.writes;disk.failRead=true;
        Require(files.Mount(&disk,8,2048)==LFS_ERR_IO,"real block read failure reported");
        disk.failRead=false;Require(disk.writes==before,"mount read failure no writes");
        Require(files.Mount(&disk,8,2048)==0,"mount after IO error");
        int32_t f=files.Open(77,"/uncertain.bin",FileStore::Write|FileStore::Create);
        Require(f>0&&files.WriteFile(77,f,"UNCONFIRMED",11)==11,"pending close fixture");
        disk.failFlush=true;Require(files.Close(77,f)==LFS_ERR_IO,"close flush failure reported");
        Require(files.Close(77,f)==LFS_ERR_BADF&&files.OpenCount()==0,"failed close still releases descriptor");
        disk.failFlush=false;
        f=files.Open(77,"/uncertain2.bin",FileStore::Write|FileStore::Create);
        Require(f>0&&files.WriteFile(77,f,"PENDING",7)==7,"pending reclaim fixture");
        int32_t peer=files.Open(88,"/browser/session.bin",FileStore::Read);
        disk.failFlush=true;FileStore::ReclaimResult r=files.Reclaim(77);
        Require(r.released==1&&r.error==LFS_ERR_IO&&files.OpenCount()==1,"reclaim reports failed flush and releases cache");
        disk.failFlush=false;Require(files.Close(88,peer)==0,"peer survives failed owner reclaim");
        f=files.Open(77,"/write-error.bin",FileStore::Write|FileStore::Create);
        Require(f>0,"write error open");disk.failWrite=true;
        Require(files.WriteFile(77,f,data,8193)==LFS_ERR_IO,"real block write failure reported");
        disk.failWrite=false;Require(files.Close(77,f)==0&&!files.OpenCount(),"errored upstream file close unlinks");
        Require(files.Unmount()==0&&files.Mount(&disk,8,2048)==0,"recover mount after failures");
        Verify(files);Require(files.Unmount()==0,"failure fixture final unmount");
        GuardSectors(disk);
    }
}
extern "C" void NativeProcessSmoke(void* info,uint32_t magic) {
    Print("FILESTORE GUEST BOOT\n");Require(magic==0x2badb002,"multiboot");
    for (uint32_t i=0;i<512;i++) (&filesystem_stack_bottom)[i]=0xC3;
    static FileStore files;ObservedDisk disk;
    Require(disk.Identify()&&disk.SectorCount()==2064,"actual ATA dedicated disk");
    const uint32_t* boot=(const uint32_t*)info;
    bool writer=(boot[0]&4)&&Contains((const char*)boot[4],"writer");
    const char* command=(boot[0]&4)?(const char*)boot[4]:"";
    if (Contains(command,"recover")) RecoverFixture(files,disk);
    else if (Contains(command,"cut=")) {
        const char* p=command;
        while (*p&&!(p[0]=='c'&&p[1]=='u'&&p[2]=='t'&&p[3]=='=')) p++;
        uint32_t count=0;if (*p) { p+=4;while (*p>='0'&&*p<='9') count=count*10+(*p++-'0'); }
        CutFixture(files,disk,count);
    } else if (writer) WriteFixture(files,disk);
    else {
        ReadOnlyFixture(files,disk);
        FailureFixture(files,disk);
    }
    for (uint32_t i=0;i<512;i++) Require((&filesystem_stack_bottom)[i]==0xC3,"16 KiB stack bottom canary");
    Print("SUMMARY phase=");Print(Contains(command,"recover")?"recover":Contains(command,"cut=")?"complete":writer?"writer":"reader");Print(" checks=");Hex(checks);
    Print(" reads=");Hex(disk.reads);Print(" writes=");Hex(disk.writes);Print(" flushes=");Hex(disk.flushes);
    Print(" stack_observed=");Hex((uint32_t)&filesystem_stack_top-disk.lowStack);Print(" open=");Hex(files.OpenCount());Print("\n");
    Finish(true);
}

void operator delete(void*) {}
extern "C" void __cxa_pure_virtual() { gtos_lfs_assert_fail("fixture pure virtual",0); }
