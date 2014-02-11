/*
 * SHL - Utility Helpers
 *
 * Copyright (c) 2011-2013 David Herrmann <dh.herrmann@gmail.com>
 * Dedicated to the Public Domain
 */

/*
 * Utility Helpers
 */

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "shl_macro.h"
#include "shl_util.h"

/*
 * Strict atoi()
 * These helpers implement a strict version of atoi() (or strtol()). They only
 * parse digit/alpha characters. No whitespace or other characters are parsed.
 * The unsigned-variants explicitly forbid leading +/- signs. Use the signed
 * variants to allow these.
 * Base-prefix parsing is only done if base=0 is requested. Otherwise,
 * base-prefixes are forbidden.
 * The input string must be ASCII compatbile (which includes UTF8).
 *
 * We also always check for overflows and return errors (but continue parsing!)
 * so callers can catch it correctly.
 *
 * Additionally, we allow "length" parameters so strings do not necessarily have
 * to be zero-terminated. We have wrappers which skip this by passing strlen().
 */

int shl_ctoi(char ch, unsigned int base)
{
	unsigned int v;

	switch (ch) {
	case '0'...'9':
		v = ch - '0';
		break;
	case 'a'...'z':
		v = ch - 'a' + 10;
		break;
	case 'A'...'Z':
		v = ch - 'A' + 10;
		break;
	default:
		return -EINVAL;
	}

	if (v >= base)
		return -EINVAL;

	return v;
}

/* figure out base and skip prefix */
static unsigned int shl__skip_base(const char **str, size_t *len)
{
	if (*len > 1) {
		if ((*str)[0] == '0') {
			if (shl_ctoi((*str)[1], 8) >= 0) {
				*str += 1;
				*len -= 1;
				return 8;
			}
		}
	}

	if (*len > 2) {
		if ((*str)[0] == '0' && (*str)[1] == 'x') {
			if (shl_ctoi((*str)[2], 16) >= 0) {
				*str += 2;
				*len -= 2;
				return 16;
			}
		}
	}

	return 10;
}

int shl_atoi_ulln(const char *str,
		  size_t len,
		  unsigned int base,
		  const char **next,
		  unsigned long long *out)
{
	bool huge;
	uint32_t val1;
	unsigned long long val2;
	size_t pos;
	int r, c;

	/* We use u32 as storage first so we have fast mult-overflow checks. We
	 * cast up to "unsigned long long" once we exceed UINT32_MAX. Overflow
	 * checks will get pretty slow for non-power2 bases, though. */

	huge = false;
	val1 = 0;
	val2 = 0;
	r = 0;

	if (base > 36) {
		if (next)
			*next = str;
		if (out)
			*out = 0;
		return -EINVAL;
	}

	if (base == 0)
		base = shl__skip_base(&str, &len);

	for (pos = 0; pos < len; ++pos) {
		c = shl_ctoi(str[pos], base);
		if (c < 0)
			break;

		/* skip calculations on error */
		if (r < 0)
			continue;

		if (!huge) {
			val2 = val1;
			r = shl_mult_u32(&val1, base);
			if (r >= 0 && val1 + c >= val1)
				val1 += c;
			else
				huge = true;
		}

		if (huge) {
			r = shl_mult_ull(&val2, base);
			if (r >= 0 && val2 + c >= val2)
				val2 += c;
		}
	}

	if (next)
		*next = (char*)&str[pos];
	if (out) {
		if (r < 0)
			*out = ULLONG_MAX;
		else if (huge)
			*out = val2;
		else
			*out = val1;
	}

	return r;
}

int shl_atoi_uln(const char *str,
		 size_t len,
		 unsigned int base,
		 const char **next,
		 unsigned long *out)
{
	unsigned long long val;
	int r;

	r = shl_atoi_ulln(str, len, base, next, &val);
	if (r >= 0 && val > ULONG_MAX)
		r = -ERANGE;

	if (out)
		*out = shl_min(val, (unsigned long long)ULONG_MAX);

	return r;
}

