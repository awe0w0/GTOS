// The float half of the cross-boundary consumer, built and linked on its own.
//
// It is deliberately separate from the integer exports: the target spec asks for soft float, so this function compiles to calls
// into compiler_builtins (`__mulsf3`, `__addsf3`, ...) rather than to x87 instructions. Those helpers are exported from the built
// compiler_builtins under their Rust-mangled names, and a C caller needs the unmangled names, so the link is expected to fail here
// until the runtime helpers are built with their C aliases. The test asserts exactly which symbols are missing instead of
// pretending the float boundary works.
#![no_std]
#![no_main]

#[unsafe(no_mangle)]
pub extern "C" fn gtos_abi_scale(value: u32, weight: f32) -> u32 {
    let scaled = (weight * 2.0) as u32;
    value.wrapping_mul(scaled).wrapping_add(7)
}

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
