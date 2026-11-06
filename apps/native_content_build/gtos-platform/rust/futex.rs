// Cooperative waiting for GTOS's bounded BSP native contexts. This is not a
// kernel futex queue: waiters yield and notifications may wake extra waiters.
// std's futex callers permit spurious wakes and recheck their own atomic state.
use crate::sync::atomic::{AtomicU32, Ordering};
use crate::time::Duration;

pub type Primitive = u32;
pub type SmallPrimitive = u32;
pub type Futex = AtomicU32;
pub type SmallFutex = AtomicU32;

static NOTIFICATIONS: AtomicU32 = AtomicU32::new(0);

#[cfg(test)]
use crate::platform;

#[cfg(not(test))]
mod platform {
    use core::arch::asm;
    use core::mem::size_of;

    #[repr(C)]
    struct Request { version: u32, clock: u32, flags: u32, result: u32, bytes: u32 }
    #[repr(C)]
    struct Result {
        version: u32, clock: u32, unit: u32, source: u32, capabilities: u32, resolution: u32,
        microseconds: u64, delivered_ticks: u64, pit_input: u32, pit_divisor: u32,
    }
    const _: () = assert!(size_of::<usize>() == 4 && size_of::<Request>() == 20 && size_of::<Result>() == 48);

    fn call(mut operation: u32, request: u32, bytes: u32) -> i32 {
        unsafe {
            // LLVM reserves EBX on IA32; save/restore it explicitly. EDI holds
            // the request and all other registers survive the native ABI.
            asm!("push ebx", "mov ebx, edi", "int 0x80", "pop ebx",
                 inout("eax") operation, in("edi") request, in("ecx") bytes);
        }
        operation as i32
    }

    pub fn yield_now() {
        assert_eq!(call(0x4703,0,0),0,"GTOS thread yield failed");
    }

    pub fn resolution_us() -> u64 {
        10000
    }

    pub fn now_us() -> u64 {
        let mut result = Result { version: 0, clock: 0, unit: 0, source: 0, capabilities: 0,
            resolution: 0, microseconds: 0, delivered_ticks: 0, pit_input: 0, pit_divisor: 0 };
        let request = Request { version: 1, clock: 1, flags: 0,
            result: (&mut result as *mut Result) as u32, bytes: size_of::<Result>() as u32 };
        let status = call(0x4712,(&request as *const Request) as u32,size_of::<Request>() as u32);
        assert!(status == 0 && result.version == 1 && result.clock == 1 && result.unit == 1 &&
            result.source == 1 && result.capabilities & 7 == 7 && result.resolution == 10000 &&
            result.pit_input == 1193182 && result.pit_divisor == 11931,
            "GTOS monotonic clock is unavailable or invalid");
        result.microseconds
    }
}

pub fn futex_wait(futex: &Futex, expected: Primitive, timeout: Option<Duration>) -> bool {
    let notification = NOTIFICATIONS.load(Ordering::Acquire);
    if futex.load(Ordering::Acquire) != expected {
        return true;
    }
    if timeout == Some(Duration::ZERO) {
        return false;
    }
    // A coarse timestamp is rounded down. Include one resolution interval so
    // the difference between two samples cannot report the deadline early.
    let deadline = timeout.map(|duration| (platform::now_us(),
        (duration.as_nanos() + 999) / 1000 + u128::from(platform::resolution_us())));
    loop {
        if futex.load(Ordering::Acquire) != expected ||
            NOTIFICATIONS.load(Ordering::Acquire) != notification {
            return true;
        }
        if let Some((start,budget)) = deadline {
            let elapsed = platform::now_us().checked_sub(start)
                .expect("GTOS monotonic clock moved backwards");
            if u128::from(elapsed) >= budget {
                return false;
            }
        }
        platform::yield_now();
    }
}

pub fn futex_wake(_futex: &Futex) -> bool {
    NOTIFICATIONS.fetch_add(1,Ordering::Release);
    // No kernel-suspended thread was resumed. Other supported platforms also
    // cannot report a wake count and return false here.
    false
}

pub fn futex_wake_all(_futex: &Futex) {
    NOTIFICATIONS.fetch_add(1,Ordering::Release);
}
