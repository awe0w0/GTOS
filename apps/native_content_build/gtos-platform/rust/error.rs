// The native LLVM libc ABI owns the per-thread errno slot. GTOS is not a Unix
// target; these numbers are the committed SDK/file-service error contract.
use crate::{fmt, io};

unsafe extern "C" {
    fn __llvm_libc_errno() -> *mut i32;
}

#[inline]
pub fn errno() -> i32 {
    unsafe { *__llvm_libc_errno() }
}

#[inline]
pub fn is_interrupted(code: i32) -> bool {
    code == 4
}

fn description(code: i32) -> (io::ErrorKind, &'static str) {
    use io::ErrorKind::*;
    match code {
        1 | 13 => (PermissionDenied, "Permission denied"),
        2 => (NotFound, "No such file or directory"),
        4 => (Interrupted, "Interrupted operation"),
        5 => (InputOutputError, "Input/output error"),
        7 => (ArgumentListTooLong, "Argument list too long"),
        11 => (WouldBlock, "Operation would block"),
        12 => (OutOfMemory, "Out of memory"),
        16 => (ResourceBusy, "Resource busy"),
        17 => (AlreadyExists, "File exists"),
        18 => (CrossesDevices, "Cross-device operation"),
        20 => (NotADirectory, "Not a directory"),
        21 => (IsADirectory, "Is a directory"),
        22 => (InvalidInput, "Invalid argument"),
        23 | 24 => (TooManyOpenFiles, "Too many open files"),
        26 => (ExecutableFileBusy, "Executable file busy"),
        27 => (FileTooLarge, "File too large"),
        28 => (StorageFull, "No space left on device"),
        29 => (NotSeekable, "Illegal seek"),
        30 => (ReadOnlyFilesystem, "Read-only filesystem"),
        31 => (TooManyLinks, "Too many links"),
        32 => (BrokenPipe, "Broken pipe"),
        35 => (Deadlock, "Deadlock would occur"),
        36 => (InvalidFilename, "File name too long"),
        38 | 95 => (Unsupported, "Operation not supported"),
        39 => (DirectoryNotEmpty, "Directory not empty"),
        40 => (FilesystemLoop, "Too many symbolic links"),
        0 => (Uncategorized, "Success"),
        9 => (Uncategorized, "Bad file descriptor"),
        14 => (Uncategorized, "Bad address"),
        19 => (Uncategorized, "No device available"),
        75 => (Uncategorized, "Value too large"),
        _ => (Uncategorized, "Unknown GTOS error"),
    }
}

pub fn decode_error_kind(code: i32) -> io::ErrorKind {
    description(code).0
}

pub fn format_error(code: i32, f: &mut fmt::Formatter<'_>) -> fmt::Result {
    let message = description(code).1;
    if message == "Unknown GTOS error" {
        write!(f, "{message} {code}")
    } else {
        f.write_str(message)
    }
}
