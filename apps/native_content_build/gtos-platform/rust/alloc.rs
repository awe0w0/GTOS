// GTOS's C allocation ABI is supplied by the native heap runtime. This module
// does not provide a heap, change its quota, or assume a POSIX target family.
use super::{MIN_ALIGN, realloc_fallback};
use crate::alloc::Layout;
use crate::ffi::c_void;
use crate::ptr;

unsafe extern "C" {
    // Host unit tests redirect only these imports, keeping the allocator body
    // intact and the host test harness's own allocator outside the test hooks.
    #[cfg_attr(all(test, not(target_os = "gtos")), link_name = "gtos_test_malloc")]
    fn malloc(bytes: usize) -> *mut c_void;
    #[cfg_attr(all(test, not(target_os = "gtos")), link_name = "gtos_test_calloc")]
    fn calloc(count: usize, bytes: usize) -> *mut c_void;
    #[cfg_attr(all(test, not(target_os = "gtos")), link_name = "gtos_test_realloc")]
    #[cfg_attr(not(all(test, not(target_os = "gtos"))), link_name = "realloc")]
    fn c_realloc(pointer: *mut c_void, bytes: usize) -> *mut c_void;
    #[cfg_attr(all(test, not(target_os = "gtos")), link_name = "gtos_test_free")]
    fn free(pointer: *mut c_void);
    #[cfg_attr(all(test, not(target_os = "gtos")), link_name = "gtos_test_posix_memalign")]
    fn posix_memalign(result: *mut *mut c_void, alignment: usize, bytes: usize) -> i32;
}

#[inline]
pub unsafe fn alloc(layout: Layout) -> *mut u8 {
    if layout.align() <= MIN_ALIGN && layout.align() <= layout.size() {
        unsafe { malloc(layout.size()).cast() }
    } else {
        let mut result = ptr::null_mut();
        let alignment = layout.align().max(core::mem::size_of::<usize>());
        let status = unsafe { posix_memalign(&mut result, alignment, layout.size()) };
        if status == 0 { result.cast() } else { ptr::null_mut() }
    }
}

#[inline]
pub unsafe fn alloc_zeroed(layout: Layout) -> *mut u8 {
    if layout.align() <= MIN_ALIGN && layout.align() <= layout.size() {
        unsafe { calloc(1, layout.size()).cast() }
    } else {
        let result = unsafe { alloc(layout) };
        if !result.is_null() {
            unsafe { ptr::write_bytes(result, 0, layout.size()) };
        }
        result
    }
}

#[inline]
pub unsafe fn dealloc(pointer: *mut u8, _layout: Layout) {
    unsafe { free(pointer.cast()) };
}

#[inline]
pub unsafe fn realloc(pointer: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
    if layout.align() <= MIN_ALIGN && layout.align() <= new_size {
        unsafe { c_realloc(pointer.cast(), new_size).cast() }
    } else {
        unsafe { realloc_fallback(pointer, layout, new_size) }
    }
}
