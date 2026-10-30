// The C half of the float boundary, on its own. The call is the whole point: the declaration must match the Rust definition, and
// the link must resolve the soft-float helpers the Rust side calls into.
extern unsigned gtos_abi_scale(unsigned value, float weight);

static unsigned output;

void _start(void) {
    output = gtos_abi_scale(21u, 1.5f);
}
