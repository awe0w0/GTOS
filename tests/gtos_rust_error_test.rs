#![feature(io_error_more, io_error_uncategorized, io_error_input_output_error, io_error_too_many_open_files)]
pub use std::{fmt, io};

#[path = "../apps/native_content_build/gtos-platform/rust/error.rs"]
mod actual;

std::thread_local! {
    static ERRNO: std::cell::Cell<i32> = const { std::cell::Cell::new(0) };
}

#[unsafe(no_mangle)]
extern "C" fn __llvm_libc_errno() -> *mut i32 {
    ERRNO.with(std::cell::Cell::as_ptr)
}

struct ErrorText(i32);
impl fmt::Display for ErrorText {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        actual::format_error(self.0, f)
    }
}

#[test]
fn sdk_and_file_errors_decode_without_claiming_unimplemented_services() {
    use io::ErrorKind::*;
    for (code, expected) in [
        (1,PermissionDenied),(2,NotFound),(4,Interrupted),(5,InputOutputError),
        (7,ArgumentListTooLong),(11,WouldBlock),(12,OutOfMemory),(13,PermissionDenied),
        (16,ResourceBusy),(17,AlreadyExists),(18,CrossesDevices),(20,NotADirectory),
        (21,IsADirectory),(22,InvalidInput),(23,TooManyOpenFiles),(24,TooManyOpenFiles),
        (26,ExecutableFileBusy),(27,FileTooLarge),(28,StorageFull),(29,NotSeekable),
        (30,ReadOnlyFilesystem),(31,TooManyLinks),(32,BrokenPipe),(35,Deadlock),
        (36,InvalidFilename),(38,Unsupported),(39,DirectoryNotEmpty),(40,FilesystemLoop),
        (95,Unsupported),
    ] {
        assert_eq!(actual::decode_error_kind(code), expected, "error {code}");
        assert!(!format!("{}",ErrorText(code)).contains("Success"));
    }
    for code in [0,9,14,19,75,-2,i32::MIN,i32::MAX,123456] {
        assert_eq!(actual::decode_error_kind(code), Uncategorized, "error {code}");
    }
}

#[test]
fn interrupted_is_exact_and_unknown_messages_retain_the_code() {
    for code in [i32::MIN,-4,0,3,4,5,i32::MAX] {
        assert_eq!(actual::is_interrupted(code),code == 4);
    }
    assert_eq!(format!("{}",ErrorText(0)),"Success");
    assert_eq!(format!("{}",ErrorText(9)),"Bad file descriptor");
    assert_eq!(format!("{}",ErrorText(19)),"No device available");
    assert_eq!(format!("{}",ErrorText(-2)),"Unknown GTOS error -2");
    assert_eq!(format!("{}",ErrorText(i32::MAX)),"Unknown GTOS error 2147483647");
}

#[test]
fn errno_reads_the_runtime_slot_on_each_call_and_each_thread() {
    ERRNO.with(|value| value.set(5));
    assert_eq!(actual::errno(),5);
    std::thread::spawn(|| {
        assert_eq!(actual::errno(),0);
        ERRNO.with(|value| value.set(12));
        assert_eq!(actual::errno(),12);
    }).join().unwrap();
    assert_eq!(actual::errno(),5);
    ERRNO.with(|value| value.set(22));
    assert_eq!(actual::errno(),22);
}
