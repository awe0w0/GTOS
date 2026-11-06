pub use std::{ffi, mem, ptr, sync};
macro_rules! rtabort {
    ($($message:tt)*) => { panic!($($message)*) };
}

#[path = "../apps/native_content_build/gtos-platform/rust/tls.rs"]
mod actual;
use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

static FAIL_ALLOCATE: AtomicBool = AtomicBool::new(false);
static FAIL_REGISTER: AtomicBool = AtomicBool::new(false);
static ALLOCATED: AtomicUsize = AtomicUsize::new(0);
static FREED: AtomicUsize = AtomicUsize::new(0);
unsafe extern "C" {
    #[link_name = "malloc"] fn real_malloc(bytes: usize) -> *mut ffi::c_void;
    #[link_name = "free"] fn real_free(pointer: *mut ffi::c_void);
    #[link_name = "__cxa_thread_atexit"]
    fn real_atexit(destroy: unsafe extern "C" fn(*mut u8), object: *mut u8, dso: *mut ffi::c_void) -> i32;
}
#[unsafe(no_mangle)]
unsafe extern "C" fn gtos_test_tls_malloc(bytes: usize) -> *mut ffi::c_void {
    if FAIL_ALLOCATE.swap(false,Ordering::SeqCst) { return ptr::null_mut(); }
    let pointer = unsafe { real_malloc(bytes) };
    if !pointer.is_null() { ALLOCATED.fetch_add(1,Ordering::SeqCst); }
    pointer
}
#[unsafe(no_mangle)]
unsafe extern "C" fn gtos_test_tls_free(pointer: *mut ffi::c_void) {
    FREED.fetch_add(1,Ordering::SeqCst);
    unsafe { real_free(pointer) };
}
#[unsafe(no_mangle)]
unsafe extern "C" fn gtos_test_tls_atexit(destroy: unsafe extern "C" fn(*mut u8), object: *mut u8,
                                        dso: *mut ffi::c_void) -> i32 {
    if FAIL_REGISTER.swap(false,Ordering::SeqCst) { return -1; }
    unsafe { real_atexit(destroy,object,dso) }
}

static DESTROYED: AtomicUsize = AtomicUsize::new(0);
unsafe extern "C" fn drop_box(pointer: *mut u8) {
    unsafe { drop(Box::from_raw(pointer.cast::<usize>())) };
    DESTROYED.fetch_add(1, Ordering::SeqCst);
}

#[test]
fn empty_set_replace_and_clear_are_thread_local() {
    let key = actual::create(None);
    unsafe {
        assert!(actual::get(key).is_null());
        actual::set(key, ptr::without_provenance_mut(17));
        assert_eq!(actual::get(key) as usize,17);
    }
    std::thread::spawn(move || unsafe {
        assert!(actual::get(key).is_null());
        actual::set(key,ptr::without_provenance_mut(29));
        assert_eq!(actual::get(key) as usize,29);
        actual::set(key,ptr::null_mut());
        assert!(actual::get(key).is_null());
    }).join().unwrap();
    unsafe {
        assert_eq!(actual::get(key) as usize,17);
        actual::set(key,ptr::null_mut());
        actual::destroy(key);
    }
}

#[test]
fn concurrent_lazy_initialization_publishes_one_key() {
    let before_allocated = ALLOCATED.load(Ordering::SeqCst);
    let before_freed = FREED.load(Ordering::SeqCst);
    let key = std::sync::Arc::new(actual::LazyKey::new(None));
    let barrier = std::sync::Arc::new(std::sync::Barrier::new(8));
    let workers: Vec<_> = (0..8).map(|index| {
        let (key,barrier) = (key.clone(),barrier.clone());
        std::thread::spawn(move || {
            barrier.wait();
            let handle = key.force();
            unsafe {
                assert!(actual::get(handle).is_null());
                actual::set(handle,ptr::without_provenance_mut(index + 1));
                std::thread::yield_now();
                assert_eq!(actual::get(handle) as usize,index + 1);
            }
            handle
        })
    }).collect();
    let expected = key.force();
    for worker in workers { assert_eq!(worker.join().unwrap(),expected); }
    unsafe { actual::destroy(expected) };
    assert_eq!(ALLOCATED.load(Ordering::SeqCst)-before_allocated,
               FREED.load(Ordering::SeqCst)-before_freed);
}

#[test]
fn native_exit_runs_one_destructor_for_the_final_non_null_value() {
    let key = actual::create(Some(drop_box));
    let before = DESTROYED.load(Ordering::SeqCst);
    for _ in 0..12 {
        std::thread::spawn(move || unsafe {
            actual::set(key,Box::into_raw(Box::new(42usize)).cast());
        }).join().unwrap();
    }
    assert_eq!(DESTROYED.load(Ordering::SeqCst),before + 12);
    std::thread::spawn(move || unsafe {
        let object = Box::into_raw(Box::new(3usize));
        actual::set(key,object.cast());
        actual::set(key,ptr::null_mut());
        drop(Box::from_raw(object));
    }).join().unwrap();
    assert_eq!(DESTROYED.load(Ordering::SeqCst),before + 12);
    unsafe { actual::destroy(key) };
}

static REENTER_KEY: AtomicUsize = AtomicUsize::new(0);
static ROUNDS: AtomicUsize = AtomicUsize::new(0);
unsafe extern "C" fn reenter(_: *mut u8) {
    let round = ROUNDS.fetch_add(1,Ordering::SeqCst);
    if round < 2 {
        unsafe { actual::set(REENTER_KEY.load(Ordering::SeqCst),ptr::without_provenance_mut(1)) };
    }
}

#[test]
fn destructor_may_reinitialize_the_same_key() {
    let key = actual::create(Some(reenter));
    ROUNDS.store(0,Ordering::SeqCst);
    REENTER_KEY.store(key,Ordering::SeqCst);
    std::thread::spawn(move || unsafe { actual::set(key,ptr::without_provenance_mut(1)) })
        .join().unwrap();
    assert_eq!(ROUNDS.load(Ordering::SeqCst),3);
    unsafe { actual::destroy(key) };
}

#[test]
fn allocation_failure_does_not_publish_a_key_and_can_be_retried() {
    let key = actual::LazyKey::new(None);
    FAIL_ALLOCATE.store(true,Ordering::SeqCst);
    assert!(std::panic::catch_unwind(|| key.force()).is_err());
    let valid = key.force();
    assert_ne!(valid,0);
    assert_eq!(key.force(),valid);
    unsafe { actual::destroy(valid) };
}

#[test]
fn failed_destructor_registration_is_fatal_instead_of_silent_leak_success() {
    let key = actual::create(Some(drop_box));
    let before = DESTROYED.load(Ordering::SeqCst);
    FAIL_REGISTER.store(true,Ordering::SeqCst);
    let failed = std::thread::spawn(move || unsafe {
        // The test's rtabort seam panics; production aborts this process. Do
        // not resume using the failed key, or pretend the destructor was set.
        actual::set(key,ptr::without_provenance_mut(1));
    }).join();
    assert!(failed.is_err());
    assert_eq!(DESTROYED.load(Ordering::SeqCst),before);
    unsafe { actual::destroy(key) };
}
