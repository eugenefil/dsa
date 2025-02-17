use std::sync::atomic::AtomicU32;

pub fn wait(addr: &AtomicU32, expected: u32) -> Result<(), i32> {
    let ret = unsafe {
        libc::syscall(
            libc::SYS_futex,
            addr as *const AtomicU32,
            libc::FUTEX_WAIT,
            expected,
            std::ptr::null::<libc::timespec>(),
        )
    };
    if ret < 0 {
        let errno = unsafe { *libc::__errno_location() };
        if errno == libc::EAGAIN {
            Ok(()) // value is not equal to expected, no need to sleep
        } else {
            Err(errno)
        }
    } else {
        Ok(()) // woken up
    }
}

pub fn wake(addr: &AtomicU32, how_many: u32) -> Result<u32, i32> {
    assert!(
        how_many <= libc::INT_MAX as u32,
        "how_many should be less or equal than INT_MAX"
    );
    let ret = unsafe {
        libc::syscall(
            libc::SYS_futex,
            addr as *const AtomicU32,
            libc::FUTEX_WAKE,
            how_many,
        )
    };
    if ret >= 0 {
        Ok(ret as u32)
    } else {
        Err(unsafe { *libc::__errno_location() })
    }
}
