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
static int denied_prog(const struct lfs_config* c,lfs_block_t b,lfs_off_t o,const void* p,lfs_size_t n) { (void)c;(void)b;(void)o;(void)p;(void)n;need(0,"stock reader never writes");return LFS_ERR_IO; }
static int denied_erase(const struct lfs_config* c,lfs_block_t b) { (void)c;(void)b;need(0,"stock reader never erases");return LFS_ERR_IO; }
static int bd_sync(const struct lfs_config* c) { (void)c;return 0; }
int main(int argc,char** argv) {
    need(argc==2,"arguments");disk=fopen(argv[1],"rb");need(disk!=NULL,"independent disk open");
    struct lfs_config cfg={0};cfg.read=bd_read;cfg.prog=denied_prog;cfg.erase=denied_erase;cfg.sync=bd_sync;
    cfg.read_size=512;cfg.prog_size=512;cfg.block_size=4096;cfg.block_count=256;cfg.block_cycles=500;cfg.cache_size=512;cfg.lookahead_size=128;
    lfs_t fs;need(!lfs_mount(&fs,&cfg),"unmodified upstream cold mount");
    unsigned char data[10000];lfs_file_t f;
    need(!lfs_file_open(&fs,&f,"/native/stdio.bin",LFS_O_RDONLY),"real stdio file");
    need(lfs_file_read(&fs,&f,data,sizeof(data))==9004,"true stdio transfer size");
    for (unsigned i=0;i<9004;i++) need(data[i]==(i<9001?(unsigned char)(i*37+i/251+17):(unsigned char)"END"[i-9001]),"true stdio persisted bytes");
    need(!lfs_file_close(&fs,&f),"stdio close");
    need(!lfs_file_open(&fs,&f,"/native/\xe4\xb8\xad\xe6\x96\x87.txt",LFS_O_RDONLY),"UTF8 name");
    need(lfs_file_read(&fs,&f,data,sizeof(data))==10&&!memcmp(data,"line one\nX",10),"UTF8 file content");need(!lfs_file_close(&fs,&f),"UTF8 close");
    need(!lfs_file_open(&fs,&f,"/native/oom.txt",LFS_O_RDONLY),"OOM file");
    need(lfs_file_read(&fs,&f,data,sizeof(data))==2&&!memcmp(data,"ok",2),"original buffer survives OOM");need(!lfs_file_close(&fs,&f),"OOM close");
    struct lfs_info info;need(lfs_stat(&fs,"/native/oom-must-not-exist",&info)==LFS_ERR_NOENT,"OOM never creates file");
    need(!lfs_file_open(&fs,&f,"/native/console.bin",LFS_O_RDONLY),"redirected standard stream file");
    need(lfs_file_read(&fs,&f,data,sizeof(data))==11&&!memcmp(data,"redirected\n",11),"standard stream persisted bytes");
    need(!lfs_file_close(&fs,&f),"standard stream stock close");
    need(!lfs_file_open(&fs,&f,"/native/formatted.bin",LFS_O_RDONLY),"real formatted file");
    lfs_ssize_t formatted_bytes=lfs_file_read(&fs,&f,data,sizeof(data));
    const char formatted_prefix[]="file:7\nfloat:1.250|hex:0x1.8p+0\n";
    const unsigned prefix_bytes=sizeof(formatted_prefix)-1;
    need(formatted_bytes>=(lfs_ssize_t)(prefix_bytes+3000+10),"complete formatted bytes");
    need(!memcmp(data,formatted_prefix,prefix_bytes),"actual V8 FILE float prefix");
    for (unsigned i=0;i<3000;i++) need(data[prefix_bytes+i]==(unsigned char)('A'+i%26),"untruncated formatted payload");
    need(!memcmp(data+formatted_bytes-10,"recovered\n",10),"successful recovery suffix");
    need(!lfs_file_close(&fs,&f),"formatted stock close");
    need(!lfs_file_open(&fs,&f,"/native/v8-output.txt",LFS_O_RDONLY),"redirected V8 error file");
    lfs_ssize_t error_bytes=lfs_file_read(&fs,&f,data,sizeof(data));
    const char error_prefix[]="buffered:3|flushed:4\n";
    const char error_suffix[]="after-failure\nOOM PRINT ERROR 42\n";
    need(error_bytes>=(lfs_ssize_t)(sizeof(error_prefix)+sizeof(error_suffix)-2),"flushed error output size");
    need(!memcmp(data,error_prefix,sizeof(error_prefix)-1),"PrintError commits original buffered prefix");
    need(!memcmp(data+error_bytes-(sizeof(error_suffix)-1),error_suffix,sizeof(error_suffix)-1),"error recovery and OOM persisted bytes");
    need(!lfs_file_close(&fs,&f),"error stock close");
    const unsigned char png[]={
#include "resources_png.inc"
    };
    need(sizeof(png)==1108&&!lfs_file_open(&fs,&f,"/fixtures/image.png",LFS_O_RDONLY),"disk PNG");
    need(lfs_file_read(&fs,&f,data,sizeof(data))==1108&&!memcmp(data,png,1108),"disk PNG exact bytes");
    need(!lfs_file_close(&fs,&f)&&!lfs_unmount(&fs)&&!fclose(disk),"all independent read resources closed");
    puts("STOCK C FILE INTEROPERABILITY PASS");return 0;
}
