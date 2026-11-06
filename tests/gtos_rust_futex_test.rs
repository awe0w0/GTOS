use std::sync::atomic::{AtomicUsize, Ordering};
use std::time::{Duration, Instant};

pub mod platform {
    use super::*;
    pub static YIELDS: AtomicUsize = AtomicUsize::new(0);
    pub static CLOCK_CALLS: AtomicUsize = AtomicUsize::new(0);
    pub static CLOCK_MODE: AtomicUsize = AtomicUsize::new(0);
    static ORIGIN: std::sync::OnceLock<Instant> = std::sync::OnceLock::new();
    pub fn yield_now() {
        YIELDS.fetch_add(1,Ordering::SeqCst);
        std::thread::yield_now();
    }
    pub fn resolution_us() -> u64 { 1 }
    pub fn now_us() -> u64 {
        let count = CLOCK_CALLS.fetch_add(1,Ordering::SeqCst);
        match CLOCK_MODE.load(Ordering::SeqCst) {
            1 => panic!("injected native clock failure"),
            2 => if count == 0 { 100 } else { 99 },
            _ => ORIGIN.get_or_init(Instant::now).elapsed().as_micros() as u64,
        }
    }
    pub fn reset() {
        YIELDS.store(0,Ordering::SeqCst);
        CLOCK_CALLS.store(0,Ordering::SeqCst);
        CLOCK_MODE.store(0,Ordering::SeqCst);
    }
}

#[test]
fn changed_values_and_zero_timeouts_do_not_need_the_clock() {
    platform::reset();
    platform::CLOCK_MODE.store(1,Ordering::SeqCst);
    let value = actual::Futex::new(3);
    assert!(actual::futex_wait(&value,2,Some(Duration::from_secs(1))));
    assert!(!actual::futex_wait(&value,3,Some(Duration::ZERO)));
    assert_eq!(platform::CLOCK_CALLS.load(Ordering::SeqCst),0);
    assert_eq!(platform::YIELDS.load(Ordering::SeqCst),0);
    platform::reset();
}

#[test]
fn unchanged_values_time_out_without_early_submicrosecond_rounding() {
    platform::reset();
    let value = actual::Futex::new(4);
    for timeout in [Duration::from_nanos(1),Duration::from_millis(3)] {
        let start = Instant::now();
        assert!(!actual::futex_wait(&value,4,Some(timeout)));
        assert!(start.elapsed() >= timeout);
    }
}

fn wait_until_yielding() {
    let limit = Instant::now() + Duration::from_secs(2);
    while platform::YIELDS.load(Ordering::SeqCst) == 0 {
        assert!(Instant::now() < limit,"waiter never yielded");
        std::thread::yield_now();
    }
}

#[test]
fn notifications_wake_even_when_the_value_did_not_change() {
    for all in [false,true] {
        platform::reset();
        let value = std::sync::Arc::new(actual::Futex::new(7));
        let peer = value.clone();
        let worker = std::thread::spawn(move || actual::futex_wait(&peer,7,Some(Duration::from_secs(2))));
        wait_until_yielding();
        if all { actual::futex_wake_all(&value); } else { assert!(!actual::futex_wake(&value)); }
        assert!(worker.join().unwrap());
        assert_eq!(value.load(Ordering::Acquire),7);
    }
}

#[test]
fn a_value_change_without_notification_is_observed() {
    platform::reset();
    let value = std::sync::Arc::new(actual::Futex::new(1));
    let peer = value.clone();
    let worker = std::thread::spawn(move || actual::futex_wait(&peer,1,None));
    wait_until_yielding();
    value.store(2,Ordering::Release);
    assert!(worker.join().unwrap());
}

#[test]
fn invalid_or_backwards_clocks_fail_instead_of_reporting_a_timeout() {
    let value = actual::Futex::new(1);
    for mode in [1,2] {
        platform::reset();
        platform::CLOCK_MODE.store(mode,Ordering::SeqCst);
        assert!(std::panic::catch_unwind(|| actual::futex_wait(&value,1,Some(Duration::from_secs(1)))).is_err());
    }
    platform::reset();
}

struct Protected {
    lock: actual_mutex::Mutex,
    value: std::cell::UnsafeCell<usize>,
}
unsafe impl Sync for Protected {}

#[test]
fn the_real_upstream_mutex_serializes_contended_updates() {
    platform::reset();
    let shared = std::sync::Arc::new(Protected {
        lock: actual_mutex::Mutex::new(), value: std::cell::UnsafeCell::new(0),
    });
    assert!(shared.lock.try_lock());
    assert!(!shared.lock.try_lock());
    unsafe { shared.lock.unlock() };
    let workers: Vec<_> = (0..4).map(|_| {
        let shared = shared.clone();
        std::thread::spawn(move || {
            for _ in 0..1000 {
                shared.lock.lock();
                unsafe { *shared.value.get() += 1 };
                std::thread::yield_now();
                unsafe { shared.lock.unlock() };
            }
        })
    }).collect();
    for worker in workers { worker.join().unwrap(); }
    assert_eq!(unsafe { *shared.value.get() },4000);
}

#[test]
fn the_real_upstream_condvar_relocks_and_observes_notifications() {
    platform::reset();
    let shared = std::sync::Arc::new(Protected {
        lock: actual_mutex::Mutex::new(), value: std::cell::UnsafeCell::new(0),
    });
    let condition = std::sync::Arc::new(actual_condvar::Condvar::new());
    let (peer,peer_condition) = (shared.clone(),condition.clone());
    shared.lock.lock();
    let worker = std::thread::spawn(move || {
        peer.lock.lock();
        unsafe { *peer.value.get() = 1 };
        peer_condition.notify_one();
        unsafe { peer.lock.unlock() };
    });
    while unsafe { *shared.value.get() } == 0 {
        unsafe { condition.wait(&shared.lock) };
    }
    assert!(!shared.lock.try_lock());
    unsafe { shared.lock.unlock() };
    worker.join().unwrap();
    shared.lock.lock();
    assert!(!unsafe { condition.wait_timeout(&shared.lock,Duration::from_millis(1)) });
    assert!(!shared.lock.try_lock());
    condition.notify_all();
    unsafe { shared.lock.unlock() };
}
