#include "lfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE* disk;
static void need(int truth,const char* why) { if (!truth) { fprintf(stderr,"FAILED %s\n",why);exit(1); } }
static int bd_read(const struct lfs_config* c,lfs_block_t b,lfs_off_t o,void* p,lfs_size_t n) {
    (void)c;if (b>=256||o>4096||n>4096-o) return LFS_ERR_INVAL;
    if (fseek(disk,4096+(long)b*4096+o,SEEK_SET)) return LFS_ERR_IO;
    return fread(p,1,n,disk)==n?0:LFS_ERR_IO;
}
static int denied_prog(const struct lfs_config* c,lfs_block_t b,lfs_off_t o,const void* p,lfs_size_t n) { (void)c;(void)b;(void)o;(void)p;(void)n;need(0,"stock verifier never writes");return LFS_ERR_IO; }
static int denied_erase(const struct lfs_config* c,lfs_block_t b) { (void)c;(void)b;need(0,"stock verifier never erases");return LFS_ERR_IO; }
static int bd_sync(const struct lfs_config* c) { (void)c;return 0; }
int main(int argc,char** argv) {
    need(argc==2,"arguments");disk=fopen(argv[1],"rb");need(disk!=NULL,"independent disk open");
    struct lfs_config cfg={0};cfg.read=bd_read;cfg.prog=denied_prog;cfg.erase=denied_erase;cfg.sync=bd_sync;
    cfg.read_size=512;cfg.prog_size=512;cfg.block_size=4096;cfg.block_count=256;cfg.block_cycles=500;cfg.cache_size=512;cfg.lookahead_size=128;
    lfs_t fs;need(!lfs_mount(&fs,&cfg),"unmodified stock mount actual GTOS ATA");
    unsigned char data[9000];lfs_file_t f;
    need(!lfs_file_open(&fs,&f,"/native/session.bin",LFS_O_RDONLY),"native session");
    need(lfs_file_read(&fs,&f,data,sizeof(data))==8197,"native session length");
    for (unsigned i=0;i<8197;i++) need(data[i]==(i<8193?(unsigned char)(i*37+i/251+17):(unsigned char)"GTOS"[i-8193]),"native session exact bytes");
    need(!lfs_file_close(&fs,&f),"session close");
    need(!lfs_file_open(&fs,&f,"/native/hole.bin",LFS_O_RDONLY),"hole");
    need(lfs_file_read(&fs,&f,data,sizeof(data))==2048&&!memcmp(data,"HEAD",4),"hole length/header");
    for (unsigned i=4;i<2048;i++) need(data[i]==0,"hole zero fill");
    need(!lfs_file_close(&fs,&f),"hole close");
    need(!lfs_file_open(&fs,&f,"/native/中文.txt",LFS_O_RDONLY),"UTF8 path");
    need(lfs_file_read(&fs,&f,data,sizeof(data))==12&&!memcmp(data,"原生文件",12),"UTF8 bytes");need(!lfs_file_close(&fs,&f),"UTF8 close");
    const char* names[3]={"/native/reap0.bin","/native/reap1.bin","/native/reap2.bin"};
    for (unsigned i=0;i<3;i++) { need(!lfs_file_open(&fs,&f,names[i],LFS_O_RDONLY),"reclaimed file");need(lfs_file_read(&fs,&f,data,sizeof(data))==6&&!memcmp(data,"REAPED",6),"normal/fault/cancel persisted");need(!lfs_file_close(&fs,&f),"reclaimed close"); }
    need(!lfs_file_open(&fs,&f,"/peer.bin",LFS_O_RDONLY),"survivor");
    need(lfs_file_read(&fs,&f,data,sizeof(data))==4&&!memcmp(data,"PEER",4),"survivor bytes");need(!lfs_file_close(&fs,&f),"survivor close");
    const unsigned char png[]={
#include "resources_png.inc"
    };
    need(sizeof(png)==1108&&!lfs_file_open(&fs,&f,"/fixtures/image.png",LFS_O_RDONLY),"disk PNG");
    need(lfs_file_read(&fs,&f,data,sizeof(data))==1108&&!memcmp(data,png,1108),"PNG exact bytes");
    need(!lfs_file_close(&fs,&f)&&!lfs_unmount(&fs)&&!fclose(disk),"all independent read resources closed");
    puts("STOCK NATIVE FILE INTEROPERABILITY PASS");return 0;
}
