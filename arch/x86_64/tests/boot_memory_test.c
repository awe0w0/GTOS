#include "boot_memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned char bytes[BOOTINFO_LIMIT+8];
static unsigned offset;
static struct boot_memory_selection selected,before;
static struct boot_memory_request request;
struct entry { uint64_t start,length; uint32_t type; };
static void w16(unsigned off,uint16_t value) {
    bytes[off]=(unsigned char)value;bytes[off+1]=(unsigned char)(value>>8);
}
static void w32(unsigned off,uint32_t value) {
    for (unsigned i=0;i<4;++i) bytes[off+i]=(unsigned char)(value>>(8*i));
}
static void w64(unsigned off,uint64_t value) { w32(off,(uint32_t)value);w32(off+4,(uint32_t)(value>>32)); }
static void begin(void) {
    memset(bytes,0,sizeof(bytes));offset=8;
    request=(struct boot_memory_request){.kernel_start=0x100000,.kernel_end=0x160000,
        .original_info_start=0x8000};
}
static unsigned tag(uint32_t type,uint32_t size) {
    unsigned at=offset;
    assert(size>=8 && size<=BOOTINFO_LIMIT-offset);
    w32(at,type);w32(at+4,size);offset+=(size+7u)&~7u;
    return at;
}
static unsigned map(const struct entry *entries,unsigned count,unsigned stride) {
    unsigned at=tag(6,16+count*stride);
    w32(at+8,stride);
    for (unsigned i=0;i<count;++i) {
        unsigned e=at+16+i*stride;
        w64(e,entries[i].start);w64(e+8,entries[i].length);w32(e+16,entries[i].type);
    }
    return at;
}
static void normal_map(void) {
    const struct entry e={0x100000,0x3f00000,1};map(&e,1,24);
}
static unsigned module(uint32_t first,uint32_t end,const char *name) {
    unsigned length=(unsigned)strlen(name)+1,at=tag(3,16+length);
    w32(at+8,first);w32(at+12,end);memcpy(bytes+at+16,name,length);
    return at;
}
static unsigned framebuffer(uint64_t address,uint32_t pitch,uint32_t width,uint32_t height,unsigned bpp,unsigned type) {
    unsigned size=type==0 ? 40 : type==1 ? 38 : 32;
    unsigned at=tag(8,size);
    w64(at+8,address);w32(at+16,pitch);w32(at+20,width);w32(at+24,height);
    bytes[at+28]=(unsigned char)bpp;bytes[at+29]=(unsigned char)type;
    if (type==0) w16(at+32,2);
    if (type==1) {
        bytes[at+32]=16;bytes[at+33]=8;
        bytes[at+34]=8;bytes[at+35]=8;
        bytes[at+36]=0;bytes[at+37]=8;
    }
    return at;
}
static unsigned finish(void) {
    tag(0,8);w32(0,offset);request.original_info_size=offset;return offset;
}
static unsigned basic(void) { begin();normal_map();return finish(); }
static void okay(unsigned available) {
    const char *error=boot_memory_select(bytes,available,&request,&selected);
    if (error) fprintf(stderr,"unexpected rejection: %s\n",error);
    assert(!error);
    assert(selected.managed_count>0 && selected.managed_count<=BOOT_MEMORY_MAX_FRAMES);
    for (unsigned i=0;i<selected.managed_count;++i) {
        assert(selected.frames[i]>=BOOT_MEMORY_FLOOR);
        assert(selected.frames[i]<BOOT_MEMORY_CEILING);
        assert(!(selected.frames[i]&(BOOT_MEMORY_PAGE_SIZE-1)));
        if (i) assert(selected.frames[i-1]<selected.frames[i]);
    }
    for (unsigned i=selected.managed_count;i<BOOT_MEMORY_MAX_FRAMES;++i) assert(!selected.frames[i]);
}
static void reject(unsigned available) {
    memset(&selected,0xa5,sizeof(selected));before=selected;
    assert(boot_memory_select(bytes,available,&request,&selected));
    assert(!memcmp(&selected,&before,sizeof(selected)));
}
static void parse_reject(unsigned available) {
    struct handoff_view view,snapshot;
    memset(&view,0xa5,sizeof(view));snapshot=view;
    assert(handoff_parse(bytes,available,request.kernel_start,request.kernel_end,&view));
    assert(!memcmp(&view,&snapshot,sizeof(view)));
    struct handoff_summary summary={123,456};
    assert(handoff_validate(bytes,available,request.kernel_start,request.kernel_end,&summary));
    assert(summary.usable_bytes==123 && summary.mmap_entries==456);
    reject(available);
}
static void absent(uint64_t start,uint64_t end) {
    for (unsigned i=0;i<selected.managed_count;++i)
        assert(!(selected.frames[i]<end && start<selected.frames[i]+BOOT_MEMORY_PAGE_SIZE));
}
static void basic_and_limits(void) {
    unsigned n=basic();okay(n);
    assert(selected.firmware_usable_bytes==0x3f00000);
    assert(selected.eligible_count==16032 && selected.managed_count==2048 && selected.mmap_entries==1);
    assert(selected.frames[0]==0x160000 && selected.frames[2047]==0x95f000);
    for (unsigned available=0;available<n;++available) reject(available);
    request.min_frames=1;request.max_frames=7;okay(n);
    assert(selected.managed_count==7 && selected.eligible_count==16032);
    request.min_frames=8;reject(n);
    request.min_frames=0;request.max_frames=511;reject(n);
    request.max_frames=2049;reject(n);
    request.min_frames=2049;request.max_frames=0;reject(n);
    begin();const struct entry exact={0x100000,0x260000,1};map(&exact,1,24);n=finish();okay(n);
    assert(selected.eligible_count==512 && selected.managed_count==512);
    w64(32,0x25ffff);reject(n);request.min_frames=1;okay(n);
    assert(selected.eligible_count==511 && selected.managed_count==511);
    w64(32,0x60000);reject(n);
    n=basic();request.original_info_size++;reject(n);
    n=basic();request.original_info_start=UINT64_MAX-8;reject(n);
    n=basic();request.original_info_start=BOOT_MEMORY_CEILING-8;reject(n);
    n=basic();request.original_info_start=0;reject(n);
    n=basic();request.original_info_start=0x8001;reject(n);
    n=basic();request.original_info_start=0x15fff8;reject(n);
    n=basic();request.kernel_start=0;reject(n);
    n=basic();request.kernel_end=BOOT_MEMORY_CEILING+1ull;reject(n);
    n=basic();request.kernel_end=request.kernel_start;reject(n);
    n=basic();assert(boot_memory_select(bytes,n,0,&selected));assert(boot_memory_select(bytes,n,&request,0));
}
static void exclusions_and_rounding(void) {
    begin();normal_map();module(0x200fff,0x202001,"module");
    framebuffer(0x300fff,256,64,17,32,1);
    unsigned n=finish();
    struct boot_memory_range retained[]={{0x400fff,0x402001},{0x401000,0x403000},
        {0,0x80000},{0x5000000,UINT64_MAX}};
    request.retained=retained;request.retained_count=4;
    request.original_info_start=0x500ff8;
    okay(n);assert(selected.eligible_count==16032-3-3-3-2);
    absent(0x200fff,0x202001);absent(0x300fff,0x3020ff);
    absent(0x400fff,0x403000);absent(0x500ff8,0x500ff8+n);
    request.retained_count=BOOT_MEMORY_MAX_RETAINED+1;reject(n);
    request.retained_count=1;request.retained=0;reject(n);
    request.retained=retained;retained[0]=(struct boot_memory_range){9,9};reject(n);
    retained[0]=(struct boot_memory_range){UINT64_MAX,1};reject(n);

    begin();const struct entry partial[]={{0x200000,0x2801,1},{0x100000,0x60000,1},
        {0x202801,0x4000,5},{0x206801,0x37ff,1},{0x90000,0x10000,1},
        {0x3fff001,0x3fff,1},{0x5000000,0x100000,1}};
    map(partial,sizeof(partial)/sizeof(*partial),24);n=finish();request.min_frames=1;okay(n);
    assert(selected.eligible_count==5 && selected.managed_count==5);
    assert(selected.frames[0]==0x200000 && selected.frames[1]==0x201000);
    assert(selected.frames[2]==0x207000 && selected.frames[3]==0x208000 && selected.frames[4]==0x209000);
    assert(selected.mmap_entries==7);
    uint64_t firmware=0;for (unsigned i=0;i<sizeof(partial)/sizeof(*partial);++i)
        if (partial[i].type==1) firmware+=partial[i].length;
    assert(selected.firmware_usable_bytes==firmware);

    begin();const struct entry adjacent[]={{0x100000,0x60000,1},{0x200001,0xfff,1},
        {0x201000,0x1001,1},{0x202001,0xfff,1}};
    map(adjacent,4,24);n=finish();request.min_frames=1;okay(n);
    assert(selected.managed_count==1 && selected.frames[0]==0x201000);
}
static void malformed_maps(void) {
    unsigned n=basic();w32(0,15);parse_reject(n);
    n=basic();w32(0,n+1);parse_reject(n+1);
    n=basic();w32(12,UINT32_MAX);parse_reject(n);
    n=basic();w32(16,23);parse_reject(n);
    n=basic();w32(20,1);parse_reject(n);
    n=basic();w64(24,UINT64_MAX-2);w64(32,8);parse_reject(n);
    n=basic();w64(32,0);parse_reject(n);
    begin();normal_map();normal_map();n=finish();parse_reject(n);
    begin();const struct entry reserved_overlap[]={{0x100000,0x60000,1},
        {0x400000,0x3000,2},{0x402000,0x3000,3}};
    map(reserved_overlap,3,24);n=finish();parse_reject(n);
    begin();const struct entry usable_overlap[]={{0x100000,0x60000,1},
        {0x400000,0x3000,1},{0x402000,0x3000,1}};
    map(usable_overlap,3,24);n=finish();parse_reject(n);
    begin();const struct entry reverse_overlap[]={{0x400000,0x3000,2},
        {0x402000,0x3000,1},{0x100000,0x60000,1}};
    map(reverse_overlap,3,24);n=finish();parse_reject(n);
    begin();normal_map();unsigned t=tag(18,8);n=finish();parse_reject(n);
    w32(t+4,16);parse_reject(n);
    begin();normal_map();tag(777,8);n=finish();okay(n);
    begin();const struct entry one={0x100000,0x3f00000,1};map(&one,1,32);n=finish();okay(n);
    w32(4,1);w32(44,1);okay(n); /* Ignored specification-reserved fields. */
    w32(n-4,16);parse_reject(n);
    n=basic();w32(n-8,77);parse_reject(n);
}
static void modules(void) {
    begin();normal_map();unsigned t=module(0x200000,0x201000,"");unsigned n=finish();okay(n);
    assert(selected.eligible_count==16031);
    w32(t+8,0x201000);parse_reject(n);
    w32(t+8,0x202000);parse_reject(n);
    w32(t+8,0x200000);bytes[t+16]='x';parse_reject(n);
    bytes[t+16]=0;w32(t+4,16);parse_reject(n);
    begin();normal_map();module(0x15ffff,0x160001,"bad");n=finish();parse_reject(n);
    begin();normal_map();module(0x200000,0x202000,"a");module(0x201fff,0x203000,"b");n=finish();parse_reject(n);
    begin();normal_map();module(0x200000,0x202000,"a");module(0x202000,0x203000,"b");n=finish();okay(n);
    begin();normal_map();module(0x8000,0x9000,"info");n=finish();reject(n);
    begin();normal_map();module(0x3ffffff,0x4001000,"ceiling");n=finish();okay(n);
    assert(selected.eligible_count==16031);
}
static void framebuffers(void) {
    begin();normal_map();unsigned t=framebuffer(0xb8000,160,80,25,16,2);unsigned n=finish();okay(n);
    bytes[t+30]=255;bytes[t+31]=255;okay(n); /* Reserved bytes are ignored. */
    w32(t+16,0);parse_reject(n);w32(t+16,159);parse_reject(n);w32(t+16,160);
    w32(t+20,0);parse_reject(n);w32(t+20,80);w32(t+24,0);parse_reject(n);w32(t+24,25);
    bytes[t+28]=0;parse_reject(n);bytes[t+28]=8;parse_reject(n);bytes[t+28]=16;
    bytes[t+29]=3;parse_reject(n);bytes[t+29]=2;
    w32(t+4,31);parse_reject(n);w32(t+4,32);w64(t+8,UINT64_MAX-1);parse_reject(n);
    w64(t+8,0x100000);parse_reject(n);
    begin();normal_map();framebuffer(0x200000,128,32,32,32,1);framebuffer(0x300000,160,80,25,16,2);n=finish();parse_reject(n);
    begin();normal_map();framebuffer(0x200000,128,32,32,32,1);module(0x200001,0x202000,"bad");n=finish();parse_reject(n);
    begin();normal_map();module(0x200001,0x202000,"bad");framebuffer(0x200000,128,32,32,32,1);n=finish();parse_reject(n);
    begin();normal_map();t=framebuffer(0x200000,128,32,32,32,1);n=finish();okay(n);
    assert(selected.eligible_count==16031);
    bytes[t+33]=0;parse_reject(n);bytes[t+33]=8;
    bytes[t+32]=30;parse_reject(n);bytes[t+32]=16;
    bytes[t+34]=17;parse_reject(n);bytes[t+34]=8;
    w32(t+4,37);parse_reject(n);w32(t+4,39);parse_reject(n);
    begin();normal_map();t=framebuffer(0x200000,128,128,32,8,0);n=finish();okay(n);
    w16(t+32,0);parse_reject(n);w16(t+32,3);parse_reject(n);w16(t+32,2);
    bytes[t+28]=9;parse_reject(n);bytes[t+28]=1;okay(n);
    w16(t+32,3);parse_reject(n);
    begin();normal_map();framebuffer(0x8000,128,32,32,32,1);n=finish();reject(n);
    begin();normal_map();framebuffer(0x100000000ull,256,64,32,32,1);n=finish();okay(n);
    assert(selected.eligible_count==16032);
}
static void maximum_buffer(void) {
    begin();
    unsigned at=tag(6,65512);w32(at+8,24);
    for (unsigned i=0;i<2729;++i) {
        unsigned e=at+16+i*24;
        w64(e,i ? 0x400000ull+(uint64_t)i*0x2000 : 0x100000);
        w64(e+8,i ? 4096 : 0x100000);w32(e+16,1);
    }
    tag(777,8);unsigned n=finish();assert(n==BOOTINFO_LIMIT);okay(n);
    assert(selected.mmap_entries==2729 && selected.eligible_count==2888);
    assert(selected.managed_count==2048 && selected.frames[160]==0x402000);
    w64(at+16+2728*24,0x100000);parse_reject(n);
    begin();normal_map();
    for (unsigned i=0;i<2728;++i) module(0x80000000u+i*4096,0x80000000u+(i+1)*4096,"");
    tag(777,8);n=finish();assert(n==BOOTINFO_LIMIT);okay(n);
    assert(selected.eligible_count==16032 && selected.managed_count==2048);
    /* A malformed final tag must not expose otherwise-valid candidates. */
    w32(n-4,7);parse_reject(n);
}
/* Independent byte-range oracle: each selected page must be wholly in one
 * usable map entry and wholly outside every outward-effective exclusion. */
