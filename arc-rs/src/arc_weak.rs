use std::mem::ManuallyDrop;
use std::ptr::NonNull;
use std::sync::atomic::{fence, AtomicUsize, Ordering};

// `weak_count` accounts for both Weaks and Arcs - it contains 1 for _every_ Weak and an
// extra 1 for _all_ Arcs taken together, i.e. all Args together represent 1 extra Weak.
// If `weak_count` only counted Weaks, both `weak_count` and `strong_count` would need to
// be checked in `Weak::drop` to decide whether ArcData should be dropped. Checking both
// vars is not atomic while checking only `weak_count` is.
struct ArcData<T> {
    data: ManuallyDrop<T>,
    strong_count: AtomicUsize,
    weak_count: AtomicUsize,
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
                data: ManuallyDrop::new(t),
                strong_count: AtomicUsize::new(1),
                weak_count: AtomicUsize::new(1),
            }))),
        }
    }

    fn data(&self) -> &ArcData<T> {
        unsafe { self.ptr.as_ref() }
    }

    pub fn downgrade(arc: &Self) -> Weak<T> {
        let mut weak_count = arc.data().weak_count.load(Ordering::Relaxed);
        loop {
            // busy-loop while downgrading is locked by get_mut
            while weak_count == usize::MAX {
                std::hint::spin_loop();
                weak_count = arc.data().weak_count.load(Ordering::Relaxed);
            }
            // even if size of Weak is 2 bytes, `usize::MAX / 2` Weaks already will fill
            // the whole memory sans 1 byte, so overflowing should not happen
            if weak_count > usize::MAX / 2 {
                std::process::abort(); // extra safety
            }
            // Use acquire ordering for the load so that strong_count load in get_mut
            // happens before downgrading is unlocked. See get_mut for details.
            if let Err(n) = arc.data().weak_count.compare_exchange_weak(
                weak_count,
                weak_count + 1,
                Ordering::Acquire,
                Ordering::Relaxed,
            ) {
                weak_count = n;
                continue;
            }
            return Weak { ptr: arc.ptr };
        }
    }

    pub fn get_mut(&mut self) -> Option<&mut T> {
        // `weak_count = 1` means there are no Weaks left, only Arcs which together
        // account for one extra Weak. Atomically disable downgrading Arcs to Weaks by
        // setting special value `weak_count = usize::MAX` so that weak_count does not
        // change until after we check strong_count. Despite we see there are no Weaks,
        // side-effects from those Weaks may yet to be seen. Namely, increments to
        // strong_count from `Weak::upgrade`s could be reordered to happen after
        // decrements to weak_count from `Weak::drop`s. Use acquire ordering for loading
        // weak_count to sync with all `Weak::drop`s which guarantees `Weak::upgrade`s
        // happened before the drops.
        if self
            .data()
            .weak_count
            .compare_exchange(1, usize::MAX, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            return None;
        }
        let is_single = self.data().strong_count.load(Ordering::Relaxed) == 1;
        // Enable downgrading back. If the only Arc is us, no downgrading can happen.
        // Use release ordering below so that strong_count load above happens before
        // downgrading is enabled. With relaxed ordering, enabling downgrading could be
        // reordered to happen before the load and a second Arc in another thread could
        // quickly downgrade to a new Weak and then drop so that strong_count still loads
        // 1. That Weak could then be upgraded to an Arc and its data read would lead to
        // data races with a mutable ref returned by get_mut.
        //
        // Essentially, we have a critical section here that uses weak_count as a lock
        // which protects strong_count. When we take the lock here, downgrade busy-loops
        // waiting for unlock. When unlocked, `Arc::downgrade` takes the lock by
        // incrementing weak_count to 2 and uses acquire ordering so that release
        // ordering here takes effect. Downgrade releases the lock in `Weak::drop`. When
        // we acquire-load weak_count from the last `Weak::drop` here, we take the lock
        // again and so on.
        //
        // Note that the lock only protects against downgrading. We could have an Arc
        // dropped in another thread which we don't see yet so strong_count loads 2
        // instead of 1. But that only leads to failing get_mut and not data races.
        self.data().weak_count.store(1, Ordering::Release);
        if !is_single {
            return None;
        }
        // sync with all earlier Arc drops (see `Arc::drop` for details), make sure
        // nobody's using the data anymore
        fence(Ordering::Acquire);
        // SAFETY: data isn't used by anybody, so may be mutated
        unsafe { Some(&mut self.ptr.as_mut().data) }
    }
}

impl<T> Clone for Arc<T> {
    fn clone(&self) -> Self {
        // even if size of Arc is 2 bytes, `usize::MAX / 2` Arcs already will fill the
        // whole memory sans 1 byte, so overflowing should not happen
        if self.data().strong_count.fetch_add(1, Ordering::Relaxed) > usize::MAX / 2 {
            std::process::abort(); // extra safety
        }
        Self { ptr: self.ptr }
    }
}

