// The Rust half of the cross-boundary consumer: four exported functions that a C caller compiled for i686 must be able to call
// with nothing but a declaration. Integer arguments, a pointer to a C-layout struct, a soft-float argument and a pointer out.
//
// The layout assertions are the point of the struct case: the same numbers are asserted on the C side, so a mismatch is a compile
// error on one side rather than a wrong read at run time.
#![no_std]
#![no_main]

#[repr(C)]
pub struct Pair {
    pub low: u32,
    pub high: u16,
    pub tag: u8,
}

const _: () = assert!(core::mem::size_of::<Pair>() == 8);
const _: () = assert!(core::mem::offset_of!(Pair, low) == 0);
const _: () = assert!(core::mem::offset_of!(Pair, high) == 4);
const _: () = assert!(core::mem::offset_of!(Pair, tag) == 6);

#[unsafe(no_mangle)]
pub extern "C" fn gtos_abi_add(a: u32, b: u32) -> u32 {
    a.wrapping_add(b)
}

#[unsafe(no_mangle)]
pub extern "C" fn gtos_abi_pair(p: *const Pair) -> u32 {
    // A null pointer is a caller bug, but returning a fixed value keeps the function total without pulling in a panic path.
    if p.is_null() {
        return 0;
    }
    let pair = unsafe { &*p };
    pair.low ^ ((pair.high as u32) << 8) ^ (pair.tag as u32)
}

#[unsafe(no_mangle)]
pub extern "C" fn gtos_abi_store(p: *mut u32) {
    if p.is_null() {
        return;
    }
    unsafe { *p = 0x5AA5_1234 };
}

// A by-value struct argument: the caller copies eight bytes onto the stack, so a disagreement about size or layout shows up as a
// wrong value rather than as a link error. This is the case a pointer argument cannot cover.
#[unsafe(no_mangle)]
pub extern "C" fn gtos_abi_pair_value(p: Pair) -> u32 {
    p.low ^ ((p.high as u32) << 8) ^ (p.tag as u32)
}

// A 64-bit return from 32-bit arguments: SysV i386 returns it in edx:eax, so the two sides have to agree on the register pair
// rather than on a memory layout.
#[unsafe(no_mangle)]
pub extern "C" fn gtos_abi_wide(a: u32, b: u32) -> u64 {
    ((a as u64) << 32) | (b as u64)
}

// A table of function pointers, which is how a runtime hands a backend a set of entry points without the backend naming each one.
// The struct is repr(C) and the table is a const, so the C side can declare the same shape and call through it; the two entries are
// the same functions the C side also calls by name, so a wrong offset or a wrong calling convention shows up as a wrong result.
#[repr(C)]
pub struct AbiTable {
    pub add: extern "C" fn(u32, u32) -> u32,
    pub wide: extern "C" fn(u32, u32) -> u64,
}

pub static ABI_TABLE: AbiTable = AbiTable {
    add: gtos_abi_add,
    wide: gtos_abi_wide,
};

const _: () = assert!(core::mem::size_of::<AbiTable>() == 8);
const _: () = assert!(core::mem::offset_of!(AbiTable, add) == 0);
const _: () = assert!(core::mem::offset_of!(AbiTable, wide) == 4);

#[unsafe(no_mangle)]
pub extern "C" fn gtos_abi_table() -> *const AbiTable {
    &ABI_TABLE
}

#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    loop {}
}
