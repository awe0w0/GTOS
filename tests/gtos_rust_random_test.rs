#[path = "../apps/native_content_build/gtos-platform/rust/random.rs"]
mod actual;

mod hardware {
    use std::sync::atomic::{AtomicBool, AtomicUsize, Ordering};
    pub static SUPPORTED: AtomicBool = AtomicBool::new(true);
    pub static CALLS: AtomicUsize = AtomicUsize::new(0);
    pub static FAIL_FIRST: AtomicUsize = AtomicUsize::new(0);
    pub static FAIL_FROM: AtomicUsize = AtomicUsize::new(usize::MAX);
    pub static CHECKS: AtomicUsize = AtomicUsize::new(0);
    pub static ZERO: AtomicBool = AtomicBool::new(false);
    pub fn available() -> bool {
        CHECKS.fetch_add(1,Ordering::SeqCst);
        SUPPORTED.load(Ordering::SeqCst)
    }
    pub unsafe fn word() -> Option<u32> {
        let call = CALLS.fetch_add(1,Ordering::SeqCst);
        if call < FAIL_FIRST.load(Ordering::SeqCst) || call >= FAIL_FROM.load(Ordering::SeqCst) {
            None
        } else if ZERO.load(Ordering::SeqCst) {
            Some(0)
        } else {
            Some(0x44332211u32.wrapping_add(call as u32))
        }
    }
    pub fn reset() {
        SUPPORTED.store(true,Ordering::SeqCst);
        CALLS.store(0,Ordering::SeqCst);
        FAIL_FIRST.store(0,Ordering::SeqCst);
        FAIL_FROM.store(usize::MAX,Ordering::SeqCst);
        CHECKS.store(0,Ordering::SeqCst);
        ZERO.store(false,Ordering::SeqCst);
    }
}
use std::sync::atomic::Ordering;

#[test]
fn empty_requests_do_not_require_or_query_entropy() {
    hardware::reset();
    hardware::SUPPORTED.store(false,Ordering::SeqCst);
    actual::fill_bytes(&mut []);
    assert_eq!(hardware::CHECKS.load(Ordering::SeqCst),0);
    assert_eq!(hardware::CALLS.load(Ordering::SeqCst),0);
}

#[test]
fn partial_words_fill_exactly_the_requested_range() {
    for length in 1..=17 {
        hardware::reset();
        let mut bytes = [0xa5;19];
        actual::fill_bytes(&mut bytes[1..=length]);
        assert_eq!(bytes[0],0xa5);
        assert!(bytes[length+1..].iter().all(|&v| v==0xa5));
        for index in 0..length {
            let expected = 0x44332211u32.wrapping_add((index/4) as u32).to_ne_bytes()[index%4];
            assert_eq!(bytes[index+1],expected);
        }
        assert_eq!(hardware::CALLS.load(Ordering::SeqCst),(length+3)/4);
    }
}

#[test]
fn unavailable_hardware_fails_without_any_instruction_or_buffer_write() {
    hardware::reset();
    hardware::SUPPORTED.store(false,Ordering::SeqCst);
    let mut bytes = [0xa5;8];
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| actual::fill_bytes(&mut bytes)));
    assert!(result.is_err());
    assert_eq!(bytes,[0xa5;8]);
    assert_eq!(hardware::CALLS.load(Ordering::SeqCst),0);
}

#[test]
fn retry_budget_is_ten_and_a_zero_word_is_valid() {
    hardware::reset();
    hardware::FAIL_FIRST.store(9,Ordering::SeqCst);
    hardware::ZERO.store(true,Ordering::SeqCst);
    let mut bytes = [0xa5;4];
    actual::fill_bytes(&mut bytes);
    assert_eq!(bytes,[0;4]);
    assert_eq!(hardware::CALLS.load(Ordering::SeqCst),10);
    hardware::reset();
    hardware::FAIL_FIRST.store(10,Ordering::SeqCst);
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| actual::fill_bytes(&mut bytes)));
    assert!(result.is_err());
    assert_eq!(hardware::CALLS.load(Ordering::SeqCst),10);
}

#[test]
fn a_partial_request_failure_is_reported_and_the_next_call_can_recover() {
    hardware::reset();
    hardware::FAIL_FROM.store(1,Ordering::SeqCst);
    let mut bytes = [0xa5;8];
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| actual::fill_bytes(&mut bytes)));
    assert!(result.is_err());
    assert_eq!(&bytes[..4],&0x44332211u32.to_ne_bytes());
    assert_eq!(&bytes[4..],&[0xa5;4]);
    assert_eq!(hardware::CALLS.load(Ordering::SeqCst),11);
    hardware::reset();
    actual::fill_bytes(&mut bytes);
    assert_eq!(&bytes[4..],&0x44332212u32.to_ne_bytes());
}
