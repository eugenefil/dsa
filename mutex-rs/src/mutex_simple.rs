use crate::futex;
use std::cell::UnsafeCell;
use std::sync::atomic::{AtomicU32, Ordering};

pub struct Mutex<T> {
    // 0 - unlocked
    // 1 - locked
    state: AtomicU32,
    data: UnsafeCell<T>,
}

// we shouldn't be able to send Rc with Mutex, so T must be Send
unsafe impl<T: Send> Send for Mutex<T> {}
// Even if other thread have Mutex refs, only one of them can access data at a time, so
// sharing is fine. Still, we shouldn't be able to write Rc in one thread and clone in
// another, so T must be Send.
unsafe impl<T: Send> Sync for Mutex<T> {}

impl<T> Mutex<T> {
    pub fn new(t: T) -> Self {
        Self {
            state: AtomicU32::new(0),
            data: UnsafeCell::new(t),
        }
    }

    pub fn lock(&self) -> MutexGuard<'_, T> {
        while self.state.swap(1, Ordering::Acquire) == 1 {
            futex::wait(&self.state, 1).unwrap(); // compare-and-block, see futex(2)
        }
        MutexGuard { mutex: self }
    }

    pub fn into_inner(self) -> T {
        self.data.into_inner()
    }
}

pub struct MutexGuard<'a, T> {
    mutex: &'a Mutex<T>,
}

// we shouldn't be able to send Rc with MutexGuard, so T must be Send
unsafe impl<T: Send> Send for MutexGuard<'_, T> {}
// sharing MutexGuard is sharing &T, so T must be Sync
unsafe impl<T: Sync> Sync for MutexGuard<'_, T> {}

impl<T> std::ops::Deref for MutexGuard<'_, T> {
    type Target = T;

    fn deref(&self) -> &T {
        unsafe { &*self.mutex.data.get() }
    }
}

impl<T> std::ops::DerefMut for MutexGuard<'_, T> {
    fn deref_mut(&mut self) -> &mut T {
        unsafe { &mut *self.mutex.data.get() }
    }
}

impl<T> Drop for MutexGuard<'_, T> {
    fn drop(&mut self) {
        self.mutex.state.store(0, Ordering::Release);
        futex::wake(&self.mutex.state, 1).unwrap();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_works() {
        let m = Mutex::new(0);
        std::thread::scope(|s| {
            for _ in 0..4 {
                s.spawn(|| {
                    for _ in 0..100_000 {
                        *m.lock() += 1;
                    }
                });
            }
        });
        assert_eq!(m.into_inner(), 400_000);
    }
}
