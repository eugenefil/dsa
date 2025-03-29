use std::alloc::{self, Layout};
use std::ptr::NonNull;

pub struct Vec<T> {
    ptr: NonNull<T>,
    len: usize,
    cap: usize,
}

impl<T> Vec<T> {
    pub fn new() -> Self {
        assert_ne!(size_of::<T>(), 0, "ZSTs not supported yet");
        Self {
            // pointer types in Rust can't be null for null pointer optimization to work,
            // see https://doc.rust-lang.org/std/option/index.html#representation
            ptr: NonNull::dangling(),
            cap: 0,
            len: 0,
        }
    }

    fn grow(&mut self) {
        let new_cap = if self.cap == 0 {
            1
        } else {
            // current cap <= isize::MAX, so doubling won't overflow usize
            self.cap * 2
        };
        let new_layout = Layout::array::<T>(new_cap).unwrap();

        let new_ptr = if self.len == 0 {
            unsafe { alloc::alloc(new_layout) }
        } else {
            let old_layout = Layout::array::<T>(self.cap).unwrap();
            unsafe { alloc::realloc(self.ptr.as_ptr() as *mut u8, old_layout, new_layout.size()) }
        };

        // exception safety: only assign new values after we know no panic can occur
        self.ptr = NonNull::new(new_ptr as *mut T)
            .unwrap_or_else(|| alloc::handle_alloc_error(new_layout));
        self.cap = new_cap;
    }

    pub fn push(&mut self, value: T) {
        if self.cap == self.len {
            self.grow();
        }
        // SAFETY: after grow() we have
        //
        // - `len < cap`
        //
        // - `cap * size_of::<T>() <= isize::MAX`
        //
        // Thus, `len * size_of::<T>() < isize::MAX`.
        unsafe { self.ptr.add(self.len).write(value) };
        self.len += 1;
    }

    pub fn pop(&mut self) -> Option<T> {
        if self.len == 0 {
            None
        } else {
            self.len -= 1;
            unsafe { Some(self.ptr.add(self.len).read()) }
        }
    }

    pub fn insert(&mut self, index: usize, value: T) {
        assert!(index <= self.len, "index should be <= len");
        if self.cap == self.len {
            self.grow();
        }
        unsafe {
            self.ptr
                .add(index)
                .copy_to(self.ptr.add(index + 1), self.len - index);
            self.ptr.add(index).write(value);
        }
        self.len += 1;
    }

    pub fn remove(&mut self, index: usize) -> T {
        assert!(index < self.len, "index should be < len");
        unsafe {
            let result = self.ptr.add(index).read();
            self.ptr
                .add(index + 1)
                .copy_to(self.ptr.add(index), self.len - index - 1);
            self.len -= 1;
            result
        }
    }

    pub fn len(&self) -> usize {
        self.len
    }

    pub fn capacity(&self) -> usize {
        self.cap
    }
}

impl<T> Drop for Vec<T> {
    fn drop(&mut self) {
        if self.cap > 0 {
            // drop elements, for `T: !Drop` LLVM would optimize extra code out
            while let Some(_) = self.pop() {}

            let layout = Layout::array::<T>(self.cap).unwrap();
            unsafe { alloc::dealloc(self.ptr.as_ptr() as *mut u8, layout) };
        }
    }
}

impl<T> std::ops::Deref for Vec<T> {
    type Target = [T];

    fn deref(&self) -> &[T] {
        unsafe { std::slice::from_raw_parts(self.ptr.as_ptr(), self.len) }
    }
}

impl<T> std::ops::DerefMut for Vec<T> {
    fn deref_mut(&mut self) -> &mut [T] {
        unsafe { std::slice::from_raw_parts_mut(self.ptr.as_ptr(), self.len) }
    }
}

impl<T> IntoIterator for Vec<T> {
    type Item = T;
    type IntoIter = IntoIter<T>;

    fn into_iter(self) -> Self::IntoIter {
        let v = std::mem::ManuallyDrop::new(self); // don't let self be dropped
        IntoIter {
            buf: v.ptr,
            cap: v.cap,
            beg: v.ptr.as_ptr(),
            end: unsafe { v.ptr.add(v.len).as_ptr() },
        }
    }
}

