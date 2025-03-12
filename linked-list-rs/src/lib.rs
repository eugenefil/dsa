use std::marker::PhantomData;
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

    pub fn iter(&self) -> Iter<'_, T> {
        Iter {
            head: self.head,
            tail: self.tail,
            len: self.len,
            marker: PhantomData,
        }
    }

    pub fn iter_mut(&mut self) -> IterMut<'_, T> {
        IterMut {
            head: self.head,
            tail: self.tail,
            len: self.len,
            marker: PhantomData,
        }
    }
}

unsafe impl<T> Send for LinkedList<T> {}
unsafe impl<T> Sync for LinkedList<T> {}

impl<T> Drop for LinkedList<T> {
    fn drop(&mut self) {
        while let Some(_) = self.pop_front() {}
    }
}

impl<T> IntoIterator for LinkedList<T> {
    type Item = T;
    type IntoIter = IntoIter<T>;

    fn into_iter(self) -> Self::IntoIter {
        IntoIter(self)
    }
}

pub struct IntoIter<T>(LinkedList<T>);

impl<T> Iterator for IntoIter<T> {
    type Item = T;

    fn next(&mut self) -> Option<T> {
        self.0.pop_front()
    }
}

pub struct Iter<'a, T> {
    head: Link<T>,
    tail: Link<T>,
    len: usize,
    marker: PhantomData<&'a T>,
}

unsafe impl<T> Send for Iter<'_, T> {}
unsafe impl<T> Sync for Iter<'_, T> {}

impl<'a, T> Iterator for Iter<'a, T> {
    type Item = &'a T;

    fn next(&mut self) -> Option<Self::Item> {
        // checking len is simpler than comparing head and tail
        if self.len > 0 {
            self.head.map(|head| unsafe {
                self.head = (*head.as_ptr()).next;
                self.len -= 1;
                &(*head.as_ptr()).elem
            })
        } else {
            None
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.len, Some(self.len))
    }
}

impl<'a, T> DoubleEndedIterator for Iter<'a, T> {
    fn next_back(&mut self) -> Option<Self::Item> {
        if self.len > 0 {
            self.tail.map(|tail| unsafe {
                self.tail = (*tail.as_ptr()).prev;
                self.len -= 1;
                &(*tail.as_ptr()).elem
            })
        } else {
            None
        }
    }
}

pub struct IterMut<'a, T> {
    head: Link<T>,
    tail: Link<T>,
    len: usize,
    marker: PhantomData<&'a mut T>,
}

unsafe impl<T> Send for IterMut<'_, T> {}
unsafe impl<T> Sync for IterMut<'_, T> {}

impl<'a, T> Iterator for IterMut<'a, T> {
    type Item = &'a mut T;

    fn next(&mut self) -> Option<Self::Item> {
        // checking len is simpler than comparing head and tail
        if self.len > 0 {
            self.head.map(|head| unsafe {
                self.head = (*head.as_ptr()).next;
                self.len -= 1;
                &mut (*head.as_ptr()).elem
            })
        } else {
            None
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        (self.len, Some(self.len))
    }
}

impl<'a, T> DoubleEndedIterator for IterMut<'a, T> {
    fn next_back(&mut self) -> Option<Self::Item> {
        if self.len > 0 {
            self.tail.map(|tail| unsafe {
                self.tail = (*tail.as_ptr()).prev;
                self.len -= 1;
                &mut (*tail.as_ptr()).elem
            })
        } else {
            None
        }
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

    #[test]
    fn test_into_iter() {
        assert!(LinkedList::<i32>::new().into_iter().next().is_none());
        let mut x = LinkedList::new();
        x.push_back(1);
        x.push_back(2);
        let mut it = x.into_iter();
        assert_eq!(it.next(), Some(1));
        assert_eq!(it.next(), Some(2));
        assert_eq!(it.next(), None);
    }

    #[test]
    fn test_iter() {
        let mut x = LinkedList::new();
        assert!(x.iter().next().is_none());
        x.push_back(1);
        x.push_back(2);
        let mut it = x.iter();
        assert_eq!(it.next(), Some(&1));
        assert_eq!(it.next(), Some(&2));
        assert_eq!(it.next(), None);

        let mut it = x.iter().rev();
        assert_eq!(it.next(), Some(&2));
        assert_eq!(it.next(), Some(&1));
        assert_eq!(it.next(), None);
    }

    #[test]
    fn test_iter_mut() {
        let mut x = LinkedList::new();
        assert!(x.iter_mut().next().is_none());
        x.push_back(1);
        x.push_back(2);
        let mut it = x.iter_mut();
        assert_eq!(it.next(), Some(&mut 1));
        assert_eq!(it.next(), Some(&mut 2));
        assert_eq!(it.next(), None);

        x.iter_mut().for_each(|t| *t *= 10);

        let mut it = x.iter_mut().rev();
        assert_eq!(it.next(), Some(&mut 20));
        assert_eq!(it.next(), Some(&mut 10));
        assert_eq!(it.next(), None);
    }

    #[allow(dead_code)]
    fn test_sync_send() {
        fn is_sync<T: Sync>() {}
        fn is_send<T: Send>() {}

        is_sync::<LinkedList<i32>>();
        is_send::<LinkedList<i32>>();

        is_sync::<IntoIter<i32>>();
        is_send::<IntoIter<i32>>();

        is_sync::<Iter<i32>>();
        is_send::<Iter<i32>>();

        is_sync::<IterMut<i32>>();
        is_send::<IterMut<i32>>();
    }

    #[allow(dead_code)]
    fn test_variance() {
        fn list_is_covariant<'a, T>(x: LinkedList<&'static T>) -> LinkedList<&'a T> {
            x
        }
        fn into_iter_is_covariant<'a, T>(x: IntoIter<&'static T>) -> IntoIter<&'a T> {
            x
        }
        fn iter_is_covariant_over_a<'a, T>(x: Iter<'static, T>) -> Iter<'a, T> {
            x
        }
        fn iter_is_covariant_over_t<'i, 'a, T>(x: Iter<'i, &'static T>) -> Iter<'i, &'a T> {
            x
        }
        fn iter_mut_is_covariant_over_a<'a, T>(x: IterMut<'static, T>) -> IterMut<'a, T> {
            x
        }
    }
}

/// ```compile_fail
/// use linked_list_rs::IterMut;
/// fn iter_mut_is_covariant_over_t<'i, 'a, T>(x: IterMut<'i, &'static T>) -> IterMut<'i, &'a T> {
///     x
/// }
/// ```
#[allow(dead_code)]
fn test_invariance() {}
