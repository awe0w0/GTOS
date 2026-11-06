#![no_std]
#![no_main]

#[repr(C)]
pub struct Pair { pub small: f32, pub wide: f64, pub tag: u32 }
const _: () = assert!(core::mem::size_of::<Pair>() == 16 && core::mem::offset_of!(Pair,wide) == 4);

unsafe extern "C" {
    fn c_mix64(a: f64, b: f64) -> f64;
    fn c_identity64(value: f64) -> f64;
    fn c_pair(value: Pair) -> Pair;
}
#[unsafe(no_mangle)]
pub extern "C" fn rust_add32(a: f32, b: f32) -> f32 { a + b }
#[unsafe(no_mangle)]
pub extern "C" fn rust_mix64(a: f64, b: f64) -> f64 { a * 2.0 + b }
#[unsafe(no_mangle)]
pub extern "C" fn rust_identity32(value: f32) -> f32 { value }
#[unsafe(no_mangle)]
pub extern "C" fn rust_identity64(value: f64) -> f64 { value }
#[unsafe(no_mangle)]
pub extern "C" fn rust_call_c64(a: f64, b: f64) -> f64 { unsafe { c_mix64(a,b) } }
#[unsafe(no_mangle)]
pub extern "C" fn rust_roundtrip64(value: f64) -> f64 { unsafe { c_identity64(value) } }
#[unsafe(no_mangle)]
pub extern "C" fn rust_pair(value: Pair) -> Pair {
    Pair { small: value.small + 1.0, wide: value.wide * 2.0, tag: value.tag ^ 0x5a5a }
}
#[unsafe(no_mangle)]
pub extern "C" fn rust_call_c_pair(value: Pair) -> Pair { unsafe { c_pair(value) } }

#[repr(C)]
pub struct Table {
    add32: extern "C" fn(f32,f32) -> f32,
    mix64: extern "C" fn(f64,f64) -> f64,
}
static TABLE: Table = Table { add32: rust_add32, mix64: rust_mix64 };
#[unsafe(no_mangle)]
pub extern "C" fn rust_float_table() -> *const Table { &TABLE }

#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    unsafe { core::arch::asm!("ud2",options(noreturn)) }
}
