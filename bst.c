#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <math.h>

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

static void bst_preorder(struct bst_node *node,
			 void (*fn)(struct bst_node *node))
{
	struct bst_node *left, *right;
	if (!node)
		return;
	left = node->left;
	right = node->right;
	fn(node);
	bst_preorder(left, fn);
	bst_preorder(right, fn);
}

static int _bst_height(struct bst_node *node, int h)
{
	int l, r;
	if (!node)
		return h;
	l = r = ++h;
	if (node->left)
		l = _bst_height(node->left, h);
	if (node->right)
		r = _bst_height(node->right, h);
	return l > r ? l : r;
}

static int bst_height(struct bst_node *node)
{
	return _bst_height(node, 0);
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
static void print_dot(struct bst_node *node, char *argv[], bool wait_cmd)
{
	int saved_stdout;

	if (argv[0]) {
		pid_t child;
		struct sigaction sa = { 0 };
		int pipefd[2];
		int r = pipe2(pipefd, O_CLOEXEC);
		if (r < 0) {
			perror("pipe2");
			exit(1);
		}

		if (wait_cmd)
			sa.sa_handler = SIG_DFL;
		else
			sa.sa_handler = SIG_IGN;
		r = sigaction(SIGCHLD, &sa, NULL);
		if (r < 0) {
			perror("sigaction");
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
	bst_preorder(node, gen_dot);
	printf("}\n");

	if (argv[0]) {
		/* close stdout to send eof to other end of pipe */
		fflush(stdout);
		if (dup2(saved_stdout, 1) < 0) {
			perror("dup2");
			exit(1);
		}
		close(saved_stdout);

		if (wait_cmd) {
			while (wait(NULL) < 0) {
				if (errno != EINTR) {
					perror("wait");
					exit(1);
				}
			}
		}
	}
}

int intcmp(const void *p1, const void *p2)
{
	int n1 = *(int *)p1;
	int n2 = *(int *)p2;
	return n1 < n2 ? -1 : (n1 == n2 ? 0 : 1);
}

#define N 1000
#define SIZE 1024
static void test(int n, int bst_size, char *argv[])
{
	int *h, H;
	int nH = 0;
	struct num *nums;

	if (n <= 0)
		n = N;
	if (bst_size <= 0)
		bst_size = SIZE;
	printf("build N=%d random trees of n=%d nodes\n", n, bst_size);

	h = calloc(n, sizeof(int));
	if (!h) {
		perror("calloc");
		exit(0);
	}

	nums = calloc(bst_size, sizeof(struct num));
	if (!nums) {
		perror("calloc");
		free(h);
		exit(0);
	}

	srandom(time(NULL));
	for (int i = 0; i < n; ++i) {
		int nh = 0;
		struct bst_root root = { 0 };
		memset(nums, 0, bst_size * sizeof(*nums));
		for (int j = 0; j < bst_size; ++j) {
			nums[j].val = random();
			bst_insert(&root, &nums[j].node, num_cmp);
		}
		h[i] = bst_height(root.node);
		for (int j = 0; j < bst_size; ++j) {
			struct bst_node *node = &nums[j].node;
			while (node) {
				++nh;
				node = node->parent;
			}
		}
		if (argv[0]) {
			printf("height %d\n", h[i]);
			printf("avg node height %.2f\n", (float)nh / bst_size);
			print_dot(root.node, argv, true);
		}
		nH += nh;
	}

	qsort(h, n, sizeof(int), intcmp);
	printf("min height %d\n", h[0]);
	printf("max height %d\n", h[n - 1]);
	printf("median height %d\n", h[n / 2]);
	H = 0;
	for (int i = 0; i < n; ++i)
		H += h[i];
	printf("avg height %.2f\n", (float)H / n);
	printf("avg node height %.2f\n", (float)nH / (n * bst_size));
	printf("log2(n) %.2f\n", log2f(bst_size));

	free(nums);
	free(h);
}

void usage(const char *prog)
{
	printf("\
Usage: %s [OPTION] [CMD [ARGS]...]\n\
Populate binary tree, print its graphviz dot script to stdout.\n\
If given, run CMD with ARGS and pipe dot script to it.\n\
\n\
  -test[=NUM_TREES,TREE_SIZE]    create random trees and\n\
                                 print their statistics\n", prog);
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
		usage(argv[0]);
		exit(0);
	} else if (argc >= 2 && !strncmp(argv[1], "-test", 5)) {
		unsigned long n = 0;
		unsigned long size = 0;
		if (strlen(argv[1]) > 5) {
			char *endptr;
			char *s = argv[1] + 5;
			if (*s != '=') {
				usage(argv[0]);
				exit(1);
			}
			++s;

			errno = 0;
			n = strtoul(s, &endptr, 10);
			if (errno != 0 || endptr == s || *endptr != ',') {
				usage(argv[0]);
				exit(1);
			}
			s = endptr + 1;

			errno = 0;
			size = strtoul(s, &endptr, 10);
			if (errno != 0 || endptr == s || *endptr != '\0') {
				usage(argv[0]);
				exit(1);
			}
		}
		test(n, size, &argv[2]);
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
		print_dot(root.node, &argv[1], false);
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
		print_dot(root.node, &argv[1], false);
	}
	bst_preorder(root.node, (void (*)(struct bst_node *))free);
	return 0;
}
