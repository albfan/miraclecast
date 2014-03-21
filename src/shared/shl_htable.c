/*
 * SHL - Dynamic hash-table
 *
 * Written-by: Rusty Russell <rusty@rustcorp.com.au>
 * Adjusted-by: David Herrmann <dh.herrmann@gmail.com>
 * Licensed under LGPLv2+ - see LICENSE_htable file for details
 */

/*
 * Please see ccan/htable/_info at:
 *   https://github.com/rustyrussell/ccan/tree/master/ccan/htable
 * for information on the hashtable algorithm. This file copies the code inline
 * and is released under the same conditions.
 *
 * At the end of the file you can find some helpers to use this htable to store
 * objects with "unsigned long" or "char*" keys.
 */

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "shl_htable.h"

#define COLD __attribute__((cold))

struct htable {
	/* KEEP IN SYNC WITH "struct shl_htable_int" */
	size_t (*rehash)(const void *elem, void *priv);
	void *priv;
	unsigned int bits;
	size_t elems, deleted, max, max_with_deleted;
	/* These are the bits which are the same in all pointers. */
	uintptr_t common_mask, common_bits;
	uintptr_t perfect_bit;
	uintptr_t *table;
};

#define HTABLE_INITIALIZER(name, rehash, priv)				\
	{ rehash, priv, 0, 0, 0, 0, 0, -1, 0, 0, &name.perfect_bit }

struct htable_iter {
	size_t off;
};

/*
 * INLINE COPY OF ccan/htable.c
 */

/* We use 0x1 as deleted marker. */
#define HTABLE_DELETED (0x1)

/* We clear out the bits which are always the same, and put metadata there. */
static inline uintptr_t get_extra_ptr_bits(const struct htable *ht,
					   uintptr_t e)
{
	return e & ht->common_mask;
}

static inline void *get_raw_ptr(const struct htable *ht, uintptr_t e)
{
	return (void *)((e & ~ht->common_mask) | ht->common_bits);
}

static inline uintptr_t make_hval(const struct htable *ht,
				  const void *p, uintptr_t bits)
{
	return ((uintptr_t)p & ~ht->common_mask) | bits;
}

static inline bool entry_is_valid(uintptr_t e)
{
	return e > HTABLE_DELETED;
}

static inline uintptr_t get_hash_ptr_bits(const struct htable *ht,
					  size_t hash)
{
	/* Shuffling the extra bits (as specified in mask) down the
	 * end is quite expensive.  But the lower bits are redundant, so
	 * we fold the value first. */
	return (hash ^ (hash >> ht->bits))
		& ht->common_mask & ~ht->perfect_bit;
}

static void htable_init(struct htable *ht,
			size_t (*rehash)(const void *elem, void *priv),
			void *priv)
{
	struct htable empty = HTABLE_INITIALIZER(empty, NULL, NULL);
	*ht = empty;
	ht->rehash = rehash;
	ht->priv = priv;
	ht->table = &ht->perfect_bit;
}

static void htable_clear(struct htable *ht,
			 void (*free_cb) (void *entry, void *ctx),
			 void *ctx)
{
	size_t i;

	if (ht->table != &ht->perfect_bit) {
		if (free_cb) {
			for (i = 0; i < (size_t)1 << ht->bits; ++i) {
				if (entry_is_valid(ht->table[i]))
					free_cb(get_raw_ptr(ht, ht->table[i]),
						ctx);
			}
		}

		free((void *)ht->table);
	}

	htable_init(ht, ht->rehash, ht->priv);
}

size_t shl_htable_this_or_next(struct shl_htable *htable, size_t i)
{
	struct htable *ht = (void*)&htable->htable;

	if (ht->table != &ht->perfect_bit)
		for ( ; i < (size_t)1 << ht->bits; ++i)
			if (entry_is_valid(ht->table[i]))
				return i;

	return SIZE_MAX;
}

void *shl_htable_get_entry(struct shl_htable *htable, size_t i)
{
	struct htable *ht = (void*)&htable->htable;

	if (i < (size_t)1 << ht->bits)
		if (entry_is_valid(ht->table[i]))
			return get_raw_ptr(ht, ht->table[i]);

	return NULL;
}

static void htable_visit(struct htable *ht,
			 void (*visit_cb) (void *elem, void *ctx),
			 void *ctx)
{
	size_t i;

	if (visit_cb && ht->table != &ht->perfect_bit) {
		for (i = 0; i < (size_t)1 << ht->bits; ++i) {
			if (entry_is_valid(ht->table[i]))
				visit_cb(get_raw_ptr(ht, ht->table[i]), ctx);
		}
	}
}

static size_t hash_bucket(const struct htable *ht, size_t h)
{
	return h & ((1 << ht->bits)-1);
}

