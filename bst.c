#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

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

static void traverse(struct bst_node *node)
{
	unsigned long long nid, lid, rid;
	if (!node)
		return;
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
	traverse(node->left);
	traverse(node->right);
}

/**
 * print_dot - output graphviz dot file for bst to stdout
 *
 * Pipe output to e.g. "dot -Tx11" to see generated graph.
 */
static void print_dot(struct bst_root *root)
{
	printf("graph {\n");
	traverse(root->node);
	printf("}\n");
}

int main()
{
	struct bst_root root = { 0 };

	for (;;) {
		char *line = NULL;
		size_t n = 0;
		ssize_t r;
		long val;
		char *endptr;
		struct num *num;

		fprintf(stderr, "Enter a number (none to finish): ");
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
	}

	print_dot(&root);
	return 0;
}
