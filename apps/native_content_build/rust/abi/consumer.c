// The C half of the cross-boundary consumer. It is compiled by an ordinary C compiler for i686 with the same soft-float choice
// the GTOS target spec names, and it declares the Rust functions the way a real caller would: no headers, no name mangling, no
// generated glue. If a declaration and the Rust definition disagree, the link or the assertions here are what say so.
//
// The struct layout is asserted here and in the Rust source. Neither side can silently disagree: both are compile errors.

struct pair {
    unsigned low;
    unsigned short high;
    unsigned char tag;
};

_Static_assert(sizeof(struct pair) == 8, "struct pair must be 8 bytes on i686");
_Static_assert(__builtin_offsetof(struct pair, low) == 0, "low must start the struct");
_Static_assert(__builtin_offsetof(struct pair, high) == 4, "high must follow the 32-bit field");
_Static_assert(__builtin_offsetof(struct pair, tag) == 6, "tag must follow the 16-bit field");

extern unsigned gtos_abi_add(unsigned a, unsigned b);
extern unsigned gtos_abi_pair(const struct pair *p);
extern void gtos_abi_store(unsigned *p);
extern unsigned gtos_abi_pair_value(struct pair p);
extern unsigned long long gtos_abi_wide(unsigned a, unsigned b);

static unsigned output;

void _start(void) {
    struct pair sample;
    sample.low = 0x11111111u;
    sample.high = 0x2222u;
    sample.tag = 0x33u;
    unsigned sum = gtos_abi_add(sample.low, sample.high);
    unsigned folded = gtos_abi_pair(&sample);
    unsigned copied = gtos_abi_pair_value(sample);
    unsigned long long wide = gtos_abi_wide(sum, folded);
    gtos_abi_store(&output);
    // One observable result, so the program a real target would run has something to write; nothing here starts it. The wide
    // return is folded back to 32 bits on purpose: returning it is what is being checked, not what it is worth here.
    output = sum ^ folded ^ copied ^ (unsigned)(wide >> 32) ^ (unsigned)wide;
}
