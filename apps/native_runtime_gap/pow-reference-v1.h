// Independent exact integer root brackets; no target math or host floating-point oracle.
struct RuntimePowReference { double base,exponent;unsigned long long expected[4]; };
static const RuntimePowReference runtimePowReferences[] = {
    {2.0,0.5,{0x3ff6a09e667f3bcdULL,0x3ff6a09e667f3bccULL,0x3ff6a09e667f3bcdULL,0x3ff6a09e667f3bccULL}},
    {3.0,0.5,{0x3ffbb67ae8584caaULL,0x3ffbb67ae8584caaULL,0x3ffbb67ae8584cabULL,0x3ffbb67ae8584caaULL}},
    {3.0,0.25,{0x3ff50ea39fcbf166ULL,0x3ff50ea39fcbf166ULL,0x3ff50ea39fcbf167ULL,0x3ff50ea39fcbf166ULL}},
    {7.0,0.25,{0x3ffa0675250ee9f2ULL,0x3ffa0675250ee9f2ULL,0x3ffa0675250ee9f3ULL,0x3ffa0675250ee9f2ULL}},
    {1.25,1.5,{0x3ff65c55827df1d2ULL,0x3ff65c55827df1d1ULL,0x3ff65c55827df1d2ULL,0x3ff65c55827df1d1ULL}},
};