static void *htable_val(const struct htable *ht,
			struct htable_iter *i, size_t hash, uintptr_t perfect)
{
	uintptr_t h2 = get_hash_ptr_bits(ht, hash) | perfect;

	while (ht->table[i->off]) {
		if (ht->table[i->off] != HTABLE_DELETED) {
			if (get_extra_ptr_bits(ht, ht->table[i->off]) == h2)
				return get_raw_ptr(ht, ht->table[i->off]);
		}
		i->off = (i->off + 1) & ((1 << ht->bits)-1);
		h2 &= ~perfect;
	}
	return NULL;
}

static void *htable_firstval(const struct htable *ht,
			     struct htable_iter *i, size_t hash)
{
	i->off = hash_bucket(ht, hash);
	return htable_val(ht, i, hash, ht->perfect_bit);
}

static void *htable_nextval(const struct htable *ht,
			    struct htable_iter *i, size_t hash)
{
	i->off = (i->off + 1) & ((1 << ht->bits)-1);
	return htable_val(ht, i, hash, 0);
}

/* This does not expand the hash table, that's up to caller. */
static void ht_add(struct htable *ht, const void *new, size_t h)
{
	size_t i;
	uintptr_t perfect = ht->perfect_bit;

	i = hash_bucket(ht, h);

	while (entry_is_valid(ht->table[i])) {
		perfect = 0;
		i = (i + 1) & ((1 << ht->bits)-1);
	}
	ht->table[i] = make_hval(ht, new, get_hash_ptr_bits(ht, h)|perfect);
}

static COLD bool double_table(struct htable *ht)
{
	unsigned int i;
	size_t oldnum = (size_t)1 << ht->bits;
	uintptr_t *oldtable, e;

	oldtable = ht->table;
	ht->table = calloc(1 << (ht->bits+1), sizeof(size_t));
	if (!ht->table) {
		ht->table = oldtable;
		return false;
	}
	ht->bits++;
	ht->max = ((size_t)3 << ht->bits) / 4;
	ht->max_with_deleted = ((size_t)9 << ht->bits) / 10;

	/* If we lost our "perfect bit", get it back now. */
	if (!ht->perfect_bit && ht->common_mask) {
		for (i = 0; i < sizeof(ht->common_mask) * CHAR_BIT; i++) {
			if (ht->common_mask & ((size_t)1 << i)) {
				ht->perfect_bit = (size_t)1 << i;
				break;
			}
		}
	}

	if (oldtable != &ht->perfect_bit) {
		for (i = 0; i < oldnum; i++) {
			if (entry_is_valid(e = oldtable[i])) {
				void *p = get_raw_ptr(ht, e);
				ht_add(ht, p, ht->rehash(p, ht->priv));
			}
		}
		free(oldtable);
	}
	ht->deleted = 0;
	return true;
}

static COLD void rehash_table(struct htable *ht)
{
	size_t start, i;
	uintptr_t e;

	/* Beware wrap cases: we need to start from first empty bucket. */
	for (start = 0; ht->table[start]; start++);

	for (i = 0; i < (size_t)1 << ht->bits; i++) {
		size_t h = (i + start) & ((1 << ht->bits)-1);
		e = ht->table[h];
		if (!e)
			continue;
		if (e == HTABLE_DELETED)
			ht->table[h] = 0;
		else if (!(e & ht->perfect_bit)) {
			void *p = get_raw_ptr(ht, e);
			ht->table[h] = 0;
			ht_add(ht, p, ht->rehash(p, ht->priv));
		}
	}
	ht->deleted = 0;
}

/* We stole some bits, now we need to put them back... */
static COLD void update_common(struct htable *ht, const void *p)
{
	unsigned int i;
	uintptr_t maskdiff, bitsdiff;

	if (ht->elems == 0) {
		/* Always reveal one bit of the pointer in the bucket,
		 * so it's not zero or HTABLE_DELETED (1), even if
		 * hash happens to be 0.  Assumes (void *)1 is not a
		 * valid pointer. */
		for (i = sizeof(uintptr_t)*CHAR_BIT - 1; i > 0; i--) {
			if ((uintptr_t)p & ((uintptr_t)1 << i))
				break;
		}

		ht->common_mask = ~((uintptr_t)1 << i);
		ht->common_bits = ((uintptr_t)p & ht->common_mask);
		ht->perfect_bit = 1;
		return;
	}

	/* Find bits which are unequal to old common set. */
	maskdiff = ht->common_bits ^ ((uintptr_t)p & ht->common_mask);

	/* These are the bits which go there in existing entries. */
	bitsdiff = ht->common_bits & maskdiff;

	for (i = 0; i < (size_t)1 << ht->bits; i++) {
		if (!entry_is_valid(ht->table[i]))
			continue;
		/* Clear the bits no longer in the mask, set them as
		 * expected. */
		ht->table[i] &= ~maskdiff;
		ht->table[i] |= bitsdiff;
	}

	/* Take away those bits from our mask, bits and perfect bit. */
	ht->common_mask &= ~maskdiff;
	ht->common_bits &= ~maskdiff;
	ht->perfect_bit &= ~maskdiff;
}

