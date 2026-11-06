use core::ffi::c_void;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

static FAIL_MALLOC: AtomicBool = AtomicBool::new(false);
static FAIL_CALLOC: AtomicBool = AtomicBool::new(false);
static FAIL_REALLOC: AtomicBool = AtomicBool::new(false);
static FAIL_ALIGNED: AtomicBool = AtomicBool::new(false);
static FREES: AtomicUsize = AtomicUsize::new(0);

unsafe extern "C" {
    #[link_name = "malloc"] fn real_malloc(bytes: usize) -> *mut c_void;
    #[link_name = "calloc"] fn real_calloc(count: usize, bytes: usize) -> *mut c_void;
    #[link_name = "realloc"] fn real_realloc(pointer: *mut c_void, bytes: usize) -> *mut c_void;
    #[link_name = "free"] fn real_free(pointer: *mut c_void);
    #[link_name = "posix_memalign"]
    fn real_posix_memalign(result: *mut *mut c_void, alignment: usize, bytes: usize) -> i32;
}

#[unsafe(no_mangle)]
unsafe extern "C" fn gtos_test_malloc(bytes: usize) -> *mut c_void {
    if FAIL_MALLOC.swap(false, Ordering::SeqCst) { return std::ptr::null_mut(); }
    unsafe { real_malloc(bytes) }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn gtos_test_calloc(count: usize, bytes: usize) -> *mut c_void {
    if FAIL_CALLOC.swap(false, Ordering::SeqCst) { return std::ptr::null_mut(); }
    unsafe { real_calloc(count, bytes) }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn gtos_test_realloc(pointer: *mut c_void, bytes: usize) -> *mut c_void {
    if FAIL_REALLOC.swap(false, Ordering::SeqCst) { return std::ptr::null_mut(); }
    unsafe { real_realloc(pointer, bytes) }
}
#[unsafe(no_mangle)]
unsafe extern "C" fn gtos_test_free(pointer: *mut c_void) {
    FREES.fetch_add(1, Ordering::SeqCst);
    unsafe { real_free(pointer) };
}
#[unsafe(no_mangle)]
unsafe extern "C" fn gtos_test_posix_memalign(result: *mut *mut c_void, alignment: usize, bytes: usize) -> i32 {
    if FAIL_ALIGNED.swap(false, Ordering::SeqCst) {
        // Fault injection: even a bad provider's non-null error output must
        // never be accepted or zeroed. Ordinary calls below use real libc.
        unsafe { *result = 1usize as *mut c_void };
        return 12;
    }
    unsafe { real_posix_memalign(result, alignment, bytes) }
}

use std::alloc::Layout;

#[test]
fn small_allocations_keep_requested_alignment() {
    for size in [1, 3, 8, 31, 257] {
        for align in [1, 2, 4, 8] {
            let layout = Layout::from_size_align(size, align).unwrap();
            unsafe {
                let p = actual::alloc(layout);
                assert!(!p.is_null());
                assert_eq!(p as usize % align, 0);
                p.write_bytes(0xa5, size);
                assert_eq!(*p.add(size - 1), 0xa5);
                actual::dealloc(p, layout);
            }
        }
    }
}

#[test]
fn zeroing_covers_normal_and_overaligned_allocations() {
    for size in [1, 17, 4103] {
        for align in [4, 16, 64, 256, 4096] {
            let layout = Layout::from_size_align(size, align).unwrap();
            unsafe {
                let p = actual::alloc_zeroed(layout);
                assert!(!p.is_null());
                assert_eq!(p as usize % align, 0);
                assert!(std::slice::from_raw_parts(p, size).iter().all(|&v| v == 0));
                actual::dealloc(p, layout);
            }
        }
    }
}

#[test]
fn malloc_and_calloc_failure_then_recovery() {
    let layout = Layout::from_size_align(64, 4).unwrap();
    unsafe {
        FAIL_MALLOC.store(true, Ordering::SeqCst);
        assert!(actual::alloc(layout).is_null());
        FAIL_CALLOC.store(true, Ordering::SeqCst);
        assert!(actual::alloc_zeroed(layout).is_null());
        let p = actual::alloc_zeroed(layout);
        assert!(!p.is_null());
        actual::dealloc(p, layout);
    }
}

#[test]
fn aligned_failure_never_becomes_a_pointer() {
    let layout = Layout::from_size_align(17, 256).unwrap();
    unsafe {
        FAIL_ALIGNED.store(true, Ordering::SeqCst);
        assert!(actual::alloc(layout).is_null());
        FAIL_ALIGNED.store(true, Ordering::SeqCst);
        assert!(actual::alloc_zeroed(layout).is_null());
        let p = actual::alloc(layout);
        assert!(!p.is_null());
        actual::dealloc(p, layout);
    }
}

#[test]
fn realloc_failure_preserves_the_old_allocation() {
    let layout = Layout::from_size_align(32, 4).unwrap();
    unsafe {
        let p = actual::alloc(layout);
        assert!(!p.is_null());
        for i in 0..32 { p.add(i).write(i as u8); }
        let before = FREES.load(Ordering::SeqCst);
        FAIL_REALLOC.store(true, Ordering::SeqCst);
        assert!(actual::realloc(p, layout, 128).is_null());
        assert_eq!(FREES.load(Ordering::SeqCst), before);
        for i in 0..32 { assert_eq!(*p.add(i), i as u8); }
        let q = actual::realloc(p, layout, 128);
        assert!(!q.is_null());
        for i in 0..32 { assert_eq!(*q.add(i), i as u8); }
        actual::dealloc(q, Layout::from_size_align(128, 4).unwrap());
    }
}

#[test]
fn overaligned_realloc_preserves_data_alignment_and_ownership() {
    let layout = Layout::from_size_align(24, 256).unwrap();
    unsafe {
        let p = actual::alloc(layout);
        assert!(!p.is_null());
        p.write_bytes(0x39, 24);
        let before = FREES.load(Ordering::SeqCst);
        FAIL_ALIGNED.store(true, Ordering::SeqCst);
        assert!(actual::realloc(p, layout, 128).is_null());
        assert_eq!(FREES.load(Ordering::SeqCst), before);
        assert!((0..24).all(|i| *p.add(i) == 0x39));
        let q = actual::realloc(p, layout, 128);
        assert!(!q.is_null());
        assert_eq!(q as usize % 256, 0);
        assert_eq!(FREES.load(Ordering::SeqCst), before + 1);
        assert!((0..24).all(|i| *q.add(i) == 0x39));
        let r = actual::realloc(q, Layout::from_size_align(128, 256).unwrap(), 13);
        assert!(!r.is_null());
        assert_eq!(r as usize % 256, 0);
        assert!((0..13).all(|i| *r.add(i) == 0x39));
        assert_eq!(FREES.load(Ordering::SeqCst), before + 2);
        actual::dealloc(r, Layout::from_size_align(13, 256).unwrap());
        assert_eq!(FREES.load(Ordering::SeqCst), before + 3);
    }
}

#[test]
fn shrinking_below_alignment_uses_the_aligned_fallback() {
    let layout = Layout::from_size_align(32, 8).unwrap();
    unsafe {
        let p = actual::alloc(layout);
        assert!(!p.is_null());
        p.write_bytes(0x51, 32);
        let q = actual::realloc(p, layout, 4);
        assert!(!q.is_null());
        assert_eq!(q as usize % 8, 0);
        assert!((0..4).all(|i| *q.add(i) == 0x51));
        actual::dealloc(q, Layout::from_size_align(4, 8).unwrap());
    }
}
