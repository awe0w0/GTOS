// CPL3 hardware entropy, matching the native V8 runtime's RDRAND contract.
// No timestamp, process-id or deterministic fallback is accepted.
#[cfg(test)]
use crate::hardware;

#[cfg(not(test))]
mod hardware {
    #[cfg(target_arch = "x86")]
    use core::arch::x86::{__cpuid, _rdrand32_step};
    #[cfg(target_arch = "x86_64")]
    use core::arch::x86_64::{__cpuid, _rdrand32_step};

    pub fn available() -> bool {
        __cpuid(0).eax >= 1 && __cpuid(1).ecx & (1 << 30) != 0
    }

    #[target_feature(enable = "rdrand")]
    pub unsafe fn word() -> Option<u32> {
        let mut value = 0;
        if _rdrand32_step(&mut value) == 1 { Some(value) } else { None }
    }
}

pub fn fill_bytes(bytes: &mut [u8]) {
    if bytes.is_empty() {
        return;
    }
    assert!(hardware::available(), "GTOS hardware entropy is unavailable");
    for chunk in bytes.chunks_mut(4) {
        let mut value = None;
        for _ in 0..10 {
            value = unsafe { hardware::word() };
            if value.is_some() {
                break;
            }
        }
        // A failed request is unusable, including a prefix already filled.
        let word = value.expect("GTOS hardware entropy read failed").to_ne_bytes();
        chunk.copy_from_slice(&word[..chunk.len()]);
    }
}
