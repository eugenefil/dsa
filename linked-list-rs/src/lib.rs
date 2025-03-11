use std::ptr::NonNull;

// `*mut T` is invariant over T so we instead use `NonNull` which is a wrapper over
// `*const T` in order to be covariant over T but still provide the interface of `*mut T`
// `Option<NonNull<T>>` does not require extra space due to null pointer optimization
type Link<T> = Option<NonNull<Node<T>>>;

pub struct LinkedList<T> {
    head: Link<T>,
    tail: Link<T>,
    len: usize,
}

struct Node<T> {
    prev: Link<T>,
    next: Link<T>,
    elem: T,
}

impl<T> LinkedList<T> {
    pub fn new() -> Self {
        Self {
            head: None,
            tail: None,
            len: 0,
        }
    }

    pub fn len(&self) -> usize {
        self.len
    }

    pub fn front(&self) -> Option<&T> {
        self.head.map(|node| unsafe { &(*node.as_ptr()).elem })
    }

    pub fn front_mut(&mut self) -> Option<&mut T> {
        self.head.map(|node| unsafe { &mut (*node.as_ptr()).elem })
    }

    pub fn back(&self) -> Option<&T> {
        self.tail.map(|node| unsafe { &(*node.as_ptr()).elem })
    }

    pub fn back_mut(&mut self) -> Option<&mut T> {
        self.tail.map(|node| unsafe { &mut (*node.as_ptr()).elem })
    }

    pub fn push_front(&mut self, t: T) {
        let head = unsafe {
            NonNull::new_unchecked(Box::into_raw(Box::new(Node {
                prev: None,
                next: None,
                elem: t,
            })))
        };

        if let Some(old_head) = self.head {
            unsafe {
                (*head.as_ptr()).next = Some(old_head);
                (*old_head.as_ptr()).prev = Some(head);
            }
        } else {
            self.tail = Some(head);
        }
        self.head = Some(head);
        self.len += 1;
    }

    pub fn push_back(&mut self, t: T) {
        let tail = unsafe {
            NonNull::new_unchecked(Box::into_raw(Box::new(Node {
                prev: None,
                next: None,
                elem: t,
            })))
        };

        if let Some(old_tail) = self.tail {
            unsafe {
                (*old_tail.as_ptr()).next = Some(tail);
                (*tail.as_ptr()).prev = Some(old_tail);
            }
        } else {
            self.head = Some(tail);
        }
        self.tail = Some(tail);
        self.len += 1;
    }

    pub fn pop_front(&mut self) -> Option<T> {
        self.head.map(|head| unsafe {
            let head = Box::from_raw(head.as_ptr());
            if let Some(new_head) = head.next {
                (*new_head.as_ptr()).prev = None;
                self.head = Some(new_head);
            } else {
                self.head = None;
                self.tail = None;
            }
            self.len -= 1;
            head.elem
        })
    }

    pub fn pop_back(&mut self) -> Option<T> {
        self.tail.map(|tail| unsafe {
            let tail = Box::from_raw(tail.as_ptr());
            if let Some(new_tail) = tail.prev {
                (*new_tail.as_ptr()).next = None;
                self.tail = Some(new_tail);
            } else {
                self.head = None;
                self.tail = None;
            }
            self.len -= 1;
            tail.elem
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_works() {
        let mut x = LinkedList::new();
        assert_eq!(x.len(), 0);
        assert!(x.front().is_none());
        assert!(x.front_mut().is_none());
        assert!(x.back().is_none());
        assert!(x.back_mut().is_none());
        assert!(x.pop_front().is_none());
        assert!(x.pop_back().is_none());

        x.push_front(8);
        assert_eq!(x.front().unwrap(), &8);
        assert_eq!(x.back().unwrap(), &8);
        x.push_front(7);
        x.push_back(9);
        assert_eq!(x.len(), 3);
        assert_eq!(x.front().unwrap(), &7);
        assert_eq!(x.back().unwrap(), &9);

        *x.front_mut().unwrap() -= 1;
        assert_eq!(x.front().unwrap(), &6);
        *x.back_mut().unwrap() += 1;
        assert_eq!(x.back().unwrap(), &10);

        assert_eq!(x.pop_front().unwrap(), 6);
        assert_eq!(x.len(), 2);
        assert_eq!(x.pop_back().unwrap(), 10);
        assert_eq!(x.len(), 1);
        assert_eq!(x.pop_front().unwrap(), 8);
        assert_eq!(x.len(), 0);
        assert!(x.pop_front().is_none());
    }
}