static void randomized_ranges(void) {
    uint32_t random=0x47544f53;
    for (unsigned trial=0;trial<500;++trial) {
        struct entry entries[18]={{0x100000,0x60000,1}};
        struct boot_memory_range retained[8];
        uint64_t current=0x180000;
        for (unsigned i=1;i<18;++i) {
            random=random*1664525u+1013904223u;current+=random%4096;
            entries[i].start=current;
            random=random*1664525u+1013904223u;entries[i].length=1+random%65536;
            random=random*1664525u+1013904223u;entries[i].type=(random%3) ? 1 : 2;
            current+=entries[i].length;
        }
        for (unsigned i=0;i<8;++i) {
            random=random*1664525u+1013904223u;retained[i].start=0x180000+random%(current-0x180000);
            random=random*1664525u+1013904223u;retained[i].end=retained[i].start+1+random%8192;
        }
        begin();map(entries,18,24);module(0x170fff,0x171001,"edge");
        unsigned n=finish();request.min_frames=1;request.max_frames=23;
        request.retained=retained;request.retained_count=8;
        uint32_t expected=0,chosen=0;uint64_t firmware=0;
        for (unsigned i=0;i<18;++i) if (entries[i].type==1) firmware+=entries[i].length;
        okay(n);
        for (uint64_t page=BOOT_MEMORY_FLOOR;page<BOOT_MEMORY_CEILING;page+=BOOT_MEMORY_PAGE_SIZE) {
            int free_page=0;
            for (unsigned i=1;i<18;++i)
                if (entries[i].type==1 && entries[i].start<=page && page+4096<=entries[i].start+entries[i].length)
                    free_page=1;
            for (unsigned i=0;i<8;++i)
                if (page<retained[i].end && retained[i].start<page+4096) free_page=0;
            if (free_page) {
                ++expected;
                if (chosen<23) assert(selected.frames[chosen++]==page);
            }
        }
        assert(selected.eligible_count==expected && selected.managed_count==chosen);
        assert(selected.firmware_usable_bytes==firmware);
    }
}
static void mutation_stress(void) {
    uint32_t random=0xb007b007;
    for (unsigned trial=0;trial<20000;++trial) {
        begin();normal_map();module(0x200fff,0x202001,"fixture");framebuffer(0xb8000,160,80,25,16,2);
        unsigned n=finish();random=random*1664525u+1013904223u;
        unsigned pos=random%n;random=random*1664525u+1013904223u;
        bytes[pos]^=(unsigned char)(random>>24);
        memset(&selected,0xa5,sizeof(selected));before=selected;
        if (boot_memory_select(bytes,n,&request,&selected)) assert(!memcmp(&selected,&before,sizeof(selected)));
        else {
            assert(selected.managed_count<=2048 && selected.eligible_count<=16128);
            for (unsigned i=0;i<selected.managed_count;++i) {
                assert(selected.frames[i]>=0x160000 && selected.frames[i]<BOOT_MEMORY_CEILING);
                if (i) assert(selected.frames[i]>selected.frames[i-1]);
            }
        }
    }
}
int main(void) {
    basic_and_limits();exclusions_and_rounding();malformed_maps();modules();framebuffers();
    maximum_buffer();randomized_ranges();mutation_stress();
    puts("x64 boot-memory host tests: PASS (selection, exclusions, 500 oracle fixtures, 20,000 mutations)");
}
