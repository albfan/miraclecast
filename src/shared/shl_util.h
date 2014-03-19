/*
 * SHL - Utility Helpers
 *
 * Copyright (c) 2011-2013 David Herrmann <dh.herrmann@gmail.com>
 * Dedicated to the Public Domain
 */

/*
 * Utility Helpers
 */

#ifndef SHL_UTIL_H
#define SHL_UTIL_H

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "shl_macro.h"

/* strict atoi */

int shl_ctoi(char ch, unsigned int base);

int shl_atoi_ulln(const char *str,
		  size_t len,
		  unsigned int base,
		  const char **next,
		  unsigned long long *out);
int shl_atoi_uln(const char *str,
		 size_t len,
		 unsigned int base,
		 const char **next,
		 unsigned long *out);
int shl_atoi_un(const char *str,
		size_t len,
		unsigned int base,
		const char **next,
		unsigned int *out);
int shl_atoi_zn(const char *str,
		size_t len,
		unsigned int base,
		const char **next,
		size_t *out);

static inline int shl_atoi_ull(const char *str,
			       unsigned int base,
			       const char **next,
			       unsigned long long *out)
{
	return shl_atoi_ulln(str, strlen(str), base, next, out);
}

static inline int shl_atoi_ul(const char *str,
			      unsigned int base,
			      const char **next,
			      unsigned long *out)
{
	return shl_atoi_uln(str, strlen(str), base, next, out);
}

static inline int shl_atoi_u(const char *str,
			     unsigned int base,
			     const char **next,
			     unsigned int *out)
{
	return shl_atoi_un(str, strlen(str), base, next, out);
}

static inline int shl_atoi_z(const char *str,
			     unsigned int base,
			     const char **next,
			     size_t *out)
{
	return shl_atoi_zn(str, strlen(str), base, next, out);
}

/* greedy alloc */

void *shl_greedy_realloc(void **mem, size_t *size, size_t need);
void *shl_greedy_realloc0(void **mem, size_t *size, size_t need);
void *shl_greedy_realloc_t(void **arr, size_t *cnt, size_t need, size_t ts);
void *shl_greedy_realloc0_t(void **arr, size_t *cnt, size_t need, size_t ts);

#define SHL_GREEDY_REALLOC_T(array, count, need) \
	shl_greedy_realloc_t((void**)&(array), &count, need, sizeof(*(array)))
#define SHL_GREEDY_REALLOC0_T(array, count, need) \
	shl_greedy_realloc0_t((void**)&(array), &count, need, sizeof(*(array)))

/* string helpers */

char *shl_strcat(const char *first, const char *second);
_shl_sentinel_ char *shl_strjoin(const char *first, ...);
int shl_strsplit_n(const char *str, size_t len, const char *sep, char ***out);
int shl_strsplit(const char *str, const char *sep, char ***out);

static inline bool shl_isempty(const char *str)
{
	return !str || !*str;
}

static inline char *shl_startswith(const char *str, const char *prefix)
{
	if (!strncmp(str, prefix, strlen(prefix)))
		return (char*)str + strlen(prefix);
	else
		return NULL;
}

/* strv */

void shl_strv_free(char **strv);

static inline void shl_strv_freep(char ***strv)
{
	shl_strv_free(*strv);
}

#define _shl_strv_free_ _shl_cleanup_(shl_strv_freep)

/* quoted strings */

char shl_qstr_unescape_char(char c);
void shl_qstr_decode_n(char *str, size_t length);
int shl_qstr_tokenize_n(const char *str, size_t length, char ***out);
int shl_qstr_tokenize(const char *str, char ***out);
int shl_qstr_join(char **strv, char **out);

/* mkdir */

int shl_mkdir_p(const char *path, mode_t mode);
int shl_mkdir_p_prefix(const char *prefix, const char *path, mode_t mode);

/* time */

uint64_t shl_now(clockid_t clock);

/* ratelimit */

struct shl_ratelimit {
	uint64_t interval;
	uint64_t begin;
	unsigned burst;
	unsigned num;
};

#define SHL_RATELIMIT_DEFINE(_name, _interval, _burst) \
	struct shl_ratelimit _name = { (_interval), (_burst), 0, 0 }

#define SHL_RATELIMIT_INIT(_v, _interval, _burst) do { \
		struct shl_ratelimit *_r = &(_v); \
		_r->interval = (_interval); \
		_r->burst = (_burst); \
		_r->num = 0; \
		_r->begin = 0; \
	} while (false)

#define SHL_RATELIMIT_RESET(_v) do { \
		struct shl_ratelimit *_r = &(_v); \
		_r->num = 0; \
		_r->begin = 0; \
	} while (false)

bool shl_ratelimit_test(struct shl_ratelimit *r);

#endif  /* SHL_UTIL_H */
