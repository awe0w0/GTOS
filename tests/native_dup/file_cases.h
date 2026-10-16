#ifndef GTOS_DESCRIPTOR_FILE_CASES_H
#define GTOS_DESCRIPTOR_FILE_CASES_H
#include <process/file_abi.h>
static void FileCheck(bool value,unsigned line) {
    Check(value,line); __atomic_fetch_add(&cpu_record.fileChecks,1U,__ATOMIC_RELAXED);
}
#define FILE_CHECK(value) FileCheck((value),__LINE__)
static unsigned FilePathBytes(const char* path) {
    unsigned bytes=1;while (path[bytes-1]) ++bytes;return bytes;
}
static int FilePath(unsigned op,const char* path) {
    GtosFilePathRequest request={1,(unsigned)path,FilePathBytes(path)};
    return Call(op,(unsigned)&request,sizeof(request));
}
static int FileControl(unsigned op,unsigned handle) {
    GtosFileControlRequest request={1,handle};return Call(op,(unsigned)&request,sizeof(request));
}
static unsigned FileOpen(const char* path,unsigned flags) {
    GtosFileOpenRequest request={1,(unsigned)path,FilePathBytes(path),flags};
    int result=Call(GTOS_SYS_FILE_OPEN,(unsigned)&request,sizeof(request));FILE_CHECK(result>0);return result;
}
static unsigned FileDup(unsigned handle) {
    int result=FileControl(GTOS_SYS_FILE_DUP,handle);FILE_CHECK(result>0&&(unsigned)result!=handle);return result;
}
static void FileClose(unsigned handle) {FILE_CHECK(FileControl(GTOS_SYS_FILE_CLOSE,handle)==0);}
static int FileSeek(unsigned handle,int offset,unsigned whence) {
    GtosFileSeekRequest request={1,handle,(unsigned)offset,offset<0?~0U:0,whence};
    return Call(GTOS_SYS_FILE_SEEK,(unsigned)&request,sizeof(request));
}
static void FileWrite(unsigned handle,const char* text,unsigned bytes) {
    GtosFileTransferRequest request={1,handle,(unsigned)text,bytes};FILE_CHECK(Call(GTOS_SYS_FILE_WRITE,(unsigned)&request,sizeof(request))==(int)bytes);
}
static void FileRead(unsigned handle,const char* text,unsigned bytes) {
    char data[8];FILE_CHECK(bytes<=sizeof(data));
    GtosFileTransferRequest request={1,handle,(unsigned)data,bytes};FILE_CHECK(Call(GTOS_SYS_FILE_READ,(unsigned)&request,sizeof(request))==(int)bytes);
    for (unsigned i=0;i<bytes;++i) FILE_CHECK(data[i]==text[i]);
}
static void FileRejects(unsigned handle) {
    GtosFileControlRequest request={1,handle};
    FILE_CHECK(Call(GTOS_SYS_FILE_DUP,(unsigned)&request,7)==-22);
    FILE_CHECK(Call(GTOS_SYS_FILE_DUP,(unsigned)&request,9)==-22);
    request.version=2;FILE_CHECK(Call(GTOS_SYS_FILE_DUP,(unsigned)&request,sizeof(request))==-38);request.version=1;
    const unsigned bad[]={0,0xFFFFFFFCU,0xBFFFCFFCU,CPU_RECORD_VA+4096-4};
    *(volatile unsigned*)(CPU_RECORD_VA+4096-4)=1;
    for (unsigned i=0;i<4;++i) FILE_CHECK(Call(GTOS_SYS_FILE_DUP,bad[i],sizeof(request))==-14);
    const unsigned invalid[]={0,~0U,cpu_record.filePeer};
    for (unsigned i=0;i<3;++i) {request.handle=invalid[i];FILE_CHECK(Call(GTOS_SYS_FILE_DUP,(unsigned)&request,sizeof(request))==-9);}
    unsigned closed=FileDup(handle);FileClose(closed);request.handle=closed;
    FILE_CHECK(Call(GTOS_SYS_FILE_DUP,(unsigned)&request,sizeof(request))==-9);
    // Read-only input is valid input storage; zero is still an invalid handle.
    static const GtosFileControlRequest readonly={1,0};
    FILE_CHECK(Call(GTOS_SYS_FILE_DUP,(unsigned)&readonly,sizeof(readonly))==-9);
    request.handle=handle;unsigned char unaligned[10];Fill(unaligned,sizeof(unaligned));
    for (unsigned i=0;i<sizeof(request);++i) unaligned[i+1]=((unsigned char*)&request)[i];
    int copy=Call(GTOS_SYS_FILE_DUP,(unsigned)(unaligned+1),sizeof(request));FILE_CHECK(copy>0);
    FILE_CHECK(unaligned[0]==0xA5&&unaligned[9]==0xA5&&request.version==1&&request.handle==handle);FileClose(copy);
    FILE_CHECK(FileSeek(handle,0,1)==0&&FileControl(GTOS_SYS_FILE_SIZE,handle)==8);
}
static void FileCapacity(unsigned handle) {
    unsigned copies[62];
    for (unsigned i=0;i<62;++i) copies[i]=FileDup(handle);
    FILE_CHECK(FileControl(GTOS_SYS_FILE_DUP,handle)==-24);
    for (unsigned i=0;i<62;++i) FileClose(copies[i]);
    FILE_CHECK(FileSeek(handle,0,1)==0);
}
static void FilePeer() {
    cpu_record.fileShared=FileOpen("/peer.bin",GTOS_FILE_READ_WRITE|GTOS_FILE_CREATE|GTOS_FILE_TRUNCATE);
    FileWrite(cpu_record.fileShared,"peer",4);FILE_CHECK(FileControl(GTOS_SYS_FILE_SYNC,cpu_record.fileShared)==0);
    FILE_CHECK(FileSeek(cpu_record.fileShared,0,0)==0);cpu_record.fileLeaks=1;
    __atomic_store_n(&cpu_record.filePhase,1U,__ATOMIC_RELEASE);
}
static void FilePeerPulse() {FILE_CHECK(FileControl(GTOS_SYS_FILE_SIZE,cpu_record.fileShared)==4);}
static void FileRoot() {
    FILE_CHECK(cpu_record.filePeer>0);
    cpu_record.fileShared=FileOpen("/dup.bin",GTOS_FILE_READ_WRITE|GTOS_FILE_CREATE|GTOS_FILE_TRUNCATE);
    FileWrite(cpu_record.fileShared,"ABCDEFGH",8);FILE_CHECK(FileSeek(cpu_record.fileShared,0,0)==0);
    FileRejects(cpu_record.fileShared);FileCapacity(cpu_record.fileShared);
    int made=FilePath(GTOS_SYS_FILE_MKDIR,"/dup-dir");FILE_CHECK(made==0||made==-17);
    int directory=FilePath(GTOS_SYS_FILE_DIR_OPEN,"/dup-dir");FILE_CHECK(directory>0);
    cpu_record.fileDirectory=FileDup(directory);FileClose(directory);
    GtosFileInfo info={};GtosFileInfoRequest read={1,cpu_record.fileDirectory,(unsigned)&info,sizeof(info)};
    FILE_CHECK(Call(GTOS_SYS_FILE_DIR_READ,(unsigned)&read,sizeof(read))==1&&info.version==1&&info.type==GTOS_FILE_TYPE_DIRECTORY&&info.name[0]=='.'&&!info.name[1]);
    FILE_CHECK(FileControl(GTOS_SYS_FILE_DIR_REWIND,cpu_record.fileDirectory)==0);
    unsigned temporary=FileOpen("/temp-alias",GTOS_FILE_READ_WRITE|GTOS_FILE_CREATE|GTOS_FILE_EXCLUSIVE|GTOS_FILE_TEMPORARY);
    FileWrite(temporary,"TEMP",4);cpu_record.fileTemporary[0]=FileDup(temporary);FileClose(temporary);
    cpu_record.fileTemporary[1]=FileDup(cpu_record.fileTemporary[0]);FILE_CHECK(FileSeek(cpu_record.fileTemporary[0],0,0)==0);FileRead(cpu_record.fileTemporary[1],"TEMP",4);
    Fill(&info,sizeof(info));GtosFileStatRequest stat={1,(unsigned)"/temp-alias",FilePathBytes("/temp-alias"),(unsigned)&info,sizeof(info)};
    FILE_CHECK(Call(GTOS_SYS_FILE_STAT,(unsigned)&stat,sizeof(stat))==-2&&Sentinel(&info,sizeof(info)));
    cpu_record.fileAlias=FileDup(cpu_record.fileShared);cpu_record.fileLeaks=5;
}
static void FileWorker(unsigned role) {
    unsigned waits=0;
    while (__atomic_load_n(&cpu_record.filePhase,__ATOMIC_ACQUIRE)!=role) {FILE_CHECK(++waits<10000);Yield();}
    unsigned copy=FileDup(cpu_record.fileShared);
    if (role==0) FileRead(copy,"A",1);
    else if (role==1) FileRead(copy,"BC",2);
    else FileRead(copy,"DEF",3);
    FileClose(copy);__atomic_store_n(&cpu_record.filePhase,role+1,__ATOMIC_RELEASE);
}
static void FileFinish() {
    FILE_CHECK(__atomic_load_n(&cpu_record.filePhase,__ATOMIC_ACQUIRE)==3&&FileSeek(cpu_record.fileAlias,0,1)==6);
    FileRead(cpu_record.fileAlias,"G",1);FileClose(cpu_record.fileShared);cpu_record.fileShared=0;cpu_record.fileLeaks=4;
}
#endif
