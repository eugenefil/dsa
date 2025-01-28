use std::cell::UnsafeCell;
use std::mem::MaybeUninit;
use std::sync::{
    atomic::{AtomicU8, Ordering},
    Arc,
};
use std::thread::{self, Thread};

const EMPTY: u8 = 0;
const DATA_READY: u8 = 1;
const RX_READY: u8 = 2;

pub struct Channel<T> {
    data: UnsafeCell<MaybeUninit<T>>,
    rx_thread: UnsafeCell<MaybeUninit<Thread>>,
    state: AtomicU8,
}

impl<T> Drop for Channel<T> {
    fn drop(&mut self) {
        match *self.state.get_mut() {
            EMPTY => (),
            DATA_READY => unsafe { self.data.get_mut().assume_init_drop(); },
            RX_READY => panic!("state should not be RX_READY"),
            _ => (),
        }
    }
}

unsafe impl<T: Send> Sync for Channel<T> {}

pub fn channel<T>() -> (Sender<T>, Receiver<T>) {
    let chan = Arc::new(Channel {
        data: UnsafeCell::new(MaybeUninit::uninit()),
        rx_thread: UnsafeCell::new(MaybeUninit::uninit()),
        state: AtomicU8::new(EMPTY),
    });
    (
        Sender {
            channel: Arc::clone(&chan),
        },
        Receiver { channel: chan },
    )
}

pub struct Sender<T> {
    channel: Arc<Channel<T>>,
}

impl<T> Sender<T> {
    pub fn send(self, t: T) {
        unsafe {
            (*self.channel.data.get()).write(t);
        }
        if self
            .channel
            .state
            .compare_exchange(EMPTY, DATA_READY, Ordering::Release, Ordering::Acquire)
            .is_err()
        {
            // Receiver was first, state is RX_READY, i.e. rx_thread is initialized
            self.channel.state.store(DATA_READY, Ordering::Release);
            unsafe {
                // unpark and drop rx_thread
                (*self.channel.rx_thread.get()).assume_init_read().unpark();
            }
            return;
        }
        // If we are first, just tell receiver data is ready - set state to DATA_READY.
        // No need to wake receiver, it'll get data the first time it checks state.
    }
}

pub struct Receiver<T> {
    channel: Arc<Channel<T>>,
}

impl<T> Receiver<T> {
    pub fn receive(self) -> T {
        unsafe {
            (*self.channel.rx_thread.get()).write(thread::current());
        }
        if self
            .channel
            .state
            .compare_exchange(EMPTY, RX_READY, Ordering::Release, Ordering::Acquire)
            .is_err()
        {
            // Sender was first, state is DATA_READY, i.e. data is initialized
            self.channel.state.store(EMPTY, Ordering::Relaxed);
            unsafe {
                (*self.channel.rx_thread.get()).assume_init_drop();
                return (*self.channel.data.get()).assume_init_read();
            }
        }
        // If we are first, tell sender rx_thread is ready - set state to RX_READY.
        // Wait for DATA_READY from sender. Sender is responsible for rx_thread.
        while self
            .channel
            .state
            .compare_exchange(DATA_READY, EMPTY, Ordering::Acquire, Ordering::Relaxed)
            .is_err()
        {
            thread::park();
        }
        unsafe {
            // rx_thread is dropped by sender
            (*self.channel.data.get()).assume_init_read()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_works() {
        let (tx, rx) = channel();
        std::thread::scope(|s| {
            s.spawn(move || {
                tx.send(7);
            });
            s.spawn(move || {
                assert_eq!(rx.receive(), 7);
            });
        });
    }

    #[test]
    fn send_then_receive() {
        let (tx, rx) = channel();
        tx.send(7);
        assert_eq!(rx.receive(), 7);
    }

    #[test]
    fn receive_then_send() {
        let (tx, rx) = channel();
        let handle = std::thread::spawn(move || {
            assert_eq!(rx.receive(), 7);
        });
        while tx.channel.state.load(Ordering::Relaxed) != RX_READY {}
        tx.send(7);
        handle.join().unwrap();
    }

    #[test]
    fn drop_data() {
        struct Foo<'a> {
            p: &'a mut i32,
        }
        impl Drop for Foo<'_> {
            fn drop(&mut self) {
                *self.p = 7;
            }
        }
        let (tx, rx) = channel();
        let mut x = 1;
        tx.send(Foo { p: &mut x });
        drop(rx);
        assert_eq!(x, 7);
    }
}
