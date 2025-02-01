use std::ptr::NonNull;
use std::sync::atomic::{fence, AtomicUsize, Ordering};

struct ArcData<T> {
    data: T,
    count: AtomicUsize,
}

// Refs in Rust are borrowing and we're owning, so we have to use a pointer.
// Our pointer is always non-null, so use NonNull.
pub struct Arc<T> {
    ptr: NonNull<ArcData<T>>,
}

// Sending Arc is basically sending a ref, so T must be Sync.
// After an Arc is sent to a thread, it may end up being the last one, so the thread
// eventually drops T with the Arc. Thus, effectively T was transferred to that thread so
// T must be Send.
unsafe impl<T: Send + Sync> Send for Arc<T> {}
// After we send &Arc to a thread, it can be cloned into a new Arc - effectively we sent
// an Arc to a thread. So everything that applies to Send also applies to Sync - T must
// be both Send and Sync.
unsafe impl<T: Send + Sync> Sync for Arc<T> {}

impl<T> Arc<T> {
    pub fn new(t: T) -> Self {
        Self {
            ptr: NonNull::from(Box::leak(Box::new(ArcData {
                data: t,
                count: AtomicUsize::new(1),
            }))),
        }
    }

    fn data(&self) -> &ArcData<T> {
        unsafe { self.ptr.as_ref() }
    }

    pub fn get_mut(&mut self) -> Option<&mut T> {
        if self.data().count.load(Ordering::Relaxed) == 1 {
            // sync with all earlier drops (see `Arc::drop` for details), make sure
            // nobody's using the data anymore
            fence(Ordering::Acquire);
            // now data can't be used by anybody, so can be safely mutated
            unsafe { Some(&mut self.ptr.as_mut().data) }
        } else {
            None
        }
    }
}

impl<T> Clone for Arc<T> {
    fn clone(&self) -> Self {
        self.data().count.fetch_add(1, Ordering::Relaxed);
        Self { ptr: self.ptr }
    }
}

impl<T> Drop for Arc<T> {
    fn drop(&mut self) {
        // `fetch_sub` below is a release-store b/c uses release ordering for the store.
        // But it's also an RMW. RMWs following a release-store form a release sequence.
        // Thus, each `fetch_sub` starts a release sequence of its own, but is also part
        // of sequences started by earlier `fetch_sub`s. `fetch_add`s in Arc::clone, also
        // being RMWs, become part of release sequences started by `fetch_sub`s, but
        // don't start their own b/c use relaxed ordering for the store part. Starting
        // from `fetch_sub` in the very first drop, all subsequent `fetch_sub`s and
        // `fetch_add`s form a release sequence that ends with `fetch_sub` in the very
        // last drop. When we acquire-load the result of an RMW from a release sequence,
        // we sync with the release-store that started the sequence. Thus, when we
        // acquire-load `count = 1` from the penultimate drop, we sync with that drop,
        // but also with all drops going back to the first one b/c they each start a
        // release sequence that ends with that penultimate drop.
        if self.data().count.fetch_sub(1, Ordering::Release) == 1 {
            // we just saw the penultimate drop `count = 1`, sync with all previous drops
            fence(Ordering::Acquire);
            // now data can't be used by anybody, i.e. no data races, drop the data
            unsafe {
                drop(Box::from_raw(self.ptr.as_ptr()));
            }
        }
    }
}

impl<T> std::ops::Deref for Arc<T> {
    type Target = T;

    fn deref(&self) -> &T {
        &self.data().data
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_works() {
        static DROP_COUNT: AtomicUsize = AtomicUsize::new(0);
        struct DropTest<T>(T);
        impl<T> Drop for DropTest<T> {
            fn drop(&mut self) {
                DROP_COUNT.fetch_add(1, Ordering::Relaxed);
            }
        }
        let mut x = Arc::new(DropTest(7));
        let y = x.clone();
        let t = std::thread::spawn(move || {
            assert_eq!(y.0, 7);
        });
        assert_eq!(x.0, 7);
        t.join().unwrap();
        assert_eq!(DROP_COUNT.load(Ordering::Relaxed), 0);

        x.get_mut().unwrap().0 = 9;
        assert_eq!(x.0, 9);

        drop(x);
        assert_eq!(DROP_COUNT.load(Ordering::Relaxed), 1);
    }
}
