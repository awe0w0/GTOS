// A real cross-boundary consumer for the private GTOS x86 target: C passes a 32-bit integer and a float by the GTOS calling
// convention, Rust does arithmetic and hands the result back. It uses no allocator and no panic payload, so it compiles without
// the standard library, and it names exactly what it needs from `core` so the compiler has to find that crate for this target.
#![no_std]
#![no_main]

#[unsafe(no_mangle)]
pub extern "C" fn gtos_rust_boundary(value: u32, weight: f32) -> u32 {
    let scaled = (weight * 2.0) as u32;
    value.wrapping_mul(scaled).wrapping_add(7)
}

#[unsafe(no_mangle)]
pub extern "C" fn gtos_rust_read_static() -> u32 {
    TABLE[1]
}

static TABLE: [u32; 4] = [3, 5, 7, 11];

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
