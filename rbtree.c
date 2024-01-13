#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <signal.h>
#include <string.h>

struct rb_node {
	struct rb_node *parent;
	struct rb_node *left;
	struct rb_node *right;
	int red;
};

struct rb_tree {
	struct rb_node *root;
	/* Sentinel node is used in place of NULL. It's always black.
	 * It's not strictly needed for inserting, but deleting may set
	 * its parent - used in the first iteration of the follow-up fixup.
	 */
	struct rb_node nil;
};

static void rb_init(struct rb_tree *tree)
{
	memset(tree, 0, sizeof(*tree));
	tree->root = &tree->nil;
}

#define RB_NIL(n) (!(n)->left)

/*
*   N             R
*  / \           / \
* 1   R   -->   N   3
*    / \       / \
*   2   3     1   2
*/
static void rb_rotate_left(struct rb_tree *tree, struct rb_node *node)
{
	struct rb_node *r = node->right;
	node->right = r->left;
	r->left->parent = node;

	if (node == tree->root)
		tree->root = r;
	else if (node == node->parent->left)
		node->parent->left = r;
	else
		node->parent->right = r;
	r->parent = node->parent;

	r->left = node;
	node->parent = r;
}

/*
*     N         L
*    / \       / \
*   L   1 --> 2   N
*  / \           / \
* 2   3         3   1
*/
static void rb_rotate_right(struct rb_tree *tree, struct rb_node *node)
{
	struct rb_node *l = node->left;
	node->left = l->right;
	l->right->parent = node;

	if (node == tree->root)
		tree->root = l;
	else if (node == node->parent->left)
		node->parent->left = l;
	else
		node->parent->right = l;
	l->parent = node->parent;

	l->right = node;
	node->parent = l;
}

static void rb_insert(struct rb_tree *tree, struct rb_node *node,
		      int (*cmp)(const struct rb_node *new, const struct rb_node *old))
{
	struct rb_node *nil = &tree->nil;
	struct rb_node *parent = nil;
	struct rb_node *n = tree->root;
	int ret;
	while (n != nil) {
		parent = n;
		ret = cmp(node, n);
		if (ret < 0)
			n = n->left;
		else
			n = n->right;
	}
	if (parent == nil)
		tree->root = node;
	else if (ret < 0)
		parent->left = node;
	else
		parent->right = node;
	node->parent = parent;
	node->left = nil;
	node->right = nil;
	node->red = 1;

	while (node->parent->red) {
		/* Here node can't be root, b/c root's parent, nil, is black.
		 * Also can't be root's child, b/c root is always black.
		 * So we're at least root's grandchild.
		 */
		struct rb_node *parent = node->parent;
		struct rb_node *grand = parent->parent;
		if (parent == grand->left) {
			struct rb_node *uncle = grand->right;
			if (uncle->red) {
				/* Both parent and uncle are red. This
				 * means grandparent is black. Swap colors:
				 * make siblings black and grandparent red.
				 * Continue tree checking with grandparent.
				 *
				 * (uppercase is black, [] - current node)
				 *      G          [g]
				 *     / \         / \
				 *    p   u -->   P   U
				 *   /           /
				 * [n]          n
				 */
				parent->red = 0;
				uncle->red = 0;
				grand->red = 1;
				node = grand;
			} else {
				/* Parent is red, but uncle is black.
				 * If node is to the right of parent, rotate
				 * parent left to become node's left child.
				 * I.e. move red-red conflict to the left.
				 * If kept as is (on the right), the conflict
				 * would remain after rotating grandparent
				 * right below: red node will be red
				 * grandparent's left child. Make parent
				 * (i.e. new lower node) new current node.
				 *
				 *     G             G
				 *    / \           / \
				 *   p   U -->     n   U
				 *  / \           /
				 * *  [n]       [p]
				 *              /
				 *             *
				 */
				if (node == parent->right) {
					rb_rotate_left(tree, parent);
					node = parent;
				}
				/* Here node is to the left of parent,
				 * either b/c of left rotation above or b/c
				 * insertion happens on the left. Make parent
				 * black, grandparent red and rotate
				 * grandparent right to remove one tree level:
				 * red grandparent becomes right child of
				 * black parent. Loop ends after that.
				 *
				 *      G          P
				 *     / \        / \
				 *    p   U --> [n]  g
				 *   / \            / \
				 * [n]  *          *   U
				 */

				/* node could have been changed above */
				node->parent->red = 0;
				grand->red = 1;
				rb_rotate_right(tree, grand);
			}
		} else {
			/* Same as above, but with left and right exchanged. */
			struct rb_node *uncle = grand->left;
			if (uncle->red) {
				parent->red = 0;
				uncle->red = 0;
				grand->red = 1;
				node = grand;
			} else {
				if (node == parent->left) {
					rb_rotate_right(tree, parent);
					node = parent;
				}
				node->parent->red = 0;
				grand->red = 1;
				rb_rotate_left(tree, grand);
			}
		}
	}
	tree->root->red = 0;
}

