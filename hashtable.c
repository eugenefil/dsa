#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <regex.h>

static int verbose;
const char *argv0;

struct hashobj {
	struct hashobj *next;
	const void *key;
	unsigned hash;
	void *data;
};

struct hashtable {
	struct hashobj **buckets;
	size_t nbuckets;
	size_t nkeys;
	unsigned (*hash)(const void *key);
	int (*cmp)(const void *key1, const void *key2);
};

static int hashtable_init(struct hashtable *tbl, size_t nbuckets,
			  unsigned (*hash)(const void *key),
			  int (*cmp)(const void *key1, const void *key2))
{
	tbl->nbuckets = nbuckets;
	tbl->buckets = calloc(tbl->nbuckets, sizeof(*tbl->buckets));
	if (!tbl->buckets)
		return -ENOMEM;
	tbl->hash = hash;
	tbl->cmp = cmp;
	return 0;
}

static void hashtable_free(struct hashtable *tbl)
{
	for (int i = 0; i < tbl->nbuckets; ++i) {
		struct hashobj *o = tbl->buckets[i];
		while (o) {
			struct hashobj *next = o->next;
			free(o);
			o = next;
		}
	}
	free(tbl->buckets);
}

static int hashtable_grow(struct hashtable *tbl)
{
	size_t nbuckets = tbl->nbuckets * 2;
	struct hashobj **buckets = calloc(nbuckets, sizeof(*tbl->buckets));
	if (!buckets)
		return -ENOMEM;
	for (int i = 0; i < tbl->nbuckets; ++i) {
		struct hashobj *o = tbl->buckets[i];
		while (o) {
			struct hashobj *next = o->next;
			int j = o->hash % nbuckets;
			o->next = buckets[j];
			buckets[j] = o;
			o = next;
		}
	}
	free(tbl->buckets);
	tbl->buckets = buckets;
	tbl->nbuckets = nbuckets;
	return 0;
}

static int hashtable_set(struct hashtable *tbl, const void *key, void *data)
{
	unsigned hash = tbl->hash(key);
	size_t i = hash % tbl->nbuckets;
	struct hashobj *o = tbl->buckets[i];
	while (o) {
		if (!tbl->cmp(o->key, key)) {
			o->data = data;
			return 0;
		}
		o = o->next;
	}

	o = calloc(1, sizeof(*o));
	if (!o)
		return -ENOMEM;
	++tbl->nkeys;
	o->key = key;
	o->hash = hash;
	o->data = data;
	o->next = tbl->buckets[i];
	tbl->buckets[i] = o;
	
	if (tbl->nkeys * 100 / tbl->nbuckets > 110)
		hashtable_grow(tbl);
	return 0;
}

static void *hashtable_get(struct hashtable *tbl, const void *key)
{
	size_t i = tbl->hash(key) % tbl->nbuckets;
	struct hashobj *o = tbl->buckets[i];
	while (o) {
		if (!tbl->cmp(o->key, key))
			return o->data;
		o = o->next;
	}
	return NULL;
}

static void *hashtable_del(struct hashtable *tbl, const void **key)
{
	size_t i = tbl->hash(*key) % tbl->nbuckets;
	struct hashobj *o = tbl->buckets[i];
	struct hashobj *prev = NULL;
	while (o) {
		if (!tbl->cmp(o->key, key)) {
			void *data = o->data;
			*key = o->key;
			if (prev)
				prev->next = o->next;
			else
				tbl->buckets[i] = o->next;
			free(o);
			--tbl->nkeys;
			return data;
		}
		prev = o;
		o = o->next;
	}
	return NULL;
}

static void hashtable_iterate(struct hashtable *tbl,
			      void (*fn)(const void *key, void *data))
{
	for (int i = 0; i < tbl->nbuckets; ++i) {
		struct hashobj *o = tbl->buckets[i];
		while (o) {
			fn(o->key, o->data);
			o = o->next;
		}
	}
}

static unsigned fnv1a_32(const void *data, size_t size)
{
	/* https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function */
	unsigned hash = 0x811c9dc5U;
	for (; size; --size)
		hash = (hash ^ *(unsigned char *)data++) * 0x01000193U;
	return hash;
}

static unsigned strhash(const void *key)
{
	return fnv1a_32(key, strlen(key));
}

static int _strcmp(const void *key1, const void *key2)
{
	return strcmp(key1, key2);
}

static unsigned numhash(const void *key)
{
	return (unsigned long long)key;
}

static int numcmp(const void *key1, const void *key2)
{
	return !(key1 == key2);
}

static void print_stats(struct hashtable *tbl,
			void (*printkey)(const void *key, void *data))
{
	float avg = (float)tbl->nkeys / tbl->nbuckets;
	float dev = 0;
	for (int i = 0; i < tbl->nbuckets; ++i) {
		int n = 0;
		struct hashobj *o = tbl->buckets[i];
		while (o) {
			++n;
			if (verbose) {
				printf("%d: hash=%u ", i, o->hash);
				printkey(o->key, o->data);
			}
			o = o->next;
		}
		if (verbose)
			printf("bucket %d: %d keys\n", i, n);
		dev += fabsf(avg - n);
	}
	printf("%zu key(s) in %zu buckets\n", tbl->nkeys, tbl->nbuckets);
	printf("avg bucket (aka load factor) %.2f\n", avg);
	printf("avg bucket deviation %.2f\n", dev / tbl->nbuckets);
}

static void print_num(const void *key, void *data)
{
	printf("%ld\n", (long)key);
}

