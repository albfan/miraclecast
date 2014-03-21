/*
 * SHL - Dynamic hash-table
 *
 * Copyright (c) 2010-2013 David Herrmann <dh.herrmann@gmail.com>
 * Licensed under LGPLv2+ - see LICENSE_htable file for details
 */

/*
 * Dynamic hash-table
 * Implementation of a self-resizing hashtable to store arbitrary objects.
 * Entries are not allocated by the table itself but are user-allocated. A
 * single entry can be stored multiple times in the hashtable. No
 * maintenance-members need to be embedded in user-allocated objects. However,
 * the key (and optionally the hash) must be stored in the objects.
 *
 * Uses internally the htable from CCAN. See LICENSE_htable.
 */

#ifndef SHL_HTABLE_H
#define SHL_HTABLE_H

#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "shl_macro.h"

/* miscellaneous */

#define shl_htable_entry(pointer, type, member) \
	shl_container_of((pointer), type, member)

/* htable */

struct shl_htable_int {
	size_t (*rehash)(const void *elem, void *priv);
	void *priv;
	unsigned int bits;
	size_t elems, deleted, max, max_with_deleted;
	/* These are the bits which are the same in all pointers. */
	uintptr_t common_mask, common_bits;
	uintptr_t perfect_bit;
	uintptr_t *table;
};

struct shl_htable {
	bool (*compare) (const void *a, const void *b);
	struct shl_htable_int htable;
};

#define SHL_HTABLE_INIT(_obj, _compare, _rehash, _priv)		\
	{							\
		.compare = (_compare),				\
		.htable = {					\
			.rehash = (_rehash),			\
			.priv = (_priv),			\
			.bits = 0,				\
			.elems = 0,				\
			.deleted = 0,				\
			.max = 0,				\
			.max_with_deleted = 0,			\
			.common_mask = -1,			\
			.common_bits = 0,			\
			.perfect_bit = 0,			\
			.table = &(_obj).htable.perfect_bit	\
		}						\
	}

void shl_htable_init(struct shl_htable *htable,
		     bool (*compare) (const void *a, const void *b),
		     size_t (*rehash)(const void *elem, void *priv),
		     void *priv);
void shl_htable_clear(struct shl_htable *htable,
		      void (*free_cb) (void *elem, void *ctx),
		      void *ctx);
void shl_htable_visit(struct shl_htable *htable,
		      void (*visit_cb) (void *elem, void *ctx),
		      void *ctx);
bool shl_htable_lookup(struct shl_htable *htable, const void *obj, size_t hash,
		       void **out);
int shl_htable_insert(struct shl_htable *htable, const void *obj, size_t hash);
bool shl_htable_remove(struct shl_htable *htable, const void *obj, size_t hash,
		       void **out);

size_t shl_htable_this_or_next(struct shl_htable *htable, size_t i);
void *shl_htable_get_entry(struct shl_htable *htable, size_t i);

#define SHL_HTABLE_FOREACH(_iter, _ht) for ( \
		size_t htable__i = shl_htable_this_or_next((_ht), 0); \
		(_iter = shl_htable_get_entry((_ht), htable__i)); \
		htable__i = shl_htable_this_or_next((_ht), htable__i + 1) \
	)

#define SHL_HTABLE_FOREACH_MACRO(_iter, _ht, _accessor) for ( \
		size_t htable__i = shl_htable_this_or_next((_ht), 0); \
		(_iter = shl_htable_get_entry((_ht), htable__i), \
		 _iter = _iter ? _accessor((void*)_iter) : NULL); \
		htable__i = shl_htable_this_or_next((_ht), htable__i + 1) \
	)

#define SHL_HTABLE_FIRST(_ht) \
	shl_htable_get_entry((_ht), shl_htable_this_or_next((_ht), 0))

#define SHL_HTABLE_FIRST_MACRO(_ht, _accessor) ({ \
		void *htable__i = shl_htable_get_entry((_ht), \
					shl_htable_this_or_next((_ht), 0)); \
		htable__i ? _accessor(htable__i) : NULL; })

