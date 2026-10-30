// The C half of the cross-boundary consumer. It is compiled by an ordinary C compiler for i686 with the same soft-float choice
// the GTOS target spec names, and it declares the Rust functions the way a real caller would: no headers, no name mangling, no
// generated glue. If a declaration and the Rust definition disagree, the link or the assertions here are what say so.
//
// The struct layout is asserted here and in the Rust source. Neither side can silently disagree: both are compile errors.
//
// Two entry points share one computation:
//
//   _start           the default: computes every value and folds them into one word, then returns. The inspection build uses it.
//   _start (RUN)     built with -DGTOS_ABI_RUN: the same computation, then every value is written out and the image exits. That is
//                    the build a run stage starts, so the numbers the Rust functions actually returned are the evidence rather than
//                    a disassembly of the call sites.
//
// The write and exit calls in the RUN build are the host's, because the thing that starts this image is a Linux user-mode emulator.
// That makes a RUN result evidence about the host ABI of the pair of compilers, never about GTOS accepting the image: the GTOS side
// of the claim is that the same objects are what a GTOS image would contain.

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

// The same shape the Rust side declares, so a wrong offset or calling convention in the table shows up as a wrong result rather
// than as a link failure. The assertion is the C half of the Rust size assertion.
struct abi_table {
    unsigned (*add)(unsigned, unsigned);
    unsigned long long (*wide)(unsigned, unsigned);
};

_Static_assert(sizeof(struct abi_table) == 8, "struct abi_table must be 8 bytes on i686");

extern const struct abi_table *gtos_abi_table(void);

// The values the run stage reads back, in the order it prints them: the five direct results, the table's two, what the Rust
// function wrote through the pointer, and the fold.
enum {
    VALUE_SUM,
    VALUE_FOLDED,
    VALUE_COPIED,
    VALUE_WIDE_HIGH,
    VALUE_WIDE_LOW,
    VALUE_TABLE_SUM,
    VALUE_TABLE_WIDE_HIGH,
    VALUE_TABLE_WIDE_LOW,
    VALUE_STORED,
    VALUE_FOLD,
    VALUE_COUNT
};

static unsigned output;

static unsigned compute(unsigned values[VALUE_COUNT]) {
    struct pair sample;
    sample.low = 0x11111111u;
    sample.high = 0x2222u;
    sample.tag = 0x33u;
    unsigned sum = gtos_abi_add(sample.low, sample.high);
    unsigned folded = gtos_abi_pair(&sample);
    unsigned copied = gtos_abi_pair_value(sample);
    unsigned long long wide = gtos_abi_wide(sum, folded);
    // Calling the same two functions through the table is the case a name-based link cannot cover: if the table's offsets or the
    // member types were wrong, these two values would not match the direct calls.
    const struct abi_table *table = gtos_abi_table();
    unsigned table_sum = table->add(sample.low, sample.high);
    unsigned long long table_wide = table->wide(sum, folded);
    // The pointer-out case: what the Rust function stored is read back before anything else touches the word.
    unsigned stored = 0;
    gtos_abi_store(&stored);
    values[VALUE_SUM] = sum;
    values[VALUE_FOLDED] = folded;
    values[VALUE_COPIED] = copied;
    values[VALUE_WIDE_HIGH] = (unsigned)(wide >> 32);
    values[VALUE_WIDE_LOW] = (unsigned)wide;
    values[VALUE_TABLE_SUM] = table_sum;
    values[VALUE_TABLE_WIDE_HIGH] = (unsigned)(table_wide >> 32);
    values[VALUE_TABLE_WIDE_LOW] = (unsigned)table_wide;
    values[VALUE_STORED] = stored;
    // One observable result, so the program a real target would run has something to write.
    output = sum ^ folded ^ copied ^ (unsigned)(wide >> 32) ^ (unsigned)wide ^ table_sum ^ (unsigned)(table_wide >> 32) ^
             (unsigned)table_wide ^ (sum == table_sum ? 0u : 0xDEADu) ^ (wide == table_wide ? 0u : 0xBEEFu);
    values[VALUE_FOLD] = output;
    return output;
}

#ifdef GTOS_ABI_RUN

// The host's write and exit, used only by the run build. A GTOS image would reach a console and a stop service instead, and that is
// precisely the part this build is not evidence for.
//
// eax is an in-out operand because an i386 system call returns its status there: declaring it as an input only lets the compiler
// hoist the syscall number out of the loop, and the second call would then be a different system call entirely. Execution found
// that; inspection alone did not.
static void write_hex(unsigned value) {
    char text[9];
    for (int index = 7; index >= 0; --index) {
        text[index] = "0123456789abcdef"[value & 0xfu];
        value >>= 4;
    }
    text[8] = '\n';
    register unsigned syscall __asm__("eax") = 4;
    __asm__ volatile("int $0x80" : "+a"(syscall) : "b"(1), "c"(text), "d"(9) : "memory", "cc");
}

void _start(void) {
    unsigned values[VALUE_COUNT];
    compute(values);
    for (int index = 0; index < VALUE_COUNT; ++index) {
        write_hex(values[index]);
    }
    register unsigned stop __asm__("eax") = 1;
    __asm__ volatile("int $0x80" : "+a"(stop) : "b"(0) : "memory", "cc");
    for (;;) {
    }
}

#else

void _start(void) {
    unsigned values[VALUE_COUNT];
    compute(values);
}

#endif