static struct rb_node *rb_find(struct rb_tree *tree, const struct rb_node *node,
	int (*cmp)(const struct rb_node *n1, const struct rb_node *n2))
{
	struct rb_node *nil = &tree->nil;
	struct rb_node *n = tree->root;
	while (n != nil) {
		int ret = cmp(node, n);
		if (!ret)
			return n;
		else if (ret < 0)
			n = n->left;
		else
			n = n->right;
	}
	return NULL;
}

static void rb_replace(struct rb_tree *tree, struct rb_node *what,
			  struct rb_node *with)
{
	if (what == tree->root)
		tree->root = with;
	else if (what == what->parent->left)
		what->parent->left = with;
	else
		what->parent->right = with;
	/*
	 * Even if replacement node (aka "with" node) is nil, delete
	 * fixup depends on its parent being correctly set below.
	 * That's why we have to use sentinel nil instead of NULL.
	 */
	with->parent = what->parent;
}

static void rb_delete(struct rb_tree *tree, struct rb_node *node)
{
	/* Save original color of the deleted node to later determine
	 * whether we need to restore red-black properties.
	 */
	int orig_red = node->red;
	/*
	 * After a black node is deleted, a node that takes its place is
	 * viewed as carrying an extra black. If the latter is black itself,
	 * it becomes doubly black, otherwise red-and-black. The point of
	 * the follow-up fixup is to remove that extra black.
	 */
	struct rb_node *extra;
	struct rb_node *nil = &tree->nil;
	if (node->left == nil) {
		extra = node->right;
		rb_replace(tree, node, node->right);
	} else if (node->right == nil) {
		extra = node->left;
		rb_replace(tree, node, node->left);
	} else {
		/* Find node's successor - the smallest node to the right of it.
		 * It's either node's immediate right or the lowest left
		 * descendant of immediate right. Either way successor has
		 * no left, b/c it's the smallest.
		 */
		struct rb_node *next = node->right;
		while (next->left != nil)
			next = next->left;
		/*
		 * Successor will take deleted node's place and color, so
		 * effectively it's successor which is deleted. Thus save
		 * its original color.
		 */
		orig_red = next->red;
		/* Successor has no left, so after its effective deletion
		 * the (possible) extra black is carried by successor's right,
		 * which can be nil.
		 */
		extra = next->right;

		if (next != node->right) {
			/*
			 * If successor is not immediate right, but a lowest
			 * left descendant of it, then delete it from its place
			 * by replacing with its right. Now successor has
			 * neither left nor right. Attach node's right to it.
			 * Note, if successor's right were nil, replacing would
			 * set proper parent on the sentinel node - needed by
			 * the follow-up delete fixup.
			 *
			 * (X carries extra black after successor deletion)
			 *   N           N
			 *  / \         / \
			 * L   R       L   S
			 *    / \  -->      \
			 *   1   2           R
			 *  / \             / \
			 * S   3           1   2
			 *  \             / \
			 *   X           X   3
			 */
			rb_replace(tree, next, next->right);
			next->right = node->right;
			node->right->parent = next;
		} else {
			/*
			 * Successor is node's immediate right. After successor
			 * replaces the node, successor's right will carry the
			 * extra black. Successor's right can be nil, so make
			 * sure sentinels's parent is set up properly - needed
			 * by the follow-up fixup.
			 *
			 *   N
			 *  / \
			 * L   S
			 *      \
			 *       X
			 */
			extra->parent = next;
		}

		rb_replace(tree, node, next);
		next->left = node->left;
		node->left->parent = next;
		next->red = node->red;
	}

	/* Deleting red node does not violate red-black properties:
	 * black heights are unchanged and can be no red-red conflict.
	 */
	if (orig_red)
		return;
	
	/*
	 * Deleted node was black. If node carrying the extra black is
	 * red (i.e. it's red-and-black), then no complex fixup is needed -
	 * just make it simply black to restore the deleted black. This is
	 * done by the statement after the loop. Loop is skipped.
	 */
	while (extra != tree->root && !extra->red) {
		/* The node carrying the extra black is doubly black. */
		struct rb_node *parent = extra->parent;
		if (extra == parent->left) {
			/* Doubly black's sibling can't be nil - there
			 * must be at least one black node on paths
			 * starting from it to match that extra black.
			 */
			struct rb_node *sibling = parent->right;
			if (sibling->red) {
				/*
				 * If sibling is red, then its parent and
				 * children are black. Children can't be nil,
				 * b/c they have to match the extra black.
				 * Swap sibling's and parent's colors, then
				 * rotate parent left, so that new sibling is
				 * black. B/c it's former sibling's child, new
				 * sibling is not nil. Black heights are unchanged.
				 *
				 * (uppercase is black, <> - sibling)
				 *   P             S
				 *  / \           / \
				 * X  <s>  -->   p   B
				 *    / \       / \
				 *   A   B     X  <A>
				 */
				sibling->red = 0;
				parent->red = 1;
				rb_rotate_left(tree, parent);
				sibling = parent->right;
			}

			/* Sibling is black and not nil, but children can be nil. */
			if (!sibling->left->red && !sibling->right->red) {
				/*
				 * Paths through the doubly black node and its
				 * black sibling have same black heights. If
				 * sibling's both children are black, decrement
				 * the heights by making sibling red and pushing
				 * the extra black from doubly black to parent.
				 *
				 * (? - either red or black, {} - extra black)
				 *    P?                  {P?}
				 *   / \                  / \
				 * {X}  S   -->          X   s
				 *     / \                  / \
				 *    L   R                L   R
				 */
				sibling->red = 1;
				extra = parent;
				/*
				 * If parent is red, it becomes red-and-black.
				 * The loop then ends and the statement after
				 * the loop makes parent simply black - removes
				 * the extra black. Otherwise loop continues to
				 * push the extra black up to the root.
				 */
			} else {
				/* One or both children of black sibling are red. */
				if (!sibling->right->red) {
					/*
					 * Black sibling's right child is black,
					 * while left is red and thus not nil.
					 * Swap sibling's and its left child's
					 * colors, then rotate sibling right to
					 * make its left child new black non-nil
					 * sibling and former sibling - its new
					 * red non-nil right child.
					 *
					 * (? - either red or black, <> - sibling)
					 *   P?          P?
					 *  / \         / \
					 * X  <S>  --> X  <L>
					 *    / \         / \
					 *   l   R       1   s
					 *  / \             / \
					 * 1   2           2   R
					 */
					sibling->red = 1;
					sibling->left->red = 0;
					rb_rotate_right(tree, sibling);
					sibling = parent->right;
				}

				/* Black sibling's right child is red. Parent
				 * and left child can be any color. Swap sibling's
				 * and parent's colors, then rotate parent left to
				 * become sibling's left black child and thus take
				 * the extra black from doubly black - make it simply
				 * black. Make sibling's red right child black to
				 * compensate for the loss of black parent. Left
				 * sibling's child retains black parent. Thus
				 * black heights are left unchanged.
				 *
				 * (? - either red or black, {} - extra black)
				 *    P?            S?
				 *   / \           / \
				 * {X}  S   -->   P   R
				 *     / \       / \
				 *    L?  r     X   L?
				 */
				sibling->red = parent->red;
				parent->red = 0;
				sibling->right->red = 0;
				rb_rotate_left(tree, parent);
				/* There is no extra black, so finish. */
				break;
			}
		} else {
			/* Same as above, but with left and right exchanged. */
			struct rb_node *sibling = parent->left;
			if (sibling->red) {
				sibling->red = 0;
				parent->red = 1;
				rb_rotate_right(tree, parent);
				sibling = parent->left;
			}

			if (!sibling->left->red && !sibling->right->red) {
				sibling->red = 1;
				extra = parent;
			} else {
				if (!sibling->left->red) {
					sibling->red = 1;
					sibling->right->red = 0;
					rb_rotate_left(tree, sibling);
					sibling = parent->left;
				}

				sibling->red = parent->red;
				parent->red = 0;
				sibling->left->red = 0;
				rb_rotate_right(tree, parent);
				break;
			}
		}
	}
	extra->red = 0;
}

