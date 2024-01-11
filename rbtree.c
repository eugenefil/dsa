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
				 * (uppercase is black, [] denote current node)
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

static void rb_preorder(struct rb_node *node, void (*fn)(struct rb_node *))
{
	if (RB_NIL(node))
		return;
	fn(node);
	if (!RB_NIL(node->left))
		rb_preorder(node->left, fn);
	if (!RB_NIL(node->right))
		rb_preorder(node->right, fn);
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

int intcmp(const struct rb_node *new, const struct rb_node *old)
{
	long n = ((const struct num *)new)->val;
	long o = ((const struct num *)old)->val;
	return n < o ? -1 : (n == o ? 0 : 1);
}

int main(int argc, char *argv[])
{
	struct rb_tree tree;
	rb_init(&tree);

	char *line = NULL;
	size_t n = 0;
	for (;;) {
		fprintf(stderr, "Enter number (none to finish): ");
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

		errno = 0;
		char *endptr;
		long val = strtol(line, &endptr, 10);
		if (errno != 0 || *endptr != '\0')
			continue;

		struct num *num = calloc(1, sizeof(*num));
		if (!num) {
			perror("calloc");
			exit(1);
		}
		num->val = val;
		rb_insert(&tree, &num->node, intcmp);
		print_dot(tree.root, &argv[1], false);
	}
	free(line);
	return 0;
}