int shl_atoi_un(const char *str,
		size_t len,
		unsigned int base,
		const char **next,
		unsigned int *out)
{
	unsigned long long val;
	int r;

	r = shl_atoi_ulln(str, len, base, next, &val);
	if (r >= 0 && val > UINT_MAX)
		r = -ERANGE;

	if (out)
		*out = shl_min(val, (unsigned long long)UINT_MAX);

	return r;
}

int shl_atoi_zn(const char *str,
		size_t len,
		unsigned int base,
		const char **next,
		size_t *out)
{
	unsigned long long val;
	int r;

	r = shl_atoi_ulln(str, len, base, next, &val);
	if (r >= 0 && val > SIZE_MAX)
		r = -ERANGE;

	if (out)
		*out = shl_min(val, (unsigned long long)SIZE_MAX);

	return r;
}

/*
 * Greedy Realloc
 * The greedy-realloc helpers simplify power-of-2 buffer allocations. If you
 * have a dynamic array, simply use shl_greedy_realloc() for re-allocations
 * and it makes sure your buffer-size is always a multiple of 2 and is big
 * enough for your new entries.
 * Default size is 64, but you can initialize your buffer to a bigger default
 * if you need.
 */

void *shl_greedy_realloc(void **mem, size_t *size, size_t need)
{
	size_t nsize;
	void *p;

	if (*size >= need)
		return *mem;

	nsize = SHL_ALIGN_POWER2(shl_max_t(size_t, 64U, need));
	if (nsize == 0)
		return NULL;

	p = realloc(*mem, nsize);
	if (!p)
		return NULL;

	*mem = p;
	*size = nsize;
	return p;
}

void *shl_greedy_realloc0(void **mem, size_t *size, size_t need)
{
	size_t prev = *size;
	uint8_t *p;

	p = shl_greedy_realloc(mem, size, need);
	if (!p)
		return NULL;

	if (*size > prev)
		shl_memzero(&p[prev], *size - prev);

	return p;
}

/*
 * String Helpers
 */

char *shl_strcat(const char *first, const char *second)
{
	size_t flen, slen;
	char *str;

	if (!first)
		first = "";
	if (!second)
		second = "";

	flen = strlen(first);
	slen = strlen(second);
	if (flen + slen + 1 <= flen)
		return NULL;

	str = malloc(flen + slen + 1);
	if (!str)
		return NULL;

	strcpy(str, first);
	strcpy(&str[flen], second);

	return str;
}

char *shl_strjoin(const char *first, ...) {
	va_list args;
	size_t len, l;
	const char *arg;
	char *str, *p;

	va_start(args, first);

	for (arg = first, len = 0; arg; arg = va_arg(args, const char*)) {
		l = strlen(arg);
		if (len + l < len)
			return NULL;

		len += l;
	}

	va_end(args);

	str = malloc(len + 1);
	if (!str)
		return NULL;

	va_start(args, first);

	for (arg = first, p = str; arg; arg = va_arg(args, const char*))
		p = stpcpy(p, arg);

	va_end(args);

	*p = 0;
	return str;
}

/*
 * strv
 */

void shl_strv_free(char **strv)
{
	unsigned int i;

	if (!strv)
		return;

	for (i = 0; strv[i]; ++i)
		free(strv[i]);

	free(strv);
}

/*
 * Quoted Strings
 */

char shl_qstr_unescape_char(char c)
{
	switch (c) {
	case 'a':
		return '\a';
	case 'b':
		return '\b';
	case 'f':
		return '\f';
	case 'n':
		return '\n';
	case 'r':
		return '\r';
	case 't':
		return '\t';
	case 'v':
		return '\v';
	case '"':
		return '"';
	case '\'':
		return '\'';
	case '\\':
		return '\\';
	default:
		return 0;
	}
}