static void rb_preorder(struct rb_node *node, void (*fn)(struct rb_node *))
{
	if (RB_NIL(node))
		return;
	/* fn may be free(3), so get left and right before calling it */
	struct rb_node *l = node->left;
	struct rb_node *r = node->right;
	fn(node);
	if (!RB_NIL(l))
		rb_preorder(l, fn);
	if (!RB_NIL(r))
		rb_preorder(r, fn);
}

struct num {
	struct rb_node node;
	long val;
};

static void gen_dot(struct rb_node *node)
{
	long num = ((struct num *)node)->val;
	unsigned long long nid = (unsigned long long)node;
	printf("N%llx [label=%ld color=%s]\n", nid, num, node->red ? "red" : "black");

	if (!RB_NIL(node->left) || !RB_NIL(node->right)) {
		unsigned long long lid = !RB_NIL(node->left) ?
			(unsigned long long)node->left :
			(unsigned long long)&node->left;
		unsigned long long rid = !RB_NIL(node->right) ?
			(unsigned long long)node->right :
			(unsigned long long)&node->right;
		printf("N%llx -- N%llx\n", nid, lid);
		printf("N%llx -- N%llx\n", nid, rid);
		if (RB_NIL(node->left))
			printf("N%llx [shape=point]\n", lid);
		if (RB_NIL(node->right))
			printf("N%llx [shape=point]\n", rid);
	}
}

