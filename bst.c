#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

/* see https://en.wikipedia.org/wiki/Binary_search_tree */

struct bst_node {
	struct bst_node *parent;
	struct bst_node *left;
	struct bst_node *right;
};

struct bst_root {
	struct bst_node *node;
};

static void bst_insert(struct bst_root *root, struct bst_node *node,
		       int (*cmp)(struct bst_node *new, struct bst_node *old))
{
	struct bst_node *n = root->node;
	struct bst_node *parent = NULL;
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
}

static struct bst_node *bst_find(struct bst_root *root, struct bst_node *node,
				 int (*cmp)(struct bst_node *n1, struct bst_node *n2))
{
	struct bst_node *n = root->node;
	while (n) {
		int r = cmp(node, n);
		if (r == 0)
			break;
		else if (r < 0)
			n = n->left;
		else
			n = n->right;
	}
	return n;
}

static void bst_shift(struct bst_root *root, struct bst_node *what,
		      struct bst_node *with)
{
	if (!what->parent)
		root->node = with;
	else if (what->parent->left == what)
		what->parent->left = with;
	else
		what->parent->right = with;
	if (with)
		with->parent = what->parent;
}

static void bst_delete(struct bst_root *root, struct bst_node *node)
{
	if (!node->left)
		bst_shift(root, node, node->right);
	else if (!node->right)
		bst_shift(root, node, node->left);
	else {
		/* replace node with the next lowest after it like so:
		 * (note, next lowest has no left, b/c it _is_ the lowest)
		 * - if next lowest is node's right, attach node's left to it
		 * - if next lowest is below node's right, detach it from its
		 *   place by replacing with its right, then attach node's
		 *   right and left to it
		 */

		/* find next lowest after node */
		struct bst_node *next = node->right;
		while (next->left)
			next = next->left;

		if (next != node->right) {
			/* next is somewhere below node's right - detach
			 * it from there by replacing it with its right
			 */
			bst_shift(root, next, next->right);
			/* now next has no right - attach node's right to it */
			next->right = node->right;
			node->right->parent = next;
		}
		/* replace node with next */
		bst_shift(root, node, next);
		/* attach node's left to next's left */
		next->left = node->left;
		node->left->parent = next;
	}
}

static void _bst_preorder(struct bst_node *node,
			  void (*fn)(struct bst_node *node))
{
	struct bst_node *left, *right;
	if (!node)
		return;
	left = node->left;
	right = node->right;
	fn(node);
	_bst_preorder(left, fn);
	_bst_preorder(right, fn);
}

static void bst_preorder(struct bst_root *root,
			 void (*fn)(struct bst_node *node))
{
	_bst_preorder(root->node, fn);
}

struct num {
	struct bst_node node;
	long val;
};

static int num_cmp(struct bst_node *new, struct bst_node *old)
{
	long n = ((struct num *)new)->val;
	long o = ((struct num *)old)->val;
	return n < o ? -1 : (n == o ? 0 : 1);
}

static void gen_dot(struct bst_node *node)
{
	unsigned long long nid, lid, rid;
	nid = (unsigned long long)node;
	lid = node->left ? (unsigned long long)node->left :
		(unsigned long long)&node->left;
	rid = node->right ? (unsigned long long)node->right :
		(unsigned long long)&node->right;
	printf("%llx [label=%ld]\n", nid, ((struct num *)node)->val);
	printf("%llx -- %llx\n", nid, lid);
	printf("%llx -- %llx\n", nid, rid);
	if (!node->left)
		printf("%llx [shape=point]\n", lid);
	if (!node->right)
		printf("%llx [shape=point]\n", rid);
}

/**
 * print_dot - output graphviz dot file for bst to stdout
 *
 * Pipe output to e.g. "dot -Tx11" to see generated graph.
 */
static void print_dot(struct bst_root *root, char *argv[])
{
	int saved_stdout;

	if (argv[0]) {
		pid_t child;
		int pipefd[2];
		int r = pipe2(pipefd, O_CLOEXEC);
		if (r < 0) {
			perror("pipe2");
			exit(1);
		}
		child = fork();
		if (child < 0) {
			perror("fork");
			exit(1);
		} else if (child == 0) {
			if (dup2(pipefd[0], 0) < 0) {
				perror("child: dup2");
				exit(1);
			}
			if (execvp(argv[0], argv) < 0) {
				perror("child execvp");
				exit(1);
			}
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
	bst_preorder(root, gen_dot);
	printf("}\n");

	if (argv[0]) {
		/* close stdout to send eof to other end of pipe */
		fflush(stdout);
		if (dup2(saved_stdout, 1) < 0) {
			perror("dup2");
			exit(1);
		}
		close(saved_stdout);
	}
}

int main(int argc, char *argv[])
{
	char *line = NULL;
	size_t n = 0;
	ssize_t r;
	long val;
	char *endptr;
	struct num *num;
	struct bst_root root = { 0 };

	if (argc == 2 && !strcmp(argv[1], "-h")) {
		printf("\
Usage: %s [CMD [ARGS]...]\n\
Populate binary tree, print its graphviz dot script to stdout.\n\
If given, run CMD with ARGS and pipe dot script to it.\n", argv[0]);
		exit(0);
	}

	for (;;) {

		fprintf(stderr, "Add number (none to finish): ");
		r = getline(&line, &n, stdin);
		if (r < 0) {
			if (feof(stdin))
				break;
			perror("getline");
			exit(1);
		}
		if (r > 0 && line[r - 1] == '\n')
			line[--r] = '\0';
		if (r == 0)
			break;

		errno = 0;
		val = strtol(line, &endptr, 10);
		if (errno != 0 || *endptr != '\0')
			continue;
		
		num = calloc(1, sizeof(*num));
		if (!num) {
			perror("calloc");
			exit(1);
		}
		num->val = val;
		bst_insert(&root, &num->node, num_cmp);
		print_dot(&root, &argv[1]);
	}

	for (;;) {
		struct num dummy = { 0 };

		fprintf(stderr, "Delete number (none to finish): ");
		r = getline(&line, &n, stdin);
		if (r < 0) {
			if (feof(stdin))
				break;
			perror("getline");
			exit(1);
		}
		if (r > 0 && line[r - 1] == '\n')
			line[--r] = '\0';
		if (r == 0)
			break;

		errno = 0;
		dummy.val = strtol(line, &endptr, 10);
		if (errno < 0 || *endptr != '\0')
			continue;
		
		num = (struct num *)bst_find(&root, &dummy.node, num_cmp);
		if (!num) {
			fprintf(stderr, "Number not found\n");
			continue;
		}
		bst_delete(&root, &num->node);
		free(num);
		print_dot(&root, &argv[1]);
	}
	bst_preorder(&root, (void (*)(struct bst_node *))free);
	return 0;
}
