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

	float avg = (float)N / tbl.nbuckets;
	float dev = 0;
	for (int i = 0; i < tbl.nbuckets; ++i) {
		int n = 0;
		struct hashobj *o = tbl.buckets[i];
		while (o) {
			++n;
			o = o->next;
		}
		dev += fabsf(avg - n);
	}
	printf("random number test: %zu numbers in %zu buckets\n", N, tbl.nbuckets);
	printf("avg bucket (aka load factor) %.2f\n", avg);
	printf("avg bucket deviation %.2f\n", dev / tbl.nbuckets);
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

	int N = 0;
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
			size_t len = match.rm_eo - match.rm_so;
			char *key = s + match.rm_so;
			key[len] = '\0';
			void *data = hashtable_get(&tbl, key);
			if (data)
				cnt = (size_t)data + 1;
			else {
				++N;
				void *k = malloc(len + 1);
				if (!k) {
					perror("malloc");
					exit(1);
				}
				memcpy(k, key, len + 1);
				key = k;
			}
			r = hashtable_set(&tbl, key, (void *)cnt);
			if (r < 0) {
				fprintf(stderr, "hashtable_set: %s\n", strerror(-r));
				exit(1);
			}
			s += match.rm_eo + 1;
		}
	}

	float avg = (float)N / tbl.nbuckets;
	float dev = 0;
	for (int i = 0; i < tbl.nbuckets; ++i) {
		int n = 0;
		struct hashobj *o = tbl.buckets[i];
		while (o) {
			++n;
			/* printf("%d: %s %zu\n", i, (char *)o->key, (size_t)o->data); */
			o = o->next;
		}
		/* printf("bucket %d: %d keys\n", i, n); */
		dev += fabsf(avg - n);
	}
	printf("string test: %d strings in %zu buckets\n", N, tbl.nbuckets);
	printf("avg bucket (aka load factor) %.2f\n", avg);
	printf("avg bucket deviation %.2f\n", dev / tbl.nbuckets);
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
	return 0;
}
