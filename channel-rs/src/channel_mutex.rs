use std::collections::VecDeque;
use std::sync::{Condvar, Mutex};

pub struct Channel<T> {
    queue: Mutex<VecDeque<T>>,
    ready: Condvar,
}

impl<T> Channel<T> {
    pub fn new() -> Self {
        Self {
            queue: Mutex::new(VecDeque::new()),
            ready: Condvar::new(),
        }
    }

    pub fn send(&self, t: T) {
        self.queue.lock().unwrap().push_back(t);
        self.ready.notify_one();
    }

    pub fn receive(&self) -> T {
        let mut queue = self.queue.lock().unwrap();
        loop {
            if let Some(t) = queue.pop_front() {
                return t;
            } else {
                queue = self.ready.wait(queue).unwrap();
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_works() {
        let channel = Channel::new();
        std::thread::scope(|s| {
            s.spawn(|| {
                channel.send("foo".to_owned());
            });
            s.spawn(|| {
                channel.send("bar".to_owned());
                channel.send("baz".to_owned());
            });
            s.spawn(|| {
                let mut set = std::collections::HashSet::new();
                set.insert(channel.receive());
                set.insert(channel.receive());
                set.insert(channel.receive());
                assert_eq!(set.len(), 3);
                assert!(set.contains("foo"));
                assert!(set.contains("bar"));
                assert!(set.contains("baz"));
            });
        });
    }
}
