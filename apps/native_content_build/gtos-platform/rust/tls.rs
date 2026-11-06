// Key-based TLS over the native compiler-rt emutls/C++ exit ABI. Its C heap
// does not call Rust TLS, so creating a key cannot recurse into this module.
use crate::ffi::c_void;
use crate::mem;
use crate::ptr;
use crate::sync::atomic::{AtomicUsize, Ordering};

pub type Key = usize;
type Destructor = unsafe extern "C" fn(*mut u8);

#[repr(C)]
struct Control {
    size: usize,
    alignment: usize,
    index: usize,
    initial: *const u8,
    destructor: Option<Destructor>,
}

#[repr(C)]
struct Value {
    pointer: *mut u8,
    destructor: Option<Destructor>,
    registered: bool,
}

unsafe extern "C" {
    #[cfg_attr(all(test, not(target_os = "gtos")), link_name = "gtos_test_tls_malloc")]
    fn malloc(bytes: usize) -> *mut c_void;
    #[cfg_attr(all(test, not(target_os = "gtos")), link_name = "gtos_test_tls_free")]
    fn free(pointer: *mut c_void);
    fn __emutls_get_address(control: *mut Control) -> *mut Value;
    #[cfg_attr(all(test, not(target_os = "gtos")), link_name = "gtos_test_tls_atexit")]
    fn __cxa_thread_atexit(destroy: unsafe extern "C" fn(*mut u8), object: *mut u8,
                          dso: *mut c_void) -> i32;
    static mut __dso_handle: *mut c_void;
}

pub struct LazyKey {
    key: AtomicUsize,
    destructor: Option<Destructor>,
}

impl LazyKey {
    pub const fn new(destructor: Option<Destructor>) -> Self {
        Self { key: AtomicUsize::new(0), destructor }
    }

    pub fn force(&self) -> Key {
        let key = self.key.load(Ordering::Acquire);
        if key != 0 {
            return key;
        }
        let candidate = create(self.destructor);
        match self.key.compare_exchange(0, candidate, Ordering::Release, Ordering::Acquire) {
            Ok(_) => candidate,
            Err(winner) => {
                unsafe { destroy(candidate) };
                winner
            }
        }
    }
}

pub fn create(destructor: Option<Destructor>) -> Key {
    let control = unsafe { malloc(mem::size_of::<Control>()).cast::<Control>() };
    if control.is_null() {
        rtabort!("GTOS TLS key allocation failed");
    }
    unsafe {
        control.write(Control { size: mem::size_of::<Value>(), alignment: mem::align_of::<Value>(),
                                index: 0, initial: ptr::null(), destructor });
    }
    control as Key
}

pub unsafe fn destroy(key: Key) {
    // As with pthread_key_delete, users must stop accessing this key first;
    // deleting a key does not run its values' destructors. Native emutls owns
    // the allocated per-thread cells and releases them at thread exit.
    unsafe { free(key as *mut c_void) };
}

pub unsafe fn get(key: Key) -> *mut u8 {
    unsafe { (*__emutls_get_address(key as *mut Control)).pointer }
}

pub unsafe fn set(key: Key, pointer: *mut u8) {
    unsafe {
        let control = key as *mut Control;
        let value = &mut *__emutls_get_address(control);
        value.pointer = pointer;
        value.destructor = (*control).destructor;
        if !pointer.is_null() && value.destructor.is_some() && !value.registered {
            value.registered = true;
            if __cxa_thread_atexit(run, value as *mut Value as *mut u8, __dso_handle) != 0 {
                rtabort!("GTOS TLS destructor registration failed");
            }
        }
    }
}

unsafe extern "C" fn run(object: *mut u8) {
    unsafe {
        let value = &mut *object.cast::<Value>();
        let pointer = mem::replace(&mut value.pointer, ptr::null_mut());
        let destructor = value.destructor;
        value.registered = false;
        // End the mutable borrow before user code, which may set this same key
        // and register the next round, just like std's own cleanup guard does.
        if !pointer.is_null() {
            if let Some(destroy) = destructor {
                destroy(pointer);
            }
        }
    }
}
