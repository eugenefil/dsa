use std::sync::atomic::{AtomicBool, Ordering};
use std::cell::UnsafeCell;

pub struct SpinLock<T> {
    locked: AtomicBool,
    cell: UnsafeCell<T>, // provides interior mutability
}

impl<T> SpinLock<T> {
    pub fn new(data: T) -> Self {
        SpinLock {
            locked: AtomicBool::new(false),
            cell: UnsafeCell::new(data),
        }
    }

    pub fn lock<'a>(&'a self) -> &'a mut T {
        while self.locked.swap(true, Ordering::Acquire) {
            // tell cpu (but not OS) we're busy-looping
            std::hint::spin_loop();
        }
        unsafe { &mut *self.cell.get() }
    }

    // `unlock` has 2 problems:
    //
    // 1. It's not called when e.g. a thread finishes, i.e. it's not RAII.
    //
    // 2. It should terminate the ref returned by `lock` so that can't be used after
    // `unlock` is called. But it's unable to do that.
    pub fn unlock(&self) {
        self.locked.store(false, Ordering::Release);
    }
}

// UnsafeCell is !Sync so we need to explicitly implement Sync.
// Due to locking, at any moment only one thread is allowed to alter lock's data,
// so we can safely pass SpinLock refs around, i.e. being Sync is fine.
// Data type T crosses thread boundary so must be Send; otherwise we could store an Rc
// in one thread and then clone it in another.
unsafe impl<T: Send> Sync for SpinLock<T> {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_works() {
        let lock = SpinLock::new(0u32);
        std::thread::scope(|s| {
            s.spawn(|| {
                let x = lock.lock();
                *x += 1;
                lock.unlock(); // if we forget to unlock, other threads wait forever
            });
            s.spawn(|| {
                let x = lock.lock();
                *x += 1;
                lock.unlock();
                // *x += 1; // x should not live after unlocking, but it does
            });
        });
        assert_eq!(*lock.lock(), 2);
    }
}