static void print_dot(struct rb_node *node, char *argv[], bool wait_cmd)
{
	int saved_stdout;
	if (argv[0]) {
		int pipefd[2];
		if (pipe(pipefd) < 0) {
			perror("pipe2");
			exit(1);
		}

		struct sigaction sa = {
			.sa_handler = wait_cmd ? SIG_DFL : SIG_IGN,
		};
		if (sigaction(SIGCHLD, &sa, NULL) < 0) {
			perror("sigaction");
			exit(1);
		}

		switch (fork()) {
		case -1:
			perror("fork");
			exit(1);
			break;
		case 0:
			if(dup2(pipefd[0], 0) < 0) {
				perror("dup2");
				exit(1);
			}
			close(pipefd[0]);
			close(pipefd[1]);
			if (execvp(argv[0], argv) < 0)
				perror("execvp");
			exit(1);
			break;
		}

		if ((saved_stdout = dup(1)) < 0) {
			perror("dup");
			exit(1);
		}

		if (dup2(pipefd[1], 1) < 0) {
			perror("dup2");
			exit(1);
		}
		close(pipefd[0]);
		close(pipefd[1]);
	}

	printf("graph {\n");
	printf("node [fontcolor=cyan style=filled]\n");
	rb_preorder(node, gen_dot);
	printf("}\n");

	if (argv[0]) {
		fflush(stdout);
		close(1); /* send eof by closing write end of pipe */
		if (wait_cmd) {
			while (wait(NULL) < 0) {
				if (errno == EINTR)
					continue;
				perror("wait");
				exit(1);
			}
		}
		if (dup2(saved_stdout, 1) < 0) {
			perror("dup2");
			exit(1);
		}
		close(saved_stdout);
	}
}

static int intcmp(const struct rb_node *new, const struct rb_node *old)
{
	long n = ((const struct num *)new)->val;
	long o = ((const struct num *)old)->val;
	return n < o ? -1 : (n == o ? 0 : 1);
}

static long read_long()
{
	long val = 0;
	char *line = NULL;
	size_t n = 0;
	for (;;) {
		fprintf(stderr, "Number: ");
		fflush(stderr);
		ssize_t r = getline(&line, &n, stdin);
		if (r <= 0) {
			if (feof(stdin))
				break;
			perror("getline");
			exit(1);
		}
		if (line[r - 1] == '\n')
			line[--r] = '\0';
		if (!r)
			continue;

		errno = 0;
		char *endptr;
		val = strtol(line, &endptr, 10);
		if (errno != 0 || *endptr != '\0')
			continue;
		break;
	}
	free(line);
	return val;
}

int main(int argc, char *argv[])
{
	struct rb_tree tree;
	rb_init(&tree);

	char *line = NULL;
	size_t n = 0;
	for (;;) {
		fprintf(stderr, "Command [i-insert, d-delete, p-print, none-finish]: ");
		fflush(stderr);
		ssize_t r = getline(&line, &n, stdin);
		if (r <= 0) {
			if (feof(stdin))
				break;
			perror("getline");
			exit(1);
		}
		if (line[r - 1] == '\n')
			line[--r] = '\0';
		if (!r)
			break;
		if (r > 1)
			continue;

		char cmd = line[0];
		if (cmd == 'i') {
			long val = read_long();
			if (feof(stdin))
				break;
			struct num *num = calloc(1, sizeof(*num));
			if (!num) {
				perror("calloc");
				exit(1);
			}
			num->val = val;
			rb_insert(&tree, &num->node, intcmp);
		} else if (cmd == 'd') {
			long val = read_long();
			if (feof(stdin))
				break;
			struct num dummy = {
				.val = val
			};
			struct rb_node *node = rb_find(&tree, &dummy.node, intcmp);
			if (node) {
				rb_delete(&tree, node);
				free(node);
			} else
				fprintf(stderr, "Not found\n");
		} else if (cmd == 'p')
			print_dot(tree.root, &argv[1], false);
	}
	free(line);
	rb_preorder(tree.root, (void (*)(struct rb_node *))free);
	return 0;
}
