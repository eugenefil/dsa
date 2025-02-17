use crate::{futex, mutex_simple::MutexGuard};
use std::sync::atomic::{AtomicU32, Ordering};

pub struct Condvar {
    counter: AtomicU32,
}

impl Condvar {
    pub fn new() -> Self {
        Self {
            counter: AtomicU32::new(0),
        }
    }

    pub fn wait<'a, T>(&self, guard: MutexGuard<'a, T>) -> MutexGuard<'a, T> {
        // We hold the mutex, nobody can change shared data and wake waiters until we
        // release it. Thus, the counter can't be changed until we release the mutex.
        let counter = self.counter.load(Ordering::Relaxed);

        let mutex = guard.mutex;
        // release the mutex, producer might grab it right away to add the resource and
        // wake a waiter while incrementing the counter
        drop(guard);

        // if the counter stayed unchanged, i.e. nobody grabbed the mutex, produced the
        // resource and notified waiters about it, go to sleep
        futex::wait(&self.counter, counter).unwrap();
        // calling code does the resource checking loop, which also handles spurious
        // wake-ups, so we can just return the guard
        mutex.lock()
    }

    pub fn notify_one(&self) {
        // change the futex word _before_ waking up a waiter; otherwise, it could go to
        // sleep right after the waking but before futex word is changed, i.e. missing
        // futex word change and sleeping infinitely
        self.counter.fetch_add(1, Ordering::Relaxed);
        futex::wake(&self.counter, 1).unwrap();
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_works() {
        struct Resource {
            count: u32,
        }
        let m = crate::mutex_simple::Mutex::new(Resource { count: 0 });
        let cv = Condvar::new();
        let mut wakeup_count = 0;
        std::thread::scope(|s| {
            s.spawn(|| {
                // producer
                std::thread::sleep(std::time::Duration::from_secs(1));
                m.lock().count += 1; // produce resource
                cv.notify_one(); // notify consumer
            });

            // consumer
            let mut resource = m.lock(); // acquire resource lock
            while resource.count == 0 {
                resource = cv.wait(resource); // there's no resource, go to sleep
                wakeup_count += 1;
            }
            resource.count -= 1; // consume resource
        });
        assert!(wakeup_count > 0); // check we really waited for producer
        assert!(wakeup_count < 10); // check we weren't simply spinning
    }
}
