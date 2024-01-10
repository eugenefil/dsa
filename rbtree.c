#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <signal.h>

struct rb_node {
	struct rb_node *parent;
	struct rb_node *left;
	struct rb_node *right;
	int red;
};

struct rb_root {
	struct rb_node *node;
};

static void rb_rotate_left(struct rb_root *root, struct rb_node *node)
{
	struct rb_node *c = node->right;
	node->right = c->left;
	if (c->left)
		c->left->parent = node;

	if (!node->parent)
		root->node = c;
	else if (node == node->parent->left)
		node->parent->left = c;
	else
		node->parent->right = c;
	c->parent = node->parent;

	c->left = node;
	node->parent = c;
}

static void rb_rotate_right(struct rb_root *root, struct rb_node *node)
{
	struct rb_node *c = node->left;
	node->left = c->right;
	if (c->right)
		c->right->parent = node;

	if (!node->parent)
		root->node = c;
	else if (node == node->parent->left)
		node->parent->left = c;
	else
		node->parent->right = c;
	c->parent = node->parent;

	c->right = node;
	node->parent = c;
}

static void rb_insert(struct rb_root *root, struct rb_node *node,
		      int (*cmp)(const struct rb_node *new, const struct rb_node *old))
{
	struct rb_node *parent = NULL;
	struct rb_node *n = root->node;
	int ret;
	while (n) {
		parent = n;
		ret = cmp(node, n);
		if (ret < 0)
			n = n->left;
		else
			n = n->right;
	}
	node->parent = parent;
	if (!parent)
		root->node = node;
	else if (ret < 0)
		parent->left = node;
	else
		parent->right = node;

	node->red = 1;
	while (node->parent && node->parent->red) {
		/* Here node can't be root, b/c root has no parent.
		 * Also can't be root's child, b/c root is black.
		 * So we're at least root's grandchild.
		 */
		struct rb_node *parent = node->parent;
		struct rb_node *grand = parent->parent;
		if (parent == grand->left) {
			struct rb_node *uncle = grand->right;
			if (uncle && uncle->red) {
				/* Both parent and uncle are red. This
				 * means grandparent is black. Swap colors:
				 * make siblings black and grandparent red.
				 * Continue tree checking with grandparent.
				 */
				parent->red = 0;
				uncle->red = 0;
				grand->red = 1;
				node = grand;
			} else {
				/* If node is to the right of parent, rotate
				 * parent left to become node's left child.
				 * I.e. move red-red conflict to the left.
				 * If kept as is (on the right), the conflict
				 * would remain after rotating grandparent
				 * right below: red node will be red
				 * grandparent's left child. Make parent
				 * (i.e. lower node) new current node.
				 */
				if (node == parent->right) {
					rb_rotate_left(root, parent);
					node = parent;
				}
				/* Here node is to the left of parent,
				 * either b/c of left rotation above or b/c
				 * insertion happens on the left. Make parent
				 * black, grandparent red and rotate
				 * grandparent right to remove one tree level:
				 * red grandparent becomes right child of
				 * black parent. Loop ends after that.
				 */
				/* node could have been changed above */
				node->parent->red = 0;
				grand->red = 1;
				rb_rotate_right(root, grand);
			}
		} else {
			/* Same as above, but with left and right exchanged. */
			struct rb_node *uncle = grand->left;
			if (uncle && uncle->red) {
				parent->red = 0;
				uncle->red = 0;
				grand->red = 1;
				node = grand;
			} else {
				if (node == parent->left) {
					rb_rotate_right(root, parent);
					node = parent;
				}
				node->parent->red = 0;
				grand->red = 1;
				rb_rotate_left(root, grand);
			}
		}
	}
	root->node->red = 0;
}

static void rb_preorder(struct rb_node *node, void (*fn)(struct rb_node *))
{
	if (!node)
		return;
	fn(node);
	if (node->left)
		rb_preorder(node->left, fn);
	if (node->right)
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

	if (node->left || node->right) {
		unsigned long long lid = node->left ?
			(unsigned long long)node->left :
			(unsigned long long)&node->left;
		unsigned long long rid = node->right ?
			(unsigned long long)node->right :
			(unsigned long long)&node->right;
		printf("N%llx -- N%llx\n", nid, lid);
		printf("N%llx -- N%llx\n", nid, rid);
		if (!node->left)
			printf("N%llx [shape=point]\n", lid);
		if (!node->right)
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
	struct rb_root root = { 0 };
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
		rb_insert(&root, &num->node, intcmp);
		print_dot(root.node, &argv[1], false);
	}
	free(line);
	return 0;
}