/* uint htables */

#if SIZE_MAX < UINT_MAX
#  error "'size_t' is smaller than 'unsigned int'"
#endif

bool shl_htable_compare_uint(const void *a, const void *b);
size_t shl_htable_rehash_uint(const void *elem, void *priv);

#define SHL_HTABLE_INIT_UINT(_obj)					\
	SHL_HTABLE_INIT((_obj), shl_htable_compare_uint,		\
				shl_htable_rehash_uint,			\
				NULL)

static inline void shl_htable_init_uint(struct shl_htable *htable)
{
	shl_htable_init(htable, shl_htable_compare_uint,
			shl_htable_rehash_uint, NULL);
}

static inline void shl_htable_clear_uint(struct shl_htable *htable,
					 void (*cb) (unsigned int *elem,
					             void *ctx),
					 void *ctx)
{
	shl_htable_clear(htable, (void (*) (void*, void*))cb, ctx);
}

static inline void shl_htable_visit_uint(struct shl_htable *htable,
					 void (*cb) (unsigned int *elem,
					             void *ctx),
					 void *ctx)
{
	shl_htable_visit(htable, (void (*) (void*, void*))cb, ctx);
}

static inline bool shl_htable_lookup_uint(struct shl_htable *htable,
					  unsigned int key,
					  unsigned int **out)
{
	return shl_htable_lookup(htable, (const void*)&key, (size_t)key,
				 (void**)out);
}

static inline int shl_htable_insert_uint(struct shl_htable *htable,
					 const unsigned int *key)
{
	return shl_htable_insert(htable, (const void*)key, (size_t)*key);
}

static inline bool shl_htable_remove_uint(struct shl_htable *htable,
					  unsigned int key,
					  unsigned int **out)
{
	return shl_htable_remove(htable, (const void*)&key, (size_t)key,
				 (void**)out);
}

/* ulong htables */

#if SIZE_MAX < ULONG_MAX
#  error "'size_t' is smaller than 'unsigned long'"
#endif

bool shl_htable_compare_ulong(const void *a, const void *b);
size_t shl_htable_rehash_ulong(const void *elem, void *priv);

#define SHL_HTABLE_INIT_ULONG(_obj)					\
	SHL_HTABLE_INIT((_obj), shl_htable_compare_ulong,		\
				shl_htable_rehash_ulong,		\
				NULL)

static inline void shl_htable_init_ulong(struct shl_htable *htable)
{
	shl_htable_init(htable, shl_htable_compare_ulong,
			shl_htable_rehash_ulong, NULL);
}

static inline void shl_htable_clear_ulong(struct shl_htable *htable,
					  void (*cb) (unsigned long *elem,
					              void *ctx),
					  void *ctx)
{
	shl_htable_clear(htable, (void (*) (void*, void*))cb, ctx);
}

static inline void shl_htable_visit_ulong(struct shl_htable *htable,
					  void (*cb) (unsigned long *elem,
					              void *ctx),
					  void *ctx)
{
	shl_htable_visit(htable, (void (*) (void*, void*))cb, ctx);
}

static inline bool shl_htable_lookup_ulong(struct shl_htable *htable,
					   unsigned long key,
					   unsigned long **out)
{
	return shl_htable_lookup(htable, (const void*)&key, (size_t)key,
				 (void**)out);
}

static inline int shl_htable_insert_ulong(struct shl_htable *htable,
					  const unsigned long *key)
{
	return shl_htable_insert(htable, (const void*)key, (size_t)*key);
}

static inline bool shl_htable_remove_ulong(struct shl_htable *htable,
					   unsigned long key,
					   unsigned long **out)
{
	return shl_htable_remove(htable, (const void*)&key, (size_t)key,
				 (void**)out);
}

/* uint64 htables */

bool shl_htable_compare_u64(const void *a, const void *b);
size_t shl_htable_rehash_u64(const void *elem, void *priv);