void numtest(size_t N, size_t B)
{
	struct hashtable tbl = { 0 };
	int r = hashtable_init(&tbl, B, numhash, numcmp);
	if (r < 0) {
		fprintf(stderr, "hashtable_init: %s\n", strerror(-r));
		exit(1);
	}

	srandom(time(NULL));
	for (int i = 0; i < N; ++i) {
		long num = random();
		r = hashtable_set(&tbl, (void *)num, (void *)1);
		if (r < 0) {
			fprintf(stderr, "hashtable_set: %s\n", strerror(-r));
			exit(1);
		}
	}

	printf("random number test:\n");
	print_stats(&tbl, print_num);
	hashtable_free(&tbl);
}

static void free_key(const void *key, void *data)
{
	free((void *)key);
}

static void print_str(const void *key, void *data)
{
	printf("%s %zu\n", (const char *)key, (size_t)data);
}

/* Pipe something like `curl https://en.wikipedia.org/wiki/List_of_2000s_films_based_on_actual_events` */
void strtest(size_t B)
{
	struct hashtable tbl = { 0 };
	int r = hashtable_init(&tbl, B, strhash, _strcmp);
	if (r < 0) {
		fprintf(stderr, "hashtable_init: %s\n", strerror(-r));
		exit(1);
	}

	regex_t regex;
	r = regcomp(&regex, "[[:alpha:]][[:alnum:]_-]+", REG_EXTENDED);
	if (r) {
		fprintf(stderr, "error compiling regex\n");
		exit(1);
	}

	char buf[4096];
	for (;;) {
		size_t n = fread(buf, 1, sizeof(buf) - 1, stdin);
		if (!n) {
			if (feof(stdin))
				break;
			perror("fread");
			exit(1);
		}
		buf[n] = '\0';

		char *s = buf;
		regmatch_t match;
		while (s < &buf[n] && !regexec(&regex, s, 1, &match, 0)) {
			size_t cnt = 1;
			char *key = s + match.rm_so;
			key[match.rm_eo - match.rm_so] = '\0';
			void *data = hashtable_get(&tbl, key);
			if (data)
				cnt = (size_t)data + 1;
			else {
				key = strdup(key);
				if (!key) {
					perror("strdup");
					exit(1);
				}
			}
			r = hashtable_set(&tbl, key, (void *)cnt);
			if (r < 0) {
				fprintf(stderr, "hashtable_set: %s\n", strerror(-r));
				exit(1);
			}
			s += match.rm_eo + 1;
		}
	}

	printf("string test:\n");
	print_stats(&tbl, print_str);
	hashtable_iterate(&tbl, free_key);
	hashtable_free(&tbl);
}

void usage()
{
	fprintf(stderr, "\
Usage: %s [-v] [numtest N B | strtest B]\n\
Populate hash table and print its statistics.\n\
\n\
  numtest N B    test by hashing N random numbers into B buckets\n\
  strtest B      test by hashing identifiers from stdin into B buckets\n\
  -v             be verbose\n\
\n\
Note, table may grow beyond B buckets.\n", argv0);
}

unsigned long parse_ulong_arg(char *arg)
{
	if (!arg || !*arg) {
		usage();
		exit(1);
	}
	char *endptr;
	errno = 0;
	unsigned long n = strtoul(arg, &endptr, 10);
	if (errno != 0 || *endptr != '\0' || n == 0) {
		usage();
		exit(1);
	}
	return n;
}

void print_keyval(const void *key, void *data)
{
	printf("%s=%s\n", (const char *)key, (char *)data);
}

void free_keyval(const void *key, void *data)
{
	free((void *)key);
	free(data);
}

int main(int argc, char *argv[])
{
	argv0 = argv[0];
	char **arg = &argv[1];
	while (*arg) {
		if (!strcmp(*arg, "numtest")) {
			unsigned long N = parse_ulong_arg(*++arg);
			unsigned long B = parse_ulong_arg(*++arg);
			numtest(N, B);
			exit(0);
		} else if (!strcmp(*arg, "strtest")) {
			unsigned long B = parse_ulong_arg(*++arg);
			strtest(B);
			exit(0);
		} else if (!strcmp(*arg, "-h")) {
			usage();
			exit(0);
		} else if (!strcmp(*arg, "-v"))
			verbose = 1;
		else {
			usage();
			exit(1);
		}
		++arg;
	}

	struct hashtable tbl = { 0 };
	int r = hashtable_init(&tbl, 4, strhash, _strcmp);
	if (r < 0) {
		fprintf(stderr, "hashtable_init: %s\n", strerror(-r));
		exit(1);
	}

	for (;;) {
		printf("Add key (none to finish): ");
		fflush(stdout);
		char *key = NULL;
		size_t n = 0;
		ssize_t r = getline(&key, &n, stdin);
		if (r <= 0) {
			free(key);
			if (feof(stdin))
				break;
			perror("getline");
			exit(1);
		}
		if (r > 0 && key[r - 1] == '\n')
			key[--r] = '\0';
		if (!r) {
			free(key);
			break;
		}

		char *val = NULL;
		n = 0;
		r = 0;
		while (!r) {
			printf("Value: ");
			fflush(stdout);
			r = getline(&val, &n, stdin);
			if (r <= 0) {
				free(key);
				free(val);
				if (feof(stdin))
					break;
				perror("getline");
				exit(1);
			}
			if (r > 0 && val[r - 1] == '\n')
				val[--r] = '\0';
		}

		void *old = hashtable_get(&tbl, key);
		r = hashtable_set(&tbl, key, val);
		if (r < 0) {
			fprintf(stderr, "hashtable_set: %s\n", strerror(-r));
			exit(1);
		}
		if (old) {
			free(old);
			free(key); /* old key was kept */
		}

		print_stats(&tbl, print_keyval);
		printf("************************************\n");
	}
	puts("");
	print_stats(&tbl, print_keyval);

	hashtable_iterate(&tbl, free_keyval);
	hashtable_free(&tbl);
	return 0;
}