void shl_qstr_decode_n(char *str, size_t length)
{
	size_t i;
	bool escaped;
	char *pos, c, quoted;

	quoted = 0;
	escaped = false;
	pos = str;

	for (i = 0; i < length; ++i) {
		if (escaped) {
			escaped = false;
			c = shl_qstr_unescape_char(str[i]);
			if (c) {
				*pos++ = c;
			} else if (!str[i]) {
				/* ignore binary 0 */
			} else {
				*pos++ = '\\';
				*pos++ = str[i];
			}
		} else if (quoted) {
			if (str[i] == '\\')
				escaped = true;
			else if (str[i] == '"' && quoted == '"')
				quoted = 0;
			else if (str[i] == '\'' && quoted == '\'')
				quoted = 0;
			else if (!str[i])
				/* ignore binary 0 */ ;
			else
				*pos++ = str[i];
		} else {
			if (str[i] == '\\')
				escaped = true;
			else if (str[i] == '"' || str[i] == '\'')
				quoted = str[i];
			else if (!str[i])
				/* ignore binary 0 */ ;
			else
				*pos++ = str[i];
		}
	}

	if (escaped)
		*pos++ = '\\';

	*pos = 0;
}

static int shl__qstr_push(char ***strv,
			  size_t *strv_num,
			  size_t *strv_size,
			  const char *str,
			  size_t len)
{
	size_t strv_need;
	char *ns;

	strv_need = (*strv_num + 2) * sizeof(**strv);
	if (!shl_greedy_realloc0((void**)strv, strv_size, strv_need))
		return -ENOMEM;

	ns = malloc(len + 1);
	memcpy(ns, str, len);
	ns[len] = 0;

	shl_qstr_decode_n(ns, len);
	(*strv)[*strv_num] = ns;
	*strv_num += 1;

	return 0;
}

int shl_qstr_tokenize_n(const char *str, size_t length, char ***out)
{
	char **strv, quoted;
	size_t i, strv_num, strv_size;
	const char *pos;
	bool escaped;
	int r;

	if (!out)
		return -EINVAL;
	if (!str)
		str = "";

	strv_num = 0;
	strv_size = sizeof(*strv);
	strv = malloc(strv_size);
	if (!strv)
		return -ENOMEM;

	quoted = 0;
	escaped = false;
	pos = str;

	for (i = 0; i < length; ++i) {
		if (escaped) {
			escaped = false;
		} else if (str[i] == '\\') {
			escaped = true;
		} else if (quoted) {
			if (str[i] == '"' && quoted == '"')
				quoted = 0;
			else if (str[i] == '\'' && quoted == '\'')
				quoted = 0;
		} else if (str[i] == '"' && quoted == '"') {
			quoted = '"';
		} else if (str[i] == '\'' && quoted == '\'') {
			quoted = '\'';
		} else if (str[i] == ' ') {
			/* ignore multiple separators */
			if (pos != &str[i]) {
				r = shl__qstr_push(&strv,
						   &strv_num,
						   &strv_size,
						   pos,
						   &str[i] - pos);
				if (r < 0)
					goto error;
			}

			pos = &str[i + 1];
		}
	}

	/* copy trailing token if available */
	if (i > 0 && pos != &str[i]) {
		r = shl__qstr_push(&strv,
				   &strv_num,
				   &strv_size,
				   pos,
				   &str[i] - pos);
		if (r < 0)
			goto error;
	}

	if ((int)strv_num < (ssize_t)strv_num) {
		r = -ENOMEM;
		goto error;
	}

	strv[strv_num] = NULL;
	*out = strv;
	return strv_num;

error:
	for (i = 0; i < strv_num; ++i)
		free(strv[i]);
	free(strv);
	return r;
}

int shl_qstr_tokenize(const char *str, char ***out)
{
	return shl_qstr_tokenize_n(str, str ? strlen(str) : 0, out);
}