static inline size_t shl__htable_rehash_u64(const uint64_t *p)
{
#if SIZE_MAX < UINT64_MAX
	return (size_t)((*p ^ (*p >> 32)) & 0xffffffff);
#else
	return (size_t)*p;
#endif
}

#define SHL_HTABLE_INIT_U64(_obj)					\
	SHL_HTABLE_INIT((_obj), shl_htable_compare_u64,			\
				shl_htable_rehash_u64,			\
				NULL)

static inline void shl_htable_init_u64(struct shl_htable *htable)
{
	shl_htable_init(htable, shl_htable_compare_u64,
			shl_htable_rehash_u64, NULL);
}

static inline void shl_htable_clear_u64(struct shl_htable *htable,
					void (*cb) (uint64_t *elem, void *ctx),
					void *ctx)
{
	shl_htable_clear(htable, (void (*) (void*, void*))cb, ctx);
}

static inline void shl_htable_visit_u64(struct shl_htable *htable,
					void (*cb) (uint64_t *elem, void *ctx),
					void *ctx)
{
	shl_htable_visit(htable, (void (*) (void*, void*))cb, ctx);
}

static inline bool shl_htable_lookup_u64(struct shl_htable *htable,
					 uint64_t key,
					 uint64_t **out)
{
	return shl_htable_lookup(htable, (const void*)&key,
				 shl__htable_rehash_u64(&key),
				 (void**)out);
}

static inline int shl_htable_insert_u64(struct shl_htable *htable,
					const uint64_t *key)
{
	return shl_htable_insert(htable,
				 (const void*)key,
				 shl__htable_rehash_u64(key));
}

static inline bool shl_htable_remove_u64(struct shl_htable *htable,
					 uint64_t key,
					 uint64_t **out)
{
	return shl_htable_remove(htable, (const void*)&key,
				 shl__htable_rehash_u64(&key),
				 (void**)out);
}

/* string htables */

bool shl_htable_compare_str(const void *a, const void *b);
size_t shl_htable_rehash_str(const void *elem, void *priv);

#define SHL_HTABLE_INIT_STR(_obj)					\
	SHL_HTABLE_INIT((_obj), shl_htable_compare_str,			\
				shl_htable_rehash_str,			\
				NULL)

static inline void shl_htable_init_str(struct shl_htable *htable)
{
	shl_htable_init(htable, shl_htable_compare_str,
			shl_htable_rehash_str, NULL);
}

static inline void shl_htable_clear_str(struct shl_htable *htable,
					void (*cb) (char **elem,
					            void *ctx),
					void *ctx)
{
	shl_htable_clear(htable, (void (*) (void*, void*))cb, ctx);
}

static inline void shl_htable_visit_str(struct shl_htable *htable,
					void (*cb) (char **elem,
					            void *ctx),
					void *ctx)
{
	shl_htable_visit(htable, (void (*) (void*, void*))cb, ctx);
}

static inline size_t shl_htable_hash_str(struct shl_htable *htable,
					 const char *str, size_t *hash)
{
	size_t h;

	if (hash && *hash) {
		h = *hash;
	} else {
		h = htable->htable.rehash((const void*)&str, NULL);
		if (hash)
			*hash = h;
	}

	return h;
}

static inline bool shl_htable_lookup_str(struct shl_htable *htable,
					 const char *str, size_t *hash,
					 char ***out)
{
	size_t h;

	h = shl_htable_hash_str(htable, str, hash);
	return shl_htable_lookup(htable, (const void*)&str, h, (void**)out);
}

static inline int shl_htable_insert_str(struct shl_htable *htable,
					char **str, size_t *hash)
{
	size_t h;

	h = shl_htable_hash_str(htable, *str, hash);
	return shl_htable_insert(htable, (const void*)str, h);
}

static inline bool shl_htable_remove_str(struct shl_htable *htable,
					 const char *str, size_t *hash,
					 char ***out)
{
	size_t h;

	h = shl_htable_hash_str(htable, str, hash);
	return shl_htable_remove(htable, (const void*)&str, h, (void **)out);
}

#endif /* SHL_HTABLE_H */
