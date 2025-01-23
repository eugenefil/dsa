use std::sync::atomic::{AtomicBool, Ordering};
use std::cell::UnsafeCell;
use std::ops::{Deref, DerefMut};

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

    pub fn lock(&self) -> SpinLockGuard<'_, T> {
        while self.locked.swap(true, Ordering::Acquire) {
            // tell cpu (but not OS) we're busy-looping
            std::hint::spin_loop();
        }
        SpinLockGuard { lock: self }
    }
}

// UnsafeCell is !Sync so we need to explicitly implement Sync.
// Due to locking, at any moment only one thread is allowed to alter lock's data,
// so we can safely pass SpinLock refs around, i.e. being Sync is fine.
// Data type T crosses thread boundary so must be Send; otherwise we could store an Rc
// in one thread and then clone it in another.
unsafe impl<T: Send> Sync for SpinLock<T> {}

pub struct SpinLockGuard<'a, T> {
    lock: &'a SpinLock<T>,
}

impl<T> Deref for SpinLockGuard<'_, T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        // SAFETY: Having the guard means the lock is held by current thread. No other
        // thread can obtain a guard, except by sharing this one. Sharing this guard with
        // another thread is ok - that thread won't be allowed to outlive the guard.
        // The returned ref lives as long as the guard.
        unsafe { &*self.lock.cell.get() }
    }
}

impl<T> DerefMut for SpinLockGuard<'_, T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        // SAFETY: Having the guard means the lock is held by current thread. No other
        // thread can obtain a guard, except by sharing this one. Sharing this guard with
        // another thread is ok - that thread won't be allowed to outlive the guard.
        // The returned ref lives as long as the guard. Mutable aliasing rule won't let
        // having 2 of these refs at the same time.
        unsafe { &mut *self.lock.cell.get() }
    }
}

impl<T> Drop for SpinLockGuard<'_, T> {
    fn drop(&mut self) {
        self.lock.locked.store(false, Ordering::Release);
    }
}

// with Deref, SpinLockGuard behaves like &T so should be Sync only if T is Sync
unsafe impl<T: Sync> Sync for SpinLockGuard<'_, T> {}
// same for Send
unsafe impl<T: Sync> Send for SpinLockGuard<'_, T> {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_works() {
        let lock = SpinLock::new(0u32);
        std::thread::scope(|s| {
            s.spawn(|| {
                let mut x = lock.lock();
                *x += 1;
            });
            s.spawn(|| {
                *lock.lock() += 1;
            });
        });
        assert_eq!(*lock.lock(), 2);
    }

    #[test]
    fn share_guard() {
        let lock = SpinLock::new("foo".to_owned());
        let x = lock.lock();
        std::thread::scope(|s| {
            s.spawn(|| {
                assert_eq!(*x, "foo");
            });
        });
    }

    #[test]
    fn send_guard() {
        let lock = SpinLock::new("foo".to_owned());
        let x = lock.lock();
        std::thread::scope(|s| {
            s.spawn(move || {
                assert_eq!(*x, "foo");
            });
        });
    }

    #[test]
    fn share_and_mutate_guard() {
        let lock = SpinLock::new("foo".to_owned());
        let mut x = lock.lock();
        std::thread::scope(|s| {
            s.spawn(|| {
                x.push_str("bar");
            });
        });
        assert_eq!(*x, "foobar");
    }
}
