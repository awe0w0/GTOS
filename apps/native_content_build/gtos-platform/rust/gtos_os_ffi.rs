//! Byte-based native path strings on GTOS.

#[path = "unix/ffi/os_str.rs"]
mod os_str;

#[unstable(feature = "gtos_ext", issue = "none")]
pub use self::os_str::{OsStrExt, OsStringExt};
