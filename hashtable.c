#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

struct hashobj {
	struct hashobj *next;
	const void *key;
	void *data;
};

struct hashtable {
	struct hashobj **buckets;
	size_t nbuckets;
	unsigned (*hash)(const void *key);
	int (*cmp)(const void *key1, const void *key2);
};

static int hashtable_init(struct hashtable *tbl, size_t size,
			  unsigned (*hash)(const void *key),
			  int (*cmp)(const void *key1, const void *key2))
{
	tbl->nbuckets = size;
	tbl->buckets = calloc(tbl->nbuckets, sizeof(*tbl->buckets));
	if (!tbl->buckets)
		return -ENOMEM;
	tbl->hash = hash;
	tbl->cmp = cmp;
	return 0;
}

static int hashtable_put(struct hashtable *tbl, const void *key, void *data)
{
	size_t i = tbl->hash(key) % tbl->nbuckets;
	struct hashobj *o = tbl->buckets[i];
	struct hashobj *prev = NULL;
	while (o) {
		prev = o;
		if (!tbl->cmp(o->key, key))
			return -EEXIST;
		o = o->next;
	}

	o = calloc(1, sizeof(*o));
	if (!o)
		return -ENOMEM;
	o->key = key;
	o->data = data;
	if (prev)
		prev->next = o;
	else
		tbl->buckets[i] = o;
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

int main()
{
	int N = 512;
	int nbuckets = 256;
	float avg = (float)N / nbuckets;
	float dev = 0;
	struct hashtable tbl = { 0 };
	int r = hashtable_init(&tbl, nbuckets, numhash, numcmp);
	if (r < 0) {
		perror("hashtable_init");
		exit(1);
	}
	srandom(time(NULL));
	for (int i = 0; i < N; ++i) {
		long num = random();
		r = hashtable_put(&tbl, (void *)num, (void *)1);
		if (r < 0) {
			if (r == -EEXIST)
				printf("%ld already exists\n", num);
			else {
				perror("hashtable_put");
				exit(1);
			}
		}
	}
	for (int i = 0; i < tbl.nbuckets; ++i) {
		int n = 0;
		struct hashobj *o = tbl.buckets[i];
		while (o) {
			++n;
			o = o->next;
		}
		dev += fabsf(avg - n);
	}
	printf("avg bucket (aka load factor) %.2f\n", avg);
	printf("avg bucket deviation %.2f\n", dev / nbuckets);
	return 0;
}
