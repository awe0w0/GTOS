/* Compiler interoperability in a user-mode emulator; write/exit below are
 * Linux reporting calls, not GTOS guest or FP-context acceptance. */
typedef unsigned u32;
struct Pair { float small; double wide; u32 tag; };
struct Table { float (*add32)(float,float); double (*mix64)(double,double); };
_Static_assert(sizeof(struct Pair)==16 && __builtin_offsetof(struct Pair,wide)==4,"GTOS float layout");
extern float rust_add32(float,float),rust_identity32(float);
extern double rust_mix64(double,double),rust_identity64(double),rust_call_c64(double,double),rust_roundtrip64(double);
extern struct Pair rust_pair(struct Pair),rust_call_c_pair(struct Pair);
extern const struct Table *rust_float_table(void);

double c_mix64(double a,double b) { return a+b*2.0; }
double c_identity64(double value) { return value; }
struct Pair c_pair(struct Pair value) {
    value.small-=1.0f; value.wide+=0.5; ++value.tag; return value;
}
static u32 bits32(float value) { union {float value;u32 bits;} item={.value=value};return item.bits; }
static int bits64(double value,u32 high,u32 low) {
    union {double value;u32 words[2];} item={.value=value};return item.words[0]==low && item.words[1]==high;
}
static double from64(u32 high,u32 low) {
    union {double value;u32 words[2];} item={.words={low,high}};return item.value;
}
static float from32(u32 bits) { union {float value;u32 bits;} item={.bits=bits};return item.value; }
static void report(const char *text,u32 length) {
    u32 operation=4;
    __asm__ volatile("int $0x80":"+a"(operation):"b"(1),"c"(text),"d"(length):"memory","cc");
}
int TestMain(void) {
    u32 failures=0;
#ifdef GTOS_FLOAT_BAD_EXPECTATION
    failures+=bits32(rust_add32(1.25f,2.5f))!=0x40800000u;
#else
    failures+=bits32(rust_add32(1.25f,2.5f))!=0x40700000u;
#endif
    failures+=!bits64(rust_mix64(1.25,2.5),0x40140000,0);
    failures+=!bits64(rust_call_c64(1.25,2.5),0x40190000,0);
    const struct Table *table=rust_float_table();
    failures+=bits32(table->add32(1.25f,2.5f))!=0x40700000u;
    failures+=!bits64(table->mix64(1.25,2.5),0x40140000,0);
    struct Pair source={1.5f,-2.25,0x1122};
    struct Pair a=rust_pair(source),b=rust_call_c_pair(source);
    failures+=bits32(a.small)!=0x40200000u || !bits64(a.wide,0xc0120000,0) || a.tag!=(0x1122^0x5a5a);
    failures+=bits32(b.small)!=0x3f000000u || !bits64(b.wide,0xbffc0000,0) || b.tag!=0x1123;
    const u32 singles[]={0,0x80000000,1,0x7f800000,0xff800000};
    for(u32 i=0;i<5;++i)failures+=bits32(rust_identity32(from32(singles[i])))!=singles[i];
    const u32 doubles[][2]={{0,0},{0x80000000,0},{0,1},{0x7ff00000,0},{0xfff00000,0}};
    for(u32 i=0;i<5;++i) {
        double value=from64(doubles[i][0],doubles[i][1]);
        failures+=!bits64(rust_identity64(value),doubles[i][0],doubles[i][1]);
        failures+=!bits64(rust_roundtrip64(value),doubles[i][0],doubles[i][1]);
    }
    static const char pass[]="C_RUST_FLOAT_ABI_PASS 22\n",fail[]="C_RUST_FLOAT_ABI_FAIL\n";
    if(failures)report(fail,sizeof(fail)-1);else report(pass,sizeof(pass)-1);
    return (int)failures;
}