static bool htable_add(struct htable *ht, size_t hash, const void *p)
{
	if (ht->elems+1 > ht->max && !double_table(ht))
		return false;
	if (ht->elems+1 + ht->deleted > ht->max_with_deleted)
		rehash_table(ht);
	assert(p);
	if (((uintptr_t)p & ht->common_mask) != ht->common_bits)
		update_common(ht, p);

	ht_add(ht, p, hash);
	ht->elems++;
	return true;
}

static void htable_delval(struct htable *ht, struct htable_iter *i)
{
	assert(i->off < (size_t)1 << ht->bits);
	assert(entry_is_valid(ht->table[i->off]));

	ht->elems--;
	ht->table[i->off] = HTABLE_DELETED;
	ht->deleted++;
}

/*
 * Wrapper code to make it easier to use this hash-table as map.
 */

void shl_htable_init(struct shl_htable *htable,
		     bool (*compare) (const void *a, const void *b),
		     size_t (*rehash)(const void *elem, void *priv),
		     void *priv)
{
	struct htable *ht = (void*)&htable->htable;

	htable->compare = compare;
	htable_init(ht, rehash, priv);
}

void shl_htable_clear(struct shl_htable *htable,
		      void (*free_cb) (void *elem, void *ctx),
		      void *ctx)
{
	struct htable *ht = (void*)&htable->htable;

	htable_clear(ht, free_cb, ctx);
}

void shl_htable_visit(struct shl_htable *htable,
		      void (*visit_cb) (void *elem, void *ctx),
		      void *ctx)
{
	struct htable *ht = (void*)&htable->htable;

	htable_visit(ht, visit_cb, ctx);
}

bool shl_htable_lookup(struct shl_htable *htable, const void *obj, size_t hash,
		       void **out)
{
	struct htable *ht = (void*)&htable->htable;
	struct htable_iter i;
	void *c;

	for (c = htable_firstval(ht, &i, hash);
	     c;
	     c = htable_nextval(ht, &i, hash)) {
		if (htable->compare(obj, c)) {
			if (out)
				*out = c;
			return true;
		}
	}

	return false;
}

int shl_htable_insert(struct shl_htable *htable, const void *obj, size_t hash)
{
	struct htable *ht = (void*)&htable->htable;
	bool b;

	b = htable_add(ht, hash, (void*)obj);
	return b ? 0 : -ENOMEM;
}

bool shl_htable_remove(struct shl_htable *htable, const void *obj, size_t hash,
		       void **out)
{
	struct htable *ht = (void*)&htable->htable;
	struct htable_iter i;
	void *c;

	for (c = htable_firstval(ht, &i, hash);
	     c;
	     c = htable_nextval(ht, &i, hash)) {
		if (htable->compare(obj, c)) {
			if (out)
				*out = c;
			htable_delval(ht, &i);
			return true;
		}
	}

	return false;
}

/*
 * Helpers
 */

bool shl_htable_compare_uint(const void *a, const void *b)
{
	return *(const unsigned int*)a == *(const unsigned int*)b;
}

size_t shl_htable_rehash_uint(const void *elem, void *priv)
{
	return (size_t)*(const unsigned int*)elem;
}

bool shl_htable_compare_ulong(const void *a, const void *b)
{
	return *(const unsigned long*)a == *(const unsigned long*)b;
}

size_t shl_htable_rehash_ulong(const void *elem, void *priv)
{
	return (size_t)*(const unsigned long*)elem;
}

bool shl_htable_compare_u64(const void *a, const void *b)
{
	return *(const uint64_t*)a == *(const uint64_t*)b;
}

size_t shl_htable_rehash_u64(const void *elem, void *priv)
{
	return shl__htable_rehash_u64((const uint64_t*)elem);
}

bool shl_htable_compare_str(const void *a, const void *b)
{
	if (!*(char**)a || !*(char**)b)
		return *(char**)a == *(char**)b;
	else
		return !strcmp(*(char**)a, *(char**)b);
}

/* DJB's hash function */
size_t shl_htable_rehash_str(const void *elem, void *priv)
{
	const char *str = *(char**)elem;
	size_t hash = 5381;

	for ( ; str && *str; ++str)
		hash = (hash << 5) + hash + (size_t)*str;

	return hash;
}
