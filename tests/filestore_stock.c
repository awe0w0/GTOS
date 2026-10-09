#include "lfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE* disk;
static int bd_read(const struct lfs_config* c,lfs_block_t b,lfs_off_t o,void* p,lfs_size_t n) {
    (void)c;if (b>=256||o>4096||n>4096-o) return LFS_ERR_INVAL;
    if (fseek(disk,4096+(long)b*4096+o,SEEK_SET)) return LFS_ERR_IO;
    return fread(p,1,n,disk)==n?0:LFS_ERR_IO;
}
static int bd_prog(const struct lfs_config* c,lfs_block_t b,lfs_off_t o,const void* p,lfs_size_t n) {
    (void)c;if (b>=256||o>4096||n>4096-o) return LFS_ERR_INVAL;
    if (fseek(disk,4096+(long)b*4096+o,SEEK_SET)) return LFS_ERR_IO;
    return fwrite(p,1,n,disk)==n?0:LFS_ERR_IO;
}
static int bd_erase(const struct lfs_config* c,lfs_block_t b) {
    unsigned char p[4096];memset(p,255,sizeof(p));return bd_prog(c,b,0,p,sizeof(p));
}
static int bd_sync(const struct lfs_config* c) { (void)c;return fflush(disk)?LFS_ERR_IO:0; }
static void need(int truth,const char* why) { if (!truth) { fprintf(stderr,"FAILED %s\n",why);exit(1); } }
int main(int argc,char** argv) {
    need(argc==3,"arguments");disk=fopen(argv[1],"r+b");need(disk!=NULL,"disk open");
    struct lfs_config cfg={0};cfg.read=bd_read;cfg.prog=bd_prog;cfg.erase=bd_erase;cfg.sync=bd_sync;
    cfg.read_size=512;cfg.prog_size=512;cfg.block_size=4096;cfg.block_count=256;
    cfg.block_cycles=500;cfg.cache_size=512;cfg.lookahead_size=128;
    lfs_t fs;need(lfs_mount(&fs,&cfg)==0,"stock mount GTOS-created image");
    unsigned char data[9000];lfs_file_t f;
    need(lfs_file_open(&fs,&f,"/browser/session.bin",LFS_O_RDONLY)==0,"stock session open");
    need(lfs_file_read(&fs,&f,data,sizeof(data))==8197,"stock size");
    int old=1,replacement=1;
    for (unsigned i=0;i<8197;i++) {
        unsigned char expected=i<8193?(unsigned char)(i*37+i/251+17):(unsigned char)"GTOS"[i-8193];
        if (data[i]!=expected) old=0;
        if (data[i]!=(unsigned char)(i*19+i/113+61)) replacement=0;
    }
    need(!strcmp(argv[2],"recover")?(old||replacement):old,"stock expected complete content");
    printf("STOCK RECOVER CONTENT=%s\n",old?"old":"new");
    need(!lfs_file_close(&fs,&f),"stock append");
    need(!lfs_file_open(&fs,&f,"/browser/hole.bin",LFS_O_RDONLY),"stock hole open");
    need(lfs_file_read(&fs,&f,data,sizeof(data))==2048&&!memcmp(data,"HEAD",4),"stock hole header");
    for (unsigned i=4;i<2048;i++) need(data[i]==0,"stock zero filling");
    need(!lfs_file_close(&fs,&f),"stock hole close");
    need(!lfs_file_open(&fs,&f,"/browser/中文.txt",LFS_O_RDONLY),"stock UTF8 open");
    need(lfs_file_read(&fs,&f,data,sizeof(data))==12&&!memcmp(data,"原生文件",12),"stock UTF8 data");
    need(!lfs_file_close(&fs,&f),"stock UTF8 close");
    need(!lfs_file_open(&fs,&f,"/browser/history/owner.bin",LFS_O_RDONLY),"stock reclaim open");
    need(lfs_file_read(&fs,&f,data,sizeof(data))==6&&!memcmp(data,"REAPED",6),"stock reclaim content");
    need(!lfs_file_close(&fs,&f),"stock reclaim close");
    if (!strcmp(argv[2],"write")) {
        need(!lfs_file_open(&fs,&f,"/host.txt",LFS_O_WRONLY|LFS_O_CREAT|LFS_O_EXCL),"stock create host file");
        need(lfs_file_write(&fs,&f,"HOST-LITTLEFS-2.11.3",19)==19&&!lfs_file_close(&fs,&f),"stock host write");
    } else {
        need(!lfs_file_open(&fs,&f,"/host.txt",LFS_O_RDONLY),"stock host reopen");
        need(lfs_file_read(&fs,&f,data,sizeof(data))==19&&!memcmp(data,"HOST-LITTLEFS-2.11.3",19),"stock host persistent");
        need(!lfs_file_close(&fs,&f),"stock host close");
    }
    need(!lfs_unmount(&fs)&&!fclose(disk),"stock final close");
    puts("STOCK LITTLEFS INTEROPERABILITY PASS");
    return 0;
}
