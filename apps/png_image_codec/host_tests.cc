// Host qualification only. Uses real png_decode/png_pixel APIs and source oracle.
#include "png_decode.h"
#include "png_pixel.h"
#include "png_fixtures.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>

namespace {

const unsigned char kCanary = 0xa5;
const uint32_t kGuard = 32;
const uint32_t kHostAllocationLimit = 16u * 1024u * 1024u;
uint32_t calls = 0, prefixes = 0, mutations = 0, valid_mutations = 0;
uint32_t overlap_cases = 0, boundary_cases = 0;
const char* current = "startup";

[[noreturn]] void fail(const char* condition, int line) {
    std::fprintf(stderr, "GTOS PNG HOST FAIL case=%s line=%d condition=%s\n",
                 current, line, condition);
    std::exit(1);
}
#define CHECK(condition) do { if (!(condition)) fail(#condition, __LINE__); } while (0)

uint32_t u32(uint64_t value) {
    CHECK(value <= UINT32_MAX);
    return static_cast<uint32_t>(value);
}

uint32_t span(uint32_t stride, uint32_t row, uint32_t height) {
    CHECK(height);
    return u32(uint64_t(height - 1) * stride + row);
}

struct Guarded {
    unsigned char* allocation;
    unsigned char* data;
    uint32_t size;
    explicit Guarded(uint32_t bytes) : allocation(nullptr), data(nullptr), size(bytes) {
        CHECK(bytes <= kHostAllocationLimit);
        allocation = static_cast<unsigned char*>(
            std::malloc(std::size_t(bytes) + 2 * kGuard + 7));
        CHECK(allocation != nullptr);
        uintptr_t start = reinterpret_cast<uintptr_t>(allocation + kGuard);
        data = reinterpret_cast<unsigned char*>((start + 7) & ~uintptr_t(7));
        reset();
    }
    ~Guarded() { std::free(allocation); }
    Guarded(const Guarded&) = delete;
    Guarded& operator=(const Guarded&) = delete;
    void reset() { std::memset(data - kGuard, kCanary, std::size_t(size) + 2*kGuard); }
    void guards() const {
        for (uint32_t i = 0; i < kGuard; ++i) {
            CHECK(data[-static_cast<ptrdiff_t>(i)-1] == kCanary);
            CHECK(data[size+i] == kCanary);
        }
    }
    void unchanged() const {
        for (uint32_t i = 0; i < size; ++i) CHECK(data[i] == kCanary);
    }
};

void outside_region(const Guarded& owner, const void* pointer, uint32_t bytes) {
    uintptr_t base=reinterpret_cast<uintptr_t>(owner.data);
    uintptr_t start=reinterpret_cast<uintptr_t>(pointer);
    if (start<base || start>base+owner.size || bytes>base+owner.size-start) return;
    uint32_t offset=static_cast<uint32_t>(start-base);
    for(uint32_t i=0;i<offset;++i) CHECK(owner.data[i]==kCanary);
    for(uint32_t i=offset+bytes;i<owner.size;++i) CHECK(owner.data[i]==kCanary);
}

enum Mode { Inspect, Bgra, Rgba };
enum Region { ContextRegion, InputRegion, WorkRegion, BgraRegion, RgbaRegion,
              RequirementsRegion };

struct Call {
    void* context;
    uint32_t context_bytes;
    const unsigned char* input;
    uint32_t input_bytes;
    unsigned char* work;
    uint32_t work_bytes;
    unsigned char* bgra;
    uint32_t bgra_bytes, bgra_stride;
    unsigned char* rgba;
    uint32_t rgba_bytes, rgba_stride;
    gtos_png_requirements* result;
};

gtos_png_status invoke(Mode mode, const Call& call) {
    ++calls;
    if (mode == Inspect) {
        return gtos_png_inspect(call.context, call.context_bytes, call.input,
                                call.input_bytes, call.result);
    }
    if (mode == Bgra) {
        return gtos_png_decode_bgra(call.context, call.context_bytes, call.input,
            call.input_bytes, call.work, call.work_bytes, call.bgra,
            call.bgra_bytes, call.bgra_stride, call.result);
    }
    return gtos_png_decode_rgba(call.context, call.context_bytes, call.input,
        call.input_bytes, call.work, call.work_bytes, call.bgra, call.bgra_bytes,
        call.bgra_stride, call.rgba, call.rgba_bytes, call.rgba_stride, call.result);
}

bool same(const gtos_png_requirements& a, const gtos_png_requirements& b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

gtos_png_requirements marker() {
    gtos_png_requirements result;
    std::memset(&result, kCanary, sizeof(result));
    return result;
}

void pixels(const unsigned char* actual, uint32_t capacity, uint32_t stride,
            const gtos_png_requirements& req, const unsigned char* expected) {
    CHECK(req.width && req.height && req.row_bytes == uint64_t(req.width) * 4);
    CHECK(stride >= req.row_bytes && span(stride, req.row_bytes, req.height) <= capacity);
    for (uint32_t position = 0; position < capacity; ++position) {
        uint32_t y = position / stride, x = position % stride;
        if (y < req.height && x < req.row_bytes) {
            if (expected) {
                unsigned char want=expected[y*req.row_bytes+x];
                if(actual[position]!=want)
                    std::fprintf(stderr,"GTOS PNG PIXEL MISMATCH case=%s offset=%u row=%u "
                                 "row_byte=%u actual=0x%02x expected=0x%02x\n",
                                 current,position,y,x,unsigned(actual[position]),unsigned(want));
                CHECK(actual[position]==want);
            }
        } else {
            if(actual[position]!=kCanary)
                std::fprintf(stderr,"GTOS PNG PADDING MISMATCH case=%s offset=%u "
                             "actual=0x%02x expected=0x%02x\n",current,position,
                             unsigned(actual[position]),unsigned(kCanary));
            CHECK(actual[position] == kCanary); // Every padding/unused byte.
        }
    }
}

void bridge_oracle(const Call& call, const gtos_png_requirements& req) {
    CHECK(span(call.bgra_stride, req.row_bytes, req.height) <= call.bgra_bytes);
    CHECK(span(call.rgba_stride, req.row_bytes, req.height) <= call.rgba_bytes);
    for (uint32_t y = 0; y < req.height; ++y) {
        for (uint32_t x = 0; x < req.width; ++x) {
            const unsigned char* b = call.bgra + y*call.bgra_stride + x*4;
            const unsigned char* r = call.rgba + y*call.rgba_stride + x*4;
            CHECK(r[0] == (uint32_t(b[2])*b[3] + 127)/255);
            CHECK(r[1] == (uint32_t(b[1])*b[3] + 127)/255);
            CHECK(r[2] == (uint32_t(b[0])*b[3] + 127)/255);
            CHECK(r[3] == b[3]);
        }
    }
}

struct Session {
    gtos_png_requirements needed;
    Guarded context, work, bgra, rgba, input, input_before;
    alignas(8) gtos_png_requirements result;
    uint32_t bgra_stride, rgba_stride, input_bytes;
    Session(const gtos_png_requirements& req)
        : needed(req), context(u32(uint64_t(req.context_bytes)+64)),
          work(u32(uint64_t(req.work_bytes)+64)),
          bgra(u32(uint64_t(req.height)*(req.row_bytes+5u)+64)),
          rgba(u32(uint64_t(req.height)*(req.row_bytes+7u)+64)),
          input(GTOS_PNG_FIXTURE_MAX_PNG_BYTES+64),
          input_before(GTOS_PNG_FIXTURE_MAX_PNG_BYTES+64),
          result(marker()), bgra_stride(req.row_bytes+5),
          rgba_stride(req.row_bytes+7), input_bytes(0) {}

    void reset(const unsigned char* bytes, uint32_t count) {
        CHECK(count <= input.size);
        context.reset(); work.reset(); bgra.reset(); rgba.reset(); input.reset();
        result = marker(); input_bytes = count;
        if (count) std::memcpy(input.data, bytes, count);
        std::memcpy(input_before.data, input.data, input.size);
    }

    Call call() {
        return {context.data, context.size, input.data, input_bytes,
                work.data, work.size, bgra.data, bgra.size, bgra_stride,
                rgba.data, rgba.size, rgba_stride, &result};
    }

    void finish(Mode mode, const Call& call, gtos_png_status status) {
        context.guards(); work.guards(); bgra.guards(); rgba.guards(); input.guards();
        CHECK(std::memcmp(input.data, input_before.data, input.size) == 0);
        outside_region(context,call.context,call.context_bytes);
        if(mode!=Inspect) {
            outside_region(work,call.work,call.work_bytes);
            outside_region(bgra,call.bgra,call.bgra_bytes);
        } else {
            work.unchanged(); bgra.unchanged();
        }
        if(mode==Rgba) outside_region(rgba,call.rgba,call.rgba_bytes);
        if (status != GTOS_PNG_OK) {
            CHECK(same(result, marker()));
            rgba.unchanged();
        } else if (mode != Rgba) {
            rgba.unchanged();
        }
    }
};

gtos_png_requirements inspect_positive(const gtos_png_fixture& fixture) {
    current = fixture.name;
    uint32_t context_bytes = gtos_png_context_bytes();
    CHECK(context_bytes && !(context_bytes & 7));
    Guarded context(context_bytes);
    Guarded input(fixture.png_bytes);
    std::memcpy(input.data, fixture.png, fixture.png_bytes);
    gtos_png_requirements req = marker();
    ++calls;
    CHECK(gtos_png_inspect(context.data, context_bytes, input.data,
                           fixture.png_bytes, &req) == GTOS_PNG_OK);
    context.guards(); input.guards();
    CHECK(std::memcmp(input.data, fixture.png, fixture.png_bytes) == 0);
    CHECK(req.context_bytes == context_bytes);
    CHECK(req.width == fixture.width && req.height == fixture.height);
    CHECK(req.row_bytes == uint64_t(fixture.width)*4);
    CHECK(req.bgra_bytes == uint64_t(fixture.width)*fixture.height*4);
    CHECK(req.bgra_bytes == fixture.expected_bytes);
    CHECK(req.work_bytes <= kHostAllocationLimit-64);
    return req;
}

void success(Session& session, Mode mode, const Call& call,
             const gtos_png_fixture& fixture, const unsigned char* bgra = nullptr,
             const unsigned char* rgba = nullptr) {
    gtos_png_status status = invoke(mode, call);
    CHECK(status == GTOS_PNG_OK);
    session.finish(mode, call, status);
    CHECK(same(session.result, session.needed));
    if (mode == Inspect) return;
    pixels(call.bgra, call.bgra_bytes, call.bgra_stride, session.result,
           bgra ? bgra : fixture.expected_bgra);
    for (uint32_t i = session.needed.work_bytes; i < session.work.size; ++i) {
        // Check only work storage belonging to the default independently owned buffer.
        if (call.work == session.work.data) CHECK(session.work.data[i] == kCanary);
    }
    if (mode == Rgba) {
        pixels(call.rgba, call.rgba_bytes, call.rgba_stride, session.result,
               rgba ? rgba : fixture.expected_premul_rgba);
        bridge_oracle(call, session.result);
    }
}

void failure(Session& session, Mode mode, const Call& call, gtos_png_status expected) {
    gtos_png_status status = invoke(mode, call);
    CHECK(status == expected);
    session.finish(mode, call, status);
    ++boundary_cases;
}

const gtos_png_fixture& named(const char* name) {
    for (uint32_t i=0; i<GTOS_PNG_FIXTURE_COUNT; ++i) {
        if (!std::strcmp(gtos_png_fixtures[i].name, name)) return gtos_png_fixtures[i];
    }
    fail("fixture name exists", __LINE__);
}

void test_fixtures() {
    const gtos_png_fixture& base = named("rgba8_stored_alpha");
    gtos_png_requirements baseline = inspect_positive(base);
    for (uint32_t i=0; i<GTOS_PNG_FIXTURE_COUNT; ++i) {
        const gtos_png_fixture& fixture = gtos_png_fixtures[i];
        current = fixture.name;
        if (fixture.classification == GTOS_PNG_CONFORMING) {
            gtos_png_requirements req = inspect_positive(fixture);
            Session session(req);
            for (Mode mode : {Bgra, Rgba}) {
                session.reset(fixture.png, fixture.png_bytes);
                success(session, mode, session.call(), fixture);
                // Exact capacities are accepted, including odd padded strides.
                session.reset(fixture.png, fixture.png_bytes);
                Call exact = session.call();
                exact.context_bytes = req.context_bytes;
                exact.work_bytes = req.work_bytes;
                exact.bgra_bytes = span(exact.bgra_stride, req.row_bytes, req.height);
                exact.rgba_bytes = span(exact.rgba_stride, req.row_bytes, req.height);
                success(session, mode, exact, fixture);
                for (uint32_t n=0; n<fixture.png_bytes; ++n) {
                    session.reset(fixture.png, n);
                    gtos_png_status status = invoke(mode, session.call());
                    CHECK(status != GTOS_PNG_OK);
                    session.finish(mode, session.call(), status);
                    ++prefixes;
                }
            }
        } else {
            Session session(baseline);
            session.reset(fixture.png, fixture.png_bytes);
            gtos_png_status inspected = invoke(Inspect, session.call());
            session.finish(Inspect, session.call(), inspected);
            if (fixture.classification == GTOS_PNG_REQUIRES_EXTENSION) {
                CHECK(inspected == GTOS_PNG_UNSUPPORTED);
            } else {
                // inspect validates the envelope/config; decode validates IDAT.
                CHECK(inspected == GTOS_PNG_OK || inspected == GTOS_PNG_CORRUPT);
            }
            gtos_png_requirements negative_req = inspected == GTOS_PNG_OK
                ? session.result : baseline;
            Session decoder(negative_req);
            for (Mode mode : {Bgra, Rgba}) {
                decoder.reset(fixture.png, fixture.png_bytes);
                failure(decoder, mode, decoder.call(),
                    fixture.classification == GTOS_PNG_REQUIRES_EXTENSION
                        ? GTOS_PNG_UNSUPPORTED : GTOS_PNG_CORRUPT);
                if (inspected == GTOS_PNG_OK) {
                    // Late failures may alter active BGRA/work, but not padding
                    // or spare capacities. Use independently inspected geometry.
                    pixels(decoder.bgra.data, decoder.bgra.size,
                           decoder.bgra_stride, negative_req, nullptr);
                    for (uint32_t tail=negative_req.work_bytes; tail<decoder.work.size; ++tail)
                        CHECK(decoder.work.data[tail]==kCanary);
                }
            }
        }
    }
}

void set_region(Call& call, Region region, unsigned char* pointer) {
    switch (region) {
        case ContextRegion: call.context = pointer; break;
        case InputRegion: call.input = pointer; break;
        case WorkRegion: call.work = pointer; break;
        case BgraRegion: call.bgra = pointer; break;
        case RgbaRegion: call.rgba = pointer; break;
        case RequirementsRegion:
            call.result = reinterpret_cast<gtos_png_requirements*>(pointer); break;
    }
}

uint32_t region_size(const Call& call, Region region) {
    switch (region) {
        case ContextRegion: return call.context_bytes;
        case InputRegion: return call.input_bytes;
        case WorkRegion: return call.work_bytes;
        case BgraRegion: return call.bgra_bytes;
        case RgbaRegion: return call.rgba_bytes;
        case RequirementsRegion: return sizeof(gtos_png_requirements);
    }
    fail("known region", __LINE__);
}

bool used(Mode mode, Region region) {
    if (mode == Inspect) return region == ContextRegion || region == InputRegion ||
                               region == RequirementsRegion;
    return mode == Rgba || region != RgbaRegion;
}

void test_boundaries() {
    current = "buffer_boundaries";
    const gtos_png_fixture& base = named("rgba8_stored_alpha");
    gtos_png_requirements req = inspect_positive(base);
    Session session(req);
    for (Mode mode : {Inspect, Bgra, Rgba}) {
        current = "argument_and_capacity_boundaries";
        for (uint32_t fault=0; fault<16; ++fault) {
            if (mode == Inspect && fault >= 6) continue;
            if (mode == Bgra && fault >= 12) continue;
            if (fault == 7 && !req.work_bytes) continue;
            session.reset(base.png, base.png_bytes);
            Guarded misaligned_result(sizeof(gtos_png_requirements)+8);
            Call call = session.call();
            gtos_png_status expected = GTOS_PNG_BAD_ARGUMENT;
            switch (fault) {
                case 0: call.context = nullptr; break;
                case 1: call.context = session.context.data+1; break;
                case 2: call.context_bytes = req.context_bytes-1; expected=GTOS_PNG_TOO_SMALL; break;
                case 3: call.input = nullptr; break;
                case 4: call.result = nullptr; break;
                case 5: call.result = reinterpret_cast<gtos_png_requirements*>(misaligned_result.data+1); break;
                case 6: call.work = nullptr; break;
                case 7: call.work_bytes = req.work_bytes-1; expected=GTOS_PNG_TOO_SMALL; break;
                case 8: call.bgra = nullptr; break;
                case 9: call.bgra_stride = req.row_bytes-1; break;
                case 10: call.bgra_bytes = span(call.bgra_stride, req.row_bytes, req.height)-1;
                         expected=GTOS_PNG_TOO_SMALL; break;
                case 11: call.bgra_stride = 0; break;
                case 12: call.rgba = nullptr; break;
                case 13: call.rgba_stride = req.row_bytes-1; break;
                case 14: call.rgba_bytes = span(call.rgba_stride, req.row_bytes, req.height)-1;
                         expected=GTOS_PNG_TOO_SMALL; break;
                case 15: call.rgba_stride = 0; break;
            }
            failure(session, mode, call, expected);
            if(fault==5) { misaligned_result.unchanged(); misaligned_result.guards(); }
        }
        for (uint32_t offset=1; offset<8; ++offset) {
            session.reset(base.png, base.png_bytes);
            Call call=session.call(); call.context=session.context.data+offset;
            failure(session, mode, call, GTOS_PNG_BAD_ARGUMENT);
        }
        for (uint32_t region=0; region<=RequirementsRegion; ++region) {
            Region which=static_cast<Region>(region);
            if (!used(mode, which)) continue;
            session.reset(base.png, base.png_bytes);
            Call call=session.call();
            unsigned char* fake=reinterpret_cast<unsigned char*>(UINTPTR_MAX-7);
            set_region(call, which, fake);
            failure(session, mode, call, GTOS_PNG_POINTER_OVERFLOW);
        }
        for (uint32_t a=0; a<=RequirementsRegion; ++a) {
            for (uint32_t b=a+1; b<=RequirementsRegion; ++b) {
                Region first=static_cast<Region>(a), second=static_cast<Region>(b);
                if (!used(mode, first) || !used(mode, second)) continue;
                current="pairwise_aliases";
                session.reset(base.png, base.png_bytes);
                Call call=session.call();
                uint32_t capacity=region_size(call, first);
                if (region_size(call, second)>capacity) capacity=region_size(call, second);
                Guarded shared(capacity), before(capacity);
                if (first==InputRegion || second==InputRegion)
                    std::memcpy(shared.data, base.png, base.png_bytes);
                std::memcpy(before.data, shared.data, capacity);
                set_region(call, first, shared.data); set_region(call, second, shared.data);
                failure(session, mode, call, GTOS_PNG_OVERLAP);
                shared.guards(); CHECK(!std::memcmp(shared.data, before.data, capacity));
                ++overlap_cases;
            }
        }
    }

    for (Mode mode : {Bgra, Rgba}) {
        current="byte_buffer_alignment";
        session.reset(base.png, base.png_bytes);
        // Byte buffers can be unaligned; context/result retain required alignment.
        std::memmove(session.input.data+1, session.input.data, base.png_bytes);
        std::memcpy(session.input_before.data, session.input.data, session.input.size);
        Call call=session.call();
        call.input+=1; call.work+=1; call.work_bytes=req.work_bytes;
        call.bgra+=1; call.bgra_bytes-=1; call.rgba+=1; call.rgba_bytes-=1;
        success(session, mode, call, base);
        CHECK(session.work.data[0]==kCanary && session.bgra.data[0]==kCanary &&
              session.rgba.data[0]==kCanary);

        current="full_capacity_overlap";
        session.reset(base.png, base.png_bytes); call=session.call();
        Guarded shared(u32(uint64_t(call.work_bytes)+call.bgra_bytes+16));
        Guarded before(shared.size);
        call.work=shared.data;
        call.bgra=shared.data+req.work_bytes+16; // Only spare work capacity overlaps.
        std::memcpy(before.data, shared.data, shared.size);
        failure(session, mode, call, GTOS_PNG_OVERLAP);
        CHECK(!std::memcmp(shared.data, before.data, shared.size)); shared.guards();
        ++overlap_cases;

        current="adjacent_regions";
        session.reset(base.png, base.png_bytes); shared.reset(); call=session.call();
        call.work=shared.data; call.bgra=shared.data+call.work_bytes;
        success(session, mode, call, base); shared.guards();
        for(uint32_t tail=call.work_bytes+call.bgra_bytes;tail<shared.size;++tail)
            CHECK(shared.data[tail]==kCanary);

        current="exact_eof";
        session.reset(base.png, base.png_bytes);
        session.input.data[base.png_bytes]=0;
        std::memcpy(session.input_before.data, session.input.data, session.input.size);
        call=session.call(); ++call.input_bytes;
        failure(session, mode, call, GTOS_PNG_CORRUPT);
    }
}

uint32_t read_be32(const unsigned char* p) {
    return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];
}
void write_be32(unsigned char* p,uint32_t value) {
    p[0]=static_cast<unsigned char>(value>>24); p[1]=static_cast<unsigned char>(value>>16);
    p[2]=static_cast<unsigned char>(value>>8); p[3]=static_cast<unsigned char>(value);
}
uint32_t crc32(const unsigned char* p,uint32_t n) {
    uint32_t crc=UINT32_MAX;
    for(uint32_t i=0;i<n;++i) {
        crc^=p[i];
        for(unsigned int bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u & (0u-(crc&1)));
    }
    return crc^UINT32_MAX;
}
uint32_t adler32(const unsigned char* p,uint32_t n) {
    uint32_t a=1,b=0;
    for(uint32_t i=0;i<n;++i) { a=(a+p[i])%65521; b=(b+a)%65521; }
    return (b<<16)|a;
}
uint32_t next_random(uint32_t& state) {
    state^=state<<13; state^=state>>17; state^=state<<5;
    return state;
}

bool repair_idat_crc(unsigned char* png,uint32_t n,uint32_t changed) {
    for(uint32_t offset=8;offset+12<=n;) {
        uint32_t length=read_be32(png+offset);
        if(uint64_t(offset)+length+12>n) return false;
        if(!std::memcmp(png+offset+4,"IDAT",4) && changed>=offset+8 && changed<offset+8+length) {
            write_be32(png+offset+8+length,crc32(png+offset+4,length+4));
            return true;
        }
        offset+=length+12;
    }
    return false;
}

void stored_mutation(unsigned char* png,const gtos_png_fixture& fixture,
                     uint32_t mutation_index,unsigned char* bgra,unsigned char* rgba) {
    // Source fixtures have one stored block with filter0 in both rows. Mutate an
    // actual source sample, repair RFC1950/PNG checksums, then derive the oracle
    // directly from the changed uncompressed sample bytes.
    uint32_t offset=8;
    while(std::memcmp(png+offset+4,"IDAT",4)) offset+=read_be32(png+offset)+12;
    uint32_t length=read_be32(png+offset);
    unsigned char* stream=png+offset+8;
    CHECK(stream[2]==1);
    uint32_t raw_bytes=uint32_t(stream[3])|(uint32_t(stream[4])<<8);
    CHECK(raw_bytes+11==length);
    unsigned char* raw=stream+7;
    uint32_t row_bytes=fixture.width*4, scanline=row_bytes+1;
    uint32_t source_sample=(mutation_index/8)%(fixture.width*fixture.height*4);
    uint32_t y=source_sample/row_bytes, sample=source_sample%row_bytes;
    raw[y*scanline+1+sample]^=static_cast<unsigned char>(1u<<(mutation_index%8));
    write_be32(stream+length-4,adler32(raw,raw_bytes));
    write_be32(png+offset+8+length,crc32(png+offset+4,length+4));
    for(uint32_t row=0;row<fixture.height;++row) {
        CHECK(raw[row*scanline]==0);
        for(uint32_t x=0;x<fixture.width;++x) {
            const unsigned char* p=raw+row*scanline+1+x*4;
            uint32_t index=(row*fixture.width+x)*4;
            bgra[index]=p[2]; bgra[index+1]=p[1]; bgra[index+2]=p[0]; bgra[index+3]=p[3];
            for(uint32_t c=0;c<3;++c) rgba[index+c]=static_cast<unsigned char>(
                (uint32_t(p[c])*p[3]+127)/255);
            rgba[index+3]=p[3];
        }
    }
}

void test_mutations() {
    const gtos_png_fixture& base=named("rgba8_stored_alpha");
    gtos_png_requirements req=inspect_positive(base);
    Session session(req);
    Guarded first_bgra(session.bgra.size), first_rgba(session.rgba.size);
    unsigned char input[GTOS_PNG_FIXTURE_MAX_PNG_BYTES];
    unsigned char expected_bgra[GTOS_PNG_FIXTURE_MAX_PIXELS*4];
    unsigned char expected_rgba[GTOS_PNG_FIXTURE_MAX_PIXELS*4];
    uint32_t random=0x504e4753u;
    for(uint32_t iteration=0;iteration<2000;++iteration) {
        const gtos_png_fixture* fixture=&base;
        if(iteration%4) {
            uint32_t choice=next_random(random)%15;
            for(uint32_t i=0;i<GTOS_PNG_FIXTURE_COUNT;++i) {
                if(gtos_png_fixtures[i].classification!=GTOS_PNG_CONFORMING) continue;
                if(!choice--) { fixture=&gtos_png_fixtures[i]; break; }
            }
        }
        current="seeded_mutation";
        std::memcpy(input,fixture->png,fixture->png_bytes);
        bool independent_oracle=iteration%4==0;
        if(independent_oracle) {
            stored_mutation(input,*fixture,iteration/4,expected_bgra,expected_rgba);
        } else {
            uint32_t changed=next_random(random)%fixture->png_bytes;
            input[changed]^=static_cast<unsigned char>(1u<<(next_random(random)%8));
            if(iteration%4==2) repair_idat_crc(input,fixture->png_bytes,changed);
            if(iteration%4==3) {
                uint32_t second=next_random(random)%fixture->png_bytes;
                input[second]^=static_cast<unsigned char>(1u<<(next_random(random)%8));
            }
        }
        // The fixed bounded buffers also exercise resource rejection when an
        // otherwise accepted mutation requires geometry larger than baseline.
        session.reset(input,fixture->png_bytes);
        Call call=session.call();
        gtos_png_status status=invoke(Rgba,call);
        session.finish(Rgba,call,status);
        if(independent_oracle) {
            CHECK(status==GTOS_PNG_OK);
            CHECK(same(session.result,req));
            pixels(call.bgra,call.bgra_bytes,call.bgra_stride,session.result,expected_bgra);
            pixels(call.rgba,call.rgba_bytes,call.rgba_stride,session.result,expected_rgba);
            ++valid_mutations;
        } else if(status==GTOS_PNG_OK) {
            pixels(call.bgra,call.bgra_bytes,call.bgra_stride,session.result,nullptr);
            pixels(call.rgba,call.rgba_bytes,call.rgba_stride,session.result,nullptr);
        }
        if(status==GTOS_PNG_OK) {
            bridge_oracle(call,session.result);
            for(uint32_t tail=session.result.work_bytes;tail<session.work.size;++tail)
                CHECK(session.work.data[tail]==kCanary);
        }
        gtos_png_requirements first_result=session.result;
        std::memcpy(first_bgra.data,session.bgra.data,session.bgra.size);
        std::memcpy(first_rgba.data,session.rgba.data,session.rgba.size);
        session.reset(input,fixture->png_bytes);
        gtos_png_status repeated=invoke(Rgba,session.call());
        session.finish(Rgba,session.call(),repeated);
        CHECK(repeated==status && same(first_result,session.result));
        CHECK(!std::memcmp(first_rgba.data,session.rgba.data,session.rgba.size));
        if(status==GTOS_PNG_OK) {
            CHECK(!std::memcmp(first_bgra.data,session.bgra.data,session.bgra.size));
            for(uint32_t tail=session.result.work_bytes;tail<session.work.size;++tail)
                CHECK(session.work.data[tail]==kCanary);
        }
        ++mutations;
    }
    // 500 accepted variants cover all 32 source samples and all eight bit positions.
    CHECK(valid_mutations==500 && valid_mutations>=base.width*base.height*4*8);
}

void test_context_reuse() {
    const gtos_png_fixture& a=named("rgba8_stored_alpha");
    const gtos_png_fixture& b=named("grayalpha8_fixed");
    const gtos_png_fixture& late=named("bad_adler");
    gtos_png_requirements req=inspect_positive(a);
    Session session(req);
    current="context_reuse";
    session.reset(a.png,a.png_bytes);
    success(session,Rgba,session.call(),a);
    // Preserve actual initialized context across success, late error, success.
    session.work.reset(); session.bgra.reset(); session.rgba.reset();
    session.input.reset(); std::memcpy(session.input.data,late.png,late.png_bytes);
    std::memcpy(session.input_before.data,session.input.data,session.input.size);
    session.input_bytes=late.png_bytes; session.result=marker();
    failure(session,Rgba,session.call(),GTOS_PNG_CORRUPT);
    session.work.reset(); session.bgra.reset(); session.rgba.reset();
    session.input.reset(); std::memcpy(session.input.data,b.png,b.png_bytes);
    std::memcpy(session.input_before.data,session.input.data,session.input.size);
    session.input_bytes=b.png_bytes; session.result=marker();
    gtos_png_status status=invoke(Rgba,session.call());
    CHECK(status==GTOS_PNG_OK); session.finish(Rgba,session.call(),status);
    CHECK(session.result.width==b.width && session.result.height==b.height);
    CHECK(session.result.row_bytes==b.width*4 && session.result.bgra_bytes==b.expected_bytes);
    pixels(session.bgra.data,session.bgra.size,session.bgra_stride,session.result,b.expected_bgra);
    pixels(session.rgba.data,session.rgba.size,session.rgba_stride,session.result,b.expected_premul_rgba);
    bridge_oracle(session.call(),session.result);
}

} // namespace

int main() {
    test_fixtures();
    test_boundaries();
    test_mutations();
    test_context_reuse();
    std::printf("GTOS PNG HOST PASS fixtures=%u prefixes=%u mutations=%u "
                "valid_mutations=%u overlaps=%u boundaries=%u calls=%u context_bytes=%u\n",
                GTOS_PNG_FIXTURE_COUNT,prefixes,mutations,valid_mutations,
                overlap_cases,boundary_cases,calls,gtos_png_context_bytes());
    return 0;
}