// IntoIter could have used Vec and obtain next element with `remove(0)` but that's an
// overkill due to memmove. Instead we make IntoIter kind of like Vec itself by storing
// the buffer pointer and its capacity in it. We use 2 extra pointers for iteration.
pub struct IntoIter<T> {
    buf: NonNull<T>, // needed for deallocation
    cap: usize,      // needed for deallocation
    beg: *const T,   // needed for iteration
    end: *const T,   // needed for iteration
}

impl<T> Iterator for IntoIter<T> {
    type Item = T;

    fn next(&mut self) -> Option<T> {
        if self.beg == self.end {
            None
        } else {
            unsafe {
                let result = self.beg.read();
                self.beg = self.beg.add(1);
                Some(result)
            }
        }
    }

    fn size_hint(&self) -> (usize, Option<usize>) {
        let remaining = (self.end as usize - self.beg as usize) / size_of::<T>();
        (remaining, Some(remaining))
    }
}

impl<T> DoubleEndedIterator for IntoIter<T> {
    fn next_back(&mut self) -> Option<T> {
        if self.beg == self.end {
            None
        } else {
            unsafe {
                self.end = self.end.sub(1);
                Some(self.end.read())
            }
        }
    }
}

impl<T> Drop for IntoIter<T> {
    fn drop(&mut self) {
        if self.cap > 0 {
            for _ in &mut *self {} // drop remaining elements

            let layout = Layout::array::<T>(self.cap).unwrap();
            unsafe { alloc::dealloc(self.buf.as_ptr() as *mut u8, layout) };
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_works() {
        let mut v = Vec::new();
        assert_eq!(v.len(), 0);
        v.push(7);
        v.push(8);
        v.push(9);
        assert_eq!(v.len(), 3);

        assert_eq!(v.pop(), Some(9));
        assert_eq!(v.len(), 2);
        v.pop();
        v.pop();
        assert_eq!(v.len(), 0);
        assert_eq!(v.pop(), None);

        v.insert(0, 6);
        v.insert(0, 4);
        v.insert(2, 7);
        v.insert(1, 5);
        assert_eq!(v.len(), 4);

        // Deref
        assert_eq!(&v as &[i32], [4, 5, 6, 7]);
        assert!(v.iter().eq([4, 5, 6, 7].iter()));

        assert_eq!(v.remove(0), 4);
        assert_eq!(v.remove(1), 6);
        assert_eq!(v.remove(1), 7);
        assert_eq!(v.remove(0), 5);
        assert_eq!(v.len(), 0);

        // DerefMut
        v.push(7);
        v[0] += 1;
        assert_eq!(v[0], 8);
    }

    #[test]
    fn drops_values() {
        static mut DROP_COUNT: u32 = 0;
        struct S(#[allow(dead_code)] u32); // use u32 so that S is not a ZST
        impl Drop for S {
            fn drop(&mut self) {
                unsafe { DROP_COUNT += 1 };
            }
        }

        let mut v = Vec::new();
        v.push(S(0));
        v.push(S(0));
        v.push(S(0));
        drop(v);
        assert_eq!(unsafe { DROP_COUNT }, 3);
    }

    #[test]
    fn iterator() {
        let mut v = Vec::new();
        v.push(7);
        v.push(8);
        v.push(9);
        let mut i = v.into_iter();
        assert_eq!(i.size_hint(), (3, Some(3)));
        assert_eq!(i.next(), Some(7));
        assert_eq!(i.next_back(), Some(9));
        assert_eq!(i.next(), Some(8));
        assert_eq!(i.next(), None);
    }

    #[test]
    fn iterator_drops_values() {
        static mut DROP_COUNT: u32 = 0;
        struct S(#[allow(dead_code)] u32); // use u32 so that S is not a ZST
        impl Drop for S {
            fn drop(&mut self) {
                unsafe { DROP_COUNT += 1 };
            }
        }

        let mut v = Vec::new();
        v.push(S(0));
        v.push(S(0));
        v.push(S(0));
        drop(v.into_iter());
        assert_eq!(unsafe { DROP_COUNT }, 3);
    }
}
