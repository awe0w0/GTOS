#![no_std]
#![no_main]

unsafe extern "C" {
    fn GtosNativeAdd(value: u32) -> u32;
    static gtos_native_value: u32;
    static gtos_native_divisor: u64;
}

#[unsafe(no_mangle)]
pub extern "C" fn GtosRustEntry(value: u32) -> u32 {
    let bias = unsafe { core::ptr::read_volatile(&raw const gtos_native_value) };
    let divisor = unsafe { core::ptr::read_volatile(&raw const gtos_native_divisor) };
    let wide = ((value as u64) << 32) | bias as u64;
    let quotient = wide / (divisor | 1);
    unsafe { GtosNativeAdd(quotient as u32) }
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo<'_>) -> ! {
    unsafe { core::arch::asm!("ud2", options(noreturn)); }
}
