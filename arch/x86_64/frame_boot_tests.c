#include "frame_boot_tests.h"

extern unsigned char __kernel_start[], __kernel_end[];
#define TEST_BASE UINT64_C(0x00800000)
#define PAGE UINT64_C(4096)
#define LEAF_COUNT (BOOT_MEMORY_CEILING / BOOT_MEMORY_PAGE_SIZE)
/* Large storage is private kernel BSS, never the 16 KiB BSP stack. These bytes
 * describe parser fixtures; they are never a source of claimed physical RAM. */
static unsigned char bytes[1024];
static struct boot_memory_selection selected, actual;
static struct boot_memory_request request;
static struct frame_pool failed_pool;
static uint64_t baseline[LEAF_COUNT];
static unsigned offset;
static const struct frame_platform *production;
static uint32_t writes, flushes, aliases;
static uint64_t corrupt_leaf;
static int corrupt_write;

static inline void out(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port));
}
static void say(const char *s) { while (*s) out(0xe9, (unsigned char)*s++); }
__attribute__((noreturn)) static void die(const char *s) {
    say("X64 FRAME FAIL boot fixture: "); say(s); say("\n");
    __asm__ volatile("outl %0,%1"::"a"(0x11u),"Nd"((uint16_t)0xf4));
    for (;;) __asm__ volatile("cli; hlt");
}
static void need(int condition, const char *s) { if (!condition) die(s); }
static void fill(void *memory, size_t size, unsigned char value) {
    unsigned char *p=memory;
    for (size_t i=0;i<size;++i) p[i]=value;
}
static int filled(const void *memory, size_t size, unsigned char value) {
    const unsigned char *p=memory;
    for (size_t i=0;i<size;++i) if (p[i]!=value) return 0;
    return 1;
}
static int overlap(uint64_t a,uint64_t b,uint64_t c,uint64_t d) { return a<d && c<b; }
static void w16(unsigned at,uint16_t value) {
    bytes[at]=(unsigned char)value; bytes[at+1]=(unsigned char)(value>>8);
}
static void w32(unsigned at,uint32_t value) {
    for (unsigned i=0;i<4;++i) bytes[at+i]=(unsigned char)(value>>(8*i));
}
static void w64(unsigned at,uint64_t value) { w32(at,(uint32_t)value);w32(at+4,(uint32_t)(value>>32)); }
static void begin(void) {
    fill(bytes,sizeof(bytes),0); offset=8;
    request=(struct boot_memory_request){
        .kernel_start=(uint64_t)__kernel_start,.kernel_end=(uint64_t)__kernel_end,
        .original_info_start=0x8000
    };
}
static unsigned tag(uint32_t type,uint32_t size) {
    unsigned at=offset;
    need(size>=8 && size<=sizeof(bytes)-offset,"fixture capacity");
    w32(at,type);w32(at+4,size);offset+=(size+7u)&~7u;
    need(offset<=sizeof(bytes),"fixture padding capacity");
    return at;
}
struct map_entry { uint64_t start,length; uint32_t type; };
static unsigned memory_map(const struct map_entry *entries,unsigned count,unsigned stride) {
    unsigned at=tag(6,16+count*stride);w32(at+8,stride);
    for (unsigned i=0;i<count;++i) {
        unsigned e=at+16+i*stride;
        w64(e,entries[i].start);w64(e+8,entries[i].length);w32(e+16,entries[i].type);
    }
    return at;
}
static void normal_map(void) {
    const struct map_entry e={BOOT_MEMORY_FLOOR,BOOT_MEMORY_CEILING-BOOT_MEMORY_FLOOR,1};
    memory_map(&e,1,24);
}
static void limited_map(unsigned pages) {
    const struct map_entry entries[]={
        {request.kernel_start,request.kernel_end-request.kernel_start,1},
        {TEST_BASE,(uint64_t)pages*PAGE,1}
    };
    memory_map(entries,2,24);
}
static unsigned module(uint32_t start,uint32_t end) {
    unsigned at=tag(3,17);w32(at+8,start);w32(at+12,end);return at;
}
static unsigned framebuffer(uint64_t start,uint32_t pitch,uint32_t width,
                            uint32_t height,unsigned bpp,unsigned type) {
    unsigned at=tag(8,type==0 ? 40 : type==1 ? 38 : 32);
    w64(at+8,start);w32(at+16,pitch);w32(at+20,width);w32(at+24,height);
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

/* Instrumentation delegates to the same hardware backend as the real pool.
 * It can corrupt one real leaf during installation, but never supply RAM. */
static int context(void *unused) { (void)unused;return production->context_ok(production->opaque); }
static uint64_t read_leaf(void *unused,uint64_t address) {
    (void)unused;return production->read_leaf(production->opaque,address);
}
static void write_leaf(void *unused,uint64_t address,uint64_t value) {
    (void)unused;++writes;
    if (corrupt_write && value && address==corrupt_leaf) value^=FRAME_POOL_NX;
    production->write_leaf(production->opaque,address,value);
}
static void flush(void *unused) { (void)unused;++flushes;production->flush(production->opaque); }
static volatile unsigned char *alias(void *unused,uint64_t address) {
    (void)unused;++aliases;return production->alias(production->opaque,address);
}
static const struct frame_platform instrumented={context,read_leaf,write_leaf,flush,alias,0};
static void reset_counts(void) { writes=flushes=aliases=0; }
static uint64_t leaf_bits(uint64_t value) { return value&~UINT64_C(0x60); }
static void snapshot(void) {
    for (unsigned i=0;i<LEAF_COUNT;++i)
        baseline[i]=leaf_bits(production->read_leaf(production->opaque,(uint64_t)i*PAGE));
}
static void unchanged(void) {
    /* A/D are independently set by the CPU as this fixture executes. Every
     * other bit in EVERY low-PT leaf must remain exactly as captured. */
    for (unsigned i=0;i<LEAF_COUNT;++i)
        need(leaf_bits(production->read_leaf(production->opaque,(uint64_t)i*PAGE))==baseline[i],
             "failure changed real low-PT leaf");
}
static void no_publication(void) {
    need(filled(&failed_pool,sizeof(failed_pool),0),"failed initialization published pool state");
}
static void rejected_init(const void *input,size_t size,const struct boot_memory_request *r,
                          const char *name) {
    const char *reason=0;
    reset_counts();
    need(frame_pool_init(&failed_pool,input,size,r,&instrumented,&reason)==FRAME_BOOT_MEMORY,name);
    need(reason!=0,"missing initialization rejection reason");
    need(!writes && !flushes && !aliases,"rejected boot input touched hardware");
    no_publication();unchanged();
}
static void reject(unsigned available,const char *name) {
    fill(&selected,sizeof(selected),0xa5);
    need(boot_memory_select(bytes,available,&request,&selected)!=0,name);
    need(filled(&selected,sizeof(selected),0xa5),"failed selector exposed candidate");
    rejected_init(bytes,available,&request,name);
}
static void parse_reject(unsigned available,const char *name) {
    struct handoff_view view;
    struct handoff_summary summary;
    fill(&view,sizeof(view),0xa5);fill(&summary,sizeof(summary),0xa5);
    need(handoff_parse(bytes,available,request.kernel_start,request.kernel_end,&view)!=0,name);
    need(filled(&view,sizeof(view),0xa5),"failed parser exposed view");
    need(handoff_validate(bytes,available,request.kernel_start,request.kernel_end,&summary)!=0,name);
    need(filled(&summary,sizeof(summary),0xa5),"failed validator exposed summary");
    reject(available,name);
}
static void absent(uint64_t start,uint64_t end) {
    for (uint32_t i=0;i<selected.managed_count;++i)
        need(!overlap(selected.frames[i],selected.frames[i]+PAGE,start,end),"reserved partial page selected");
}
static void okay(unsigned available) {
    const char *reason=boot_memory_select(bytes,available,&request,&selected);
    if (reason) die(reason);
    need(selected.managed_count && selected.managed_count<=BOOT_MEMORY_MAX_FRAMES,"selected count");
    for (uint32_t i=0;i<selected.managed_count;++i) {
        uint64_t a=selected.frames[i];
        need(a>=BOOT_MEMORY_FLOOR && a<BOOT_MEMORY_CEILING && !(a&(PAGE-1)),"selection boundary");
        if (i) need(a>selected.frames[i-1],"selection order and uniqueness");
    }
    for (uint32_t i=selected.managed_count;i<BOOT_MEMORY_MAX_FRAMES;++i)
        need(!selected.frames[i],"unused selection slot");
    absent(request.kernel_start,request.kernel_end);
    absent(request.original_info_start,request.original_info_start+request.original_info_size);
    for (size_t i=0;i<request.retained_count;++i) absent(request.retained[i].start,request.retained[i].end);
}
static void selection_fixtures(void) {
    unsigned n=basic();okay(n);
    uint32_t eligible=(uint32_t)((BOOT_MEMORY_CEILING-(uint64_t)__kernel_end)/PAGE);
    need(selected.eligible_count==eligible && selected.managed_count==BOOT_MEMORY_MAX_FRAMES,
         "actual linker kernel exclusion");
    need(selected.frames[0]==(uint64_t)__kernel_end,"kernel ending boundary");
    need(selected.firmware_usable_bytes==BOOT_MEMORY_CEILING-BOOT_MEMORY_FLOOR,"firmware accounting");
    for (unsigned available=0;available<n;++available) reject(available,"truncated private copy");
    request.min_frames=1;request.max_frames=7;okay(n);
    need(selected.managed_count==7 && selected.eligible_count==eligible,"explicit seven-frame selection");
    request.min_frames=8;reject(n,"minimum exceeds cap");
    request.min_frames=0;request.max_frames=511;reject(n,"default minimum cannot be silently reduced");
    request.max_frames=BOOT_MEMORY_MAX_FRAMES+1;reject(n,"oversized cap");
    begin();limited_map(512);n=finish();okay(n);
    need(selected.eligible_count==512 && selected.managed_count==512,"exact default minimum accepted");
    begin();limited_map(511);n=finish();reject(n,"511 actual eligible fixture frames fail default 512");
    request.min_frames=1;okay(n);
    need(selected.eligible_count==511 && selected.managed_count==511,"explicit reduced minimum accepted");
    begin();limited_map(7);n=finish();reject(n,"seven eligible fixture frames fail default 512");
    request.min_frames=1;request.max_frames=7;okay(n);
    need(selected.eligible_count==7 && selected.managed_count==7,"seven eligible fixture frames selected explicitly");
    n=basic();request.original_info_size++;reject(n,"original size mismatch");
    n=basic();request.original_info_start=UINT64_MAX-8;reject(n,"original address overflow");
    n=basic();request.original_info_start=BOOT_MEMORY_CEILING-8;reject(n,"original crosses low ceiling");
    n=basic();request.original_info_start=0;reject(n,"original null page");
    n=basic();request.original_info_start=0x8001;reject(n,"original alignment");
    n=basic();request.original_info_start=request.kernel_end-8;reject(n,"original overlaps kernel");
    n=basic();request.kernel_start=0;reject(n,"kernel below floor");
    n=basic();request.kernel_end=BOOT_MEMORY_CEILING+1ull;reject(n,"kernel above ceiling");
    n=basic();request.kernel_end=request.kernel_start;reject(n,"empty kernel range");

    begin();limited_map(32);
    module((uint32_t)(TEST_BASE+0xfff),(uint32_t)(TEST_BASE+0x2001));
    framebuffer(TEST_BASE+0x4fff,256,64,17,32,1);n=finish();
    const struct boot_memory_range retained[]={
        {TEST_BASE+0x8fff,TEST_BASE+0xa001},{TEST_BASE+0x9000,TEST_BASE+0xb000},
        {0,BOOT_MEMORY_FLOOR},{BOOT_MEMORY_CEILING,UINT64_MAX}
    };
    request.retained=retained;request.retained_count=4;request.min_frames=1;
    request.original_info_start=TEST_BASE+0xcff8;okay(n);
    need(selected.eligible_count==21 && selected.managed_count==21,"combined outward exclusions");
    absent(TEST_BASE+0xfff,TEST_BASE+0x2001);absent(TEST_BASE+0x4fff,TEST_BASE+0x60ff);
    request.retained_count=BOOT_MEMORY_MAX_RETAINED+1;reject(n,"too many retained ranges");
    request.retained_count=1;request.retained=0;reject(n,"missing retained ranges");
    const struct boot_memory_range empty={9,9};request.retained=&empty;reject(n,"empty retained range");
    const struct boot_memory_range overflow={UINT64_MAX,1};request.retained=&overflow;reject(n,"reversed retained range");

    begin();const struct map_entry partial[]={
        {TEST_BASE,0x2801,1},{request.kernel_start,request.kernel_end-request.kernel_start,1},
        {TEST_BASE+0x2801,0x4000,5},{TEST_BASE+0x6801,0x37ff,1},{0x90000,0x10000,1},
        {BOOT_MEMORY_CEILING-0xfff,0x3fff,1},{BOOT_MEMORY_CEILING+0x1000000ull,0x100000,1}
    };
    memory_map(partial,7,24);n=finish();request.min_frames=1;okay(n);
    const uint64_t expected[]={TEST_BASE,TEST_BASE+0x1000,TEST_BASE+0x7000,TEST_BASE+0x8000,TEST_BASE+0x9000};
    need(selected.eligible_count==5 && selected.managed_count==5 && selected.mmap_entries==7,"unaligned hole count");
    for (unsigned i=0;i<5;++i) need(selected.frames[i]==expected[i],"unaligned usable/reserved rounding");
    uint64_t firmware=0;for (unsigned i=0;i<7;++i) if (partial[i].type==1) firmware+=partial[i].length;
    need(selected.firmware_usable_bytes==firmware,"firmware bytes differ from managed bytes");
    begin();const struct map_entry adjacent[]={
        {request.kernel_start,request.kernel_end-request.kernel_start,1},
        {TEST_BASE+1,0xfff,1},{TEST_BASE+0x1000,0x1001,1},{TEST_BASE+0x2001,0xfff,1}
    };
    memory_map(adjacent,4,24);n=finish();request.min_frames=1;okay(n);
    need(selected.managed_count==1 && selected.frames[0]==TEST_BASE+0x1000,"adjacent partial entries not merged");
    begin();const struct map_entry boundaries={0,BOOT_MEMORY_CEILING+PAGE,1};
    memory_map(&boundaries,1,24);n=finish();okay(n);
    need(selected.eligible_count==eligible,"floor and ceiling clipping");
    const struct boot_memory_range last={BOOT_MEMORY_CEILING-1,UINT64_MAX};
    request.retained=&last;request.retained_count=1;okay(n);
    need(selected.eligible_count==eligible-1,"last partial retained page");
}
static void malformed_fixtures(void) {
    unsigned n=basic();w32(0,15);parse_reject(n,"small total");
    n=basic();w32(0,n+1);parse_reject(n+1,"unaligned total");
    n=basic();w32(12,UINT32_MAX);parse_reject(n,"oversized map tag");
    n=basic();w32(12,39);parse_reject(n,"incomplete map entry");
    n=basic();w32(16,23);parse_reject(n,"short map stride");
    n=basic();w32(16,25);parse_reject(n,"unaligned map stride");
    n=basic();w32(20,1);parse_reject(n,"unsupported map version");
    n=basic();w64(24,UINT64_MAX-2);w64(32,8);parse_reject(n,"map address arithmetic overflow");
    n=basic();w64(32,0);parse_reject(n,"zero map length");
    begin();normal_map();normal_map();n=finish();parse_reject(n,"duplicate map tag");
    for (unsigned kind=0;kind<4;++kind) {
        begin();const struct map_entry overlapping[]={
            {request.kernel_start,request.kernel_end-request.kernel_start,1},
            {TEST_BASE,0x3000,kind&1 ? 1u : 2u},
            {TEST_BASE+0x2000,0x3000,kind&2 ? 1u : 3u}
        };
        memory_map(overlapping,3,24);n=finish();parse_reject(n,"ambiguous overlapping map entries");
    }
    begin();const struct map_entry absent_kernel={TEST_BASE,512*PAGE,1};
    memory_map(&absent_kernel,1,24);n=finish();parse_reject(n,"map misses actual kernel");
    begin();normal_map();tag(18,8);n=finish();parse_reject(n,"live EFI boot services");
    n=basic();w32(n-4,16);parse_reject(n,"invalid end tag size");
    n=basic();w32(n-8,777);parse_reject(n,"missing end tag");
    begin();normal_map();tag(0,8);n=finish();parse_reject(n,"early end tag");
    begin();const struct map_entry extended={BOOT_MEMORY_FLOOR,BOOT_MEMORY_CEILING-BOOT_MEMORY_FLOOR,1};
    memory_map(&extended,1,32);tag(777,8);n=finish();
    w32(4,UINT32_MAX);w32(44,UINT32_MAX);okay(n); /* Specification-reserved fields are ignored. */
}
static void allocation_tag_fixtures(void) {
    begin();normal_map();unsigned t=module((uint32_t)TEST_BASE,(uint32_t)(TEST_BASE+PAGE));unsigned n=finish();okay(n);
    w32(t+8,(uint32_t)(TEST_BASE+PAGE));parse_reject(n,"empty module");
    w32(t+8,(uint32_t)(TEST_BASE+2*PAGE));parse_reject(n,"reversed module");
    w32(t+8,(uint32_t)TEST_BASE);bytes[t+16]='x';parse_reject(n,"unterminated module string");
    bytes[t+16]=0;w32(t+4,16);parse_reject(n,"short module tag");
    begin();normal_map();module((uint32_t)request.kernel_end-1,(uint32_t)request.kernel_end+1);
    n=finish();parse_reject(n,"module overlaps kernel partial page");
    begin();normal_map();module((uint32_t)TEST_BASE,(uint32_t)(TEST_BASE+2*PAGE));
    module((uint32_t)(TEST_BASE+2*PAGE-1),(uint32_t)(TEST_BASE+3*PAGE));
    n=finish();parse_reject(n,"module overlap");
    begin();normal_map();module((uint32_t)TEST_BASE,(uint32_t)(TEST_BASE+2*PAGE));
    module((uint32_t)(TEST_BASE+2*PAGE),(uint32_t)(TEST_BASE+3*PAGE));n=finish();okay(n);
    begin();normal_map();module(0x8000,0x9000);n=finish();reject(n,"module overlaps original information");
    begin();normal_map();module(BOOT_MEMORY_CEILING-1,BOOT_MEMORY_CEILING+4096);n=finish();okay(n);
    uint32_t eligible=(uint32_t)((BOOT_MEMORY_CEILING-(uint64_t)__kernel_end)/PAGE);
    need(selected.eligible_count==eligible-1,"module ceiling boundary");

    begin();normal_map();t=framebuffer(0xb8000,160,80,25,16,2);n=finish();okay(n);
    bytes[t+30]=255;bytes[t+31]=255;okay(n); /* Exact 32-byte prefix, reserved uint16_t. */
    w32(t+16,0);parse_reject(n,"zero framebuffer pitch");
    w32(t+16,159);parse_reject(n,"short framebuffer pitch");w32(t+16,160);
    w32(t+20,0);parse_reject(n,"zero framebuffer width");w32(t+20,80);
    w32(t+24,0);parse_reject(n,"zero framebuffer height");w32(t+24,25);
    bytes[t+28]=0;parse_reject(n,"zero framebuffer bpp");
    bytes[t+28]=8;parse_reject(n,"text framebuffer bpp");bytes[t+28]=16;
    bytes[t+29]=3;parse_reject(n,"unknown framebuffer format");bytes[t+29]=2;
    w32(t+4,31);parse_reject(n,"short exact framebuffer prefix");w32(t+4,32);
    w64(t+8,UINT64_MAX-1);parse_reject(n,"framebuffer address overflow");
    w64(t+8,request.kernel_start);parse_reject(n,"framebuffer overlaps actual kernel");
    begin();normal_map();framebuffer(0x8000,128,32,32,32,1);n=finish();reject(n,"framebuffer overlaps original information");
    begin();normal_map();framebuffer(TEST_BASE,128,32,32,32,1);framebuffer(TEST_BASE+2*PAGE,160,80,25,16,2);
    n=finish();parse_reject(n,"duplicate framebuffer");
    for (unsigned reverse=0;reverse<2;++reverse) {
        begin();normal_map();
        if (reverse) module((uint32_t)TEST_BASE+1,(uint32_t)(TEST_BASE+2*PAGE));
        framebuffer(TEST_BASE,128,32,32,32,1);
        if (!reverse) module((uint32_t)TEST_BASE+1,(uint32_t)(TEST_BASE+2*PAGE));
        n=finish();parse_reject(n,"module framebuffer overlap in either order");
    }
    begin();normal_map();t=framebuffer(TEST_BASE,128,32,32,32,1);n=finish();okay(n);
    need(selected.eligible_count==eligible-1,"exact framebuffer exclusion");
    bytes[t+33]=0;parse_reject(n,"zero RGB mask");bytes[t+33]=8;
    bytes[t+32]=30;parse_reject(n,"RGB mask beyond bpp");bytes[t+32]=16;
    bytes[t+34]=17;parse_reject(n,"overlapping RGB masks");bytes[t+34]=8;
    w32(t+4,37);parse_reject(n,"short RGB tail");w32(t+4,39);parse_reject(n,"long RGB tail");
    begin();normal_map();t=framebuffer(TEST_BASE,128,128,32,8,0);n=finish();okay(n);
    bytes[t+30]=255;bytes[t+31]=255;okay(n);
    w16(t+32,0);parse_reject(n,"zero palette count");
    w16(t+32,3);parse_reject(n,"palette exact length");
    w16(t+32,258);parse_reject(n,"palette count is full uint16");w16(t+32,2);
    bytes[t+28]=9;parse_reject(n,"palette bpp bound");bytes[t+28]=1;okay(n);
    begin();normal_map();framebuffer(UINT64_C(0x100000000),256,64,32,32,1);n=finish();okay(n);
    need(selected.eligible_count==eligible,"high framebuffer address is not truncated");
    begin();normal_map();t=framebuffer(UINT64_MAX-8192,256,64,32,32,1);n=finish();okay(n);
    w64(t+8,UINT64_MAX-8191);parse_reject(n,"framebuffer sum one-byte overflow");
    begin();normal_map();framebuffer(TEST_BASE,UINT32_MAX,UINT32_MAX,1,64,1);
    n=finish();parse_reject(n,"wide framebuffer row arithmetic");
}
static void actual_initialization_fixtures(const void *private_copy,size_t size,uint64_t original) {
    struct boot_memory_request real={
        .kernel_start=(uint64_t)__kernel_start,.kernel_end=(uint64_t)__kernel_end,
        .original_info_start=original,.original_info_size=size,.min_frames=1,.max_frames=7
    };
    need(!boot_memory_select(private_copy,size,&real,&actual),"select actual firmware frames for init fixtures");
    need(actual.managed_count==7,"real firmware provides fixture subset");
    /* Keep precisely the first seven real eligible frames. This fails the
     * default 512 minimum without inventing a memory map or using fake RAM. */
    const struct boot_memory_range tail={actual.frames[6]+PAGE,BOOT_MEMORY_CEILING};
    real.retained=&tail;real.retained_count=1;real.min_frames=0;real.max_frames=0;
    fill(&selected,sizeof(selected),0xa5);
    need(boot_memory_select(private_copy,size,&real,&selected)!=0,"real default-minimum rejection");
    need(filled(&selected,sizeof(selected),0xa5),"minimum failure published candidates");
    rejected_init(private_copy,size,&real,"real seven-frame map must fail default minimum");
    real.min_frames=1;real.max_frames=7;
    need(!boot_memory_select(private_copy,size,&real,&actual),"explicit reduced real selection");
    need(actual.eligible_count==7 && actual.managed_count==7,"reduced pool is explicit and exact");
    for (unsigned i=0;i<7;++i)
        need(!production->read_leaf(production->opaque,actual.frames[i]),"real candidate has a bootstrap alias");

    uint64_t late=actual.frames[6];
    for (unsigned present=0;present<2;++present) {
        /* Place both a non-present software PTE and a present mapping at the
         * LAST selected frame: preflight must preserve even earlier zero leaves. */
        uint64_t collision=present ? late|FRAME_POOL_NX|3 : UINT64_C(0x200);
        production->write_leaf(production->opaque,late,collision);production->flush(production->opaque);
        baseline[late/PAGE]=collision;reset_counts();
        need(frame_pool_init(&failed_pool,private_copy,size,&real,&instrumented,0)==FRAME_ALIAS_CONFLICT,
             "late real-leaf collision not rejected");
        need(!writes && !flushes && !aliases,"collision preflight mutated real leaves");
        no_publication();unchanged();
        production->write_leaf(production->opaque,late,0);production->flush(production->opaque);
        baseline[late/PAGE]=0;unchanged();
    }
    corrupt_leaf=late;corrupt_write=1;reset_counts();
    need(frame_pool_init(&failed_pool,private_copy,size,&real,&instrumented,0)==FRAME_CORRUPT,
         "real-leaf installation corruption not audited");
    corrupt_write=0;
    need(writes==14 && flushes==2 && !aliases,"complete real-leaf rollback and flush");
    no_publication();unchanged();

    reset_counts();
    need(frame_pool_init(&failed_pool,private_copy,size,&real,&instrumented,0)==FRAME_OK,
         "actual-map recovery after initialization failure");
    need(writes==7 && flushes==1 && !aliases,"recovery creates exactly seven real aliases");
    struct frame_stats stats;
    need(frame_pool_stats(&failed_pool,&stats)==FRAME_OK,"recovered pool audit");
    need(stats.eligible==7 && stats.managed==7 && stats.roles[FRAME_FREE]==7 && stats.permanent_aliases==7,
         "recovered real pool accounting");
    for (unsigned role=1;role<FRAME_ROLE_COUNT;++role) need(!stats.roles[role],"recovered pool published ownership");
    unsigned next=0;
    for (unsigned i=0;i<LEAF_COUNT;++i) {
        uint64_t expected=baseline[i];
        if (next<7 && (uint64_t)i*PAGE==actual.frames[next]) {
            need(!expected,"recovery overwrote original mapping");
            expected=actual.frames[next++]|FRAME_POOL_NX|3;
        }
        need(leaf_bits(production->read_leaf(production->opaque,(uint64_t)i*PAGE))==expected,
             "recovery changed unrelated real mapping");
    }
    need(next==7,"recovery exact alias coverage");
    /* No frame was allocated or dereferenced by these init-only tests. Remove
     * their aliases, flush, and discard their private metadata before real init. */
    for (unsigned i=0;i<7;++i) production->write_leaf(production->opaque,actual.frames[i],0);
    production->flush(production->opaque);fill(&failed_pool,sizeof(failed_pool),0);
    no_publication();unchanged();
}
void frame_boot_tests(const void *private_copy,size_t size,uint64_t original,
                      const struct frame_platform *platform) {
    production=platform;
    need(production && production->context_ok && production->read_leaf && production->write_leaf &&
         production->flush && production->alias,"production fixture backend");
    need(production->context_ok(production->opaque),"fixture BSP context");
    snapshot();selection_fixtures();malformed_fixtures();allocation_tag_fixtures();unchanged();
    actual_initialization_fixtures(private_copy,size,original);
    say("X64 FRAME PASS guest boot exclusions and initialization failures\n");
}
