use crate::futex;
use std::cell::UnsafeCell;
use std::sync::atomic::{AtomicU32, Ordering};

pub struct Mutex<T> {
    // 0 - unlocked
    // 1 - locked, no waiters
    // 2 - locked, there are waiters
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
        if self
            .state
            .compare_exchange(0, 1, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            let mut count = 100;
            while self.state.load(Ordering::Relaxed) == 1 && count > 0 {
                count -= 1;
                std::hint::spin_loop();
            }
            if self
                .state
                .compare_exchange(0, 1, Ordering::Acquire, Ordering::Relaxed)
                .is_err()
            {
                // after a waiter is woken, it doesn't know whether there are more
                // waiters, so it sets the state to 2 in case there are waiters left
                while self.state.swap(2, Ordering::Acquire) != 0 {
                    // compare-and-block, see futex(2)
                    futex::wait(&self.state, 2).unwrap();
                }
            }
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
        // do syscall only if we have waiters
        if self.mutex.state.swap(0, Ordering::Release) == 2 {
            futex::wake(&self.mutex.state, 1).unwrap();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_works() {
        let m = Mutex::new(0);
        let start = std::time::Instant::now();
        std::thread::scope(|s| {
            for _ in 0..4 {
                s.spawn(|| {
                    for _ in 0..100_000 {
                        *m.lock() += 1;
                    }
                });
            }
        });
        println!("{:?}", start.elapsed());
        assert_eq!(m.into_inner(), 400_000);
    }
}