impl<T> Drop for Arc<T> {
    fn drop(&mut self) {
        // `fetch_sub` below is a release-store b/c uses release ordering for the store.
        // But it's also an RMW. RMWs following a release-store form a release sequence.
        // Thus, each `fetch_sub` starts a release sequence of its own, but is also part
        // of sequences started by earlier `fetch_sub`s. `fetch_add`s in Arc::clone and
        // Weak::upgrade, also being RMWs, are part of release sequences started by
        // `fetch_sub`s, but don't start their own b/c use relaxed ordering for the store
        // part. Starting from `fetch_sub` in the very first drop, all subsequent
        // `fetch_sub`s and `fetch_add`s form a release sequence that ends with
        // `fetch_sub` in the very last drop. When we acquire-load the result of an RMW
        // from a release sequence, we sync with the release-store that started the
        // sequence. Thus, when we acquire-load `strong_count = 1` we sync with all drops
        // going back to the first one b/c they each start a release sequence that
        // eventually leads to `strong_count = 1`.
        //
        // After we saw `strong_count = 1` and decremented it to 0 atomically, we know
        // this Arc is the last so only Weak upgrades can maybe increase `strong_count`.
        // But the total modification order guarantees that either:
        //
        // - we decrement `strong_count` to 0 first, then Weak upgrades see that and fail
        //
        // - Weak upgrade increments `strong_count` to 2 first, then we wouldn't have
        //   seen `strong_count = 1` in the first place
        //
        // Thus, if we were able to decrement to 0, then no Weak upgrades can happen.
        if self.data().strong_count.fetch_sub(1, Ordering::Release) == 1 {
            // we saw `strong_count = 1`, sync with all previous Arc drops
            fence(Ordering::Acquire);
            // SAFETY: data isn't used by anybody, i.e. no data races, so may be dropped
            unsafe {
                ManuallyDrop::drop(&mut self.ptr.as_mut().data);
            }
            // all Arcs are gone, drop the extra Weak that accounted for them
            drop(Weak { ptr: self.ptr });
        }
    }
}

impl<T> std::ops::Deref for Arc<T> {
    type Target = T;

    fn deref(&self) -> &T {
        &self.data().data
    }
}

pub struct Weak<T> {
    ptr: NonNull<ArcData<T>>,
}

// see Arc's Send and Sync for details
unsafe impl<T: Send + Sync> Send for Weak<T> {}
unsafe impl<T: Send + Sync> Sync for Weak<T> {}

impl<T> Weak<T> {
    fn data(&self) -> &ArcData<T> {
        unsafe { self.ptr.as_ref() }
    }

    pub fn upgrade(&self) -> Option<Arc<T>> {
        let mut strong_count = self.data().strong_count.load(Ordering::Relaxed);
        loop {
            if strong_count == 0 {
                return None;
            }
            // even if size of Arc is 2 bytes, `usize::MAX / 2` Arcs already will fill
            // the whole memory sans 1 byte, so overflowing should not happen
            if strong_count > usize::MAX / 2 {
                std::process::abort(); // extra safety
            }
            if let Err(n) = self.data().strong_count.compare_exchange_weak(
                strong_count,
                strong_count + 1,
                Ordering::Relaxed,
                Ordering::Relaxed,
            ) {
                strong_count = n;
                continue;
            }
            return Some(Arc { ptr: self.ptr });
        }
    }
}

impl<T> Clone for Weak<T> {
    fn clone(&self) -> Self {
        // even if size of Weak is 2 bytes, `usize::MAX / 2` Weaks already will fill the
        // whole memory sans 1 byte, so overflowing should not happen
        if self.data().weak_count.fetch_add(1, Ordering::Relaxed) > usize::MAX / 2 {
            std::process::abort(); // extra safety
        }
        Self { ptr: self.ptr }
    }
}

impl<T> Drop for Weak<T> {
    fn drop(&mut self) {
        // if weak_count reaches 0, _both_ Weaks and Arcs are gone
        if self.data().weak_count.fetch_sub(1, Ordering::Release) == 1 {
            // sync with all previous Weak drops
            fence(Ordering::Acquire);
            // SAFETY: data isn't used, so may be dropped
            unsafe {
                drop(Box::from_raw(self.ptr.as_ptr()));
            }
        }
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
        let z = Arc::downgrade(&x);
        let w = Arc::downgrade(&x);
        let t = std::thread::spawn(move || {
            assert_eq!(y.0, 7);
            assert_eq!(w.upgrade().unwrap().0, 7);
        });
        assert_eq!(x.0, 7);
        t.join().unwrap();

        assert_eq!(DROP_COUNT.load(Ordering::Relaxed), 0);
        assert_eq!(z.upgrade().unwrap().0, 7);

        drop(z);
        x.get_mut().unwrap().0 = 9;
        assert_eq!(x.0, 9);

        let z = Arc::downgrade(&x);
        drop(x);
        assert_eq!(DROP_COUNT.load(Ordering::Relaxed), 1);
        assert!(z.upgrade().is_none());
    }
}
