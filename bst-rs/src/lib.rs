use std::collections::VecDeque;

type Link<T> = Option<Box<Node<T>>>;

struct Node<T> {
    elem: T,
    left: Link<T>,
    right: Link<T>,
}

impl<T> Node<T> {
    fn new(t: T) -> Self {
        Self {
            elem: t,
            left: None,
            right: None,
        }
    }

    fn iter_mut(&mut self) -> NodeIterMut<'_, T> {
        NodeIterMut {
            elem: Some(&mut self.elem),
            left: self.left.as_mut(),
            right: self.right.as_mut(),
        }
    }
}

struct NodeIterMut<'a, T> {
    elem: Option<&'a mut T>,
    left: Option<&'a mut Box<Node<T>>>,
    right: Option<&'a mut Box<Node<T>>>,
}

enum NodeIterItem<'a, T> {
    Elem(&'a mut T),
    Node(&'a mut Box<Node<T>>),
}

impl<'a, T> Iterator for NodeIterMut<'a, T> {
    type Item = NodeIterItem<'a, T>;

    fn next(&mut self) -> Option<Self::Item> {
        match self.left.take() {
            Some(node) => Some(NodeIterItem::Node(node)),
            None => match self.elem.take() {
                Some(elem) => Some(NodeIterItem::Elem(elem)),
                None => self.right.take().map(|node| {
                    NodeIterItem::Node(node)
                }),
            },
        }
    }
}

pub struct Tree<T> {
    root: Link<T>,
}

impl<T: Ord> Tree<T> {
    pub fn new() -> Self {
        Self { root: None }
    }

    pub fn insert(&mut self, t: T) {
        let Some(mut node) = self.root.as_mut() else {
            self.root = Some(Box::new(Node::new(t)));
            return;
        };
        loop {
            if t < node.elem {
                if node.left.is_none() {
                    node.left = Some(Box::new(Node::new(t)));
                    return;
                }
                node = node.left.as_mut().unwrap();
            } else {
                if t == node.elem {
                    return;
                }
                if node.right.is_none() {
                    node.right = Some(Box::new(Node::new(t)));
                    return;
                }
                node = node.right.as_mut().unwrap();
            }
        }
    }

    pub fn iter_mut(&mut self) -> IterMut<'_, T> {
        let mut iters = VecDeque::new();
        if let Some(root) = self.root.as_mut() {
            iters.push_front(root.iter_mut());
        }
        IterMut(iters)
    }
}

pub struct IterMut<'a, T>(VecDeque<NodeIterMut<'a, T>>);

impl<'a, T> Iterator for IterMut<'a, T> {
    type Item = &'a mut T;

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            let Some(iter) = self.0.front_mut() else {
                return None;
            };
            match iter.next() {
                Some(item) => match item {
                    NodeIterItem::Elem(elem) => {
                        return Some(elem);
                    },
                    NodeIterItem::Node(node) => {
                        self.0.push_front(node.iter_mut());
                        continue;
                    },
                },
                None => {
                    self.0.pop_front();
                    continue;
                },
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn it_works() {
        let mut t = Tree::new();
        t.insert(20);
        t.insert(10);
        t.insert(15);
        t.insert(30);
        assert!(t.iter_mut().eq(vec![10, 15, 20, 30].iter_mut()));
        t.iter_mut().for_each(|elem| *elem *= 2);
        assert!(t.iter_mut().eq(vec![20, 30, 40, 60].iter_mut()));
    }
}
