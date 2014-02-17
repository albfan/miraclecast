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

/* string helpers */

char *shl_strcat(const char *first, const char *second);
_shl_sentinel_ char *shl_strjoin(const char *first, ...);

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

#define _shl_cleanup_strv_ _shl_cleanup_(shl_strv_freep)

/* quoted strings */

char shl_qstr_unescape_char(char c);
void shl_qstr_decode_n(char *str, size_t length);
int shl_qstr_tokenize_n(const char *str, size_t length, char ***out);
int shl_qstr_tokenize(const char *str, char ***out);

#endif  /* SHL_UTIL_H */
