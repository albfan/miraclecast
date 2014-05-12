/*
 * SHL - Macros
 *
 * Copyright (c) 2011-2014 David Herrmann <dh.herrmann@gmail.com>
 * Dedicated to the Public Domain
 */

/*
 * Macros
 */

#ifndef SHL_MACRO_H
#define SHL_MACRO_H

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* sanity checks required for some macros */
#if __SIZEOF_POINTER__ != 4 && __SIZEOF_POINTER__ != 8
#error "Pointer size is neither 4 nor 8 bytes"
#endif

/* gcc attributes; look them up for more information */
#define _shl_printf_(_a, _b) __attribute__((__format__(printf, _a, _b)))
#define _shl_alloc_(...) __attribute__((__alloc_size__(__VA_ARGS__)))
#define _shl_sentinel_ __attribute__((__sentinel__))
#define _shl_noreturn_ __attribute__((__noreturn__))
#define _shl_unused_ __attribute__((__unused__))
#define _shl_pure_ __attribute__((__pure__))
#define _shl_const_ __attribute__((__const__))
#define _shl_deprecated_ __attribute__((__deprecated__))
#define _shl_packed_ __attribute__((__packed__))
#define _shl_malloc_ __attribute__((__malloc__))
#define _shl_weak_ __attribute__((__weak__))
#define _shl_likely_(_val) (__builtin_expect(!!(_val), 1))
#define _shl_unlikely_(_val) (__builtin_expect(!!(_val), 0))
#define _shl_public_ __attribute__((__visibility__("default")))
#define _shl_hidden_ __attribute__((__visibility__("hidden")))
#define _shl_weakref_(_val) __attribute__((__weakref__(#_val)))
#define _shl_cleanup_(_val) __attribute__((__cleanup__(_val)))

static inline void shl_freep(void *p)
{
	free(*(void**)p);
}

#define _shl_free_ _shl_cleanup_(shl_freep)

static inline void shl_closep(int *p)
{
	if (*p >= 0)
		close(*p);
}

#define _shl_close_ _shl_cleanup_(shl_closep)

static inline void shl_set_errno(int *r)
{
	errno = *r;
}

#define SHL_PROTECT_ERRNO \
	_shl_cleanup_(shl_set_errno) _shl_unused_ int shl__errno = errno

/* 2-level stringify helper */
#define SHL__STRINGIFY(_val) #_val
#define SHL_STRINGIFY(_val) SHL__STRINGIFY(_val)

/* 2-level concatenate helper */
#define SHL__CONCATENATE(_a, _b) _a ## _b
#define SHL_CONCATENATE(_a, _b) SHL__CONCATENATE(_a, _b)

/* unique identifier with prefix */
#define SHL_UNIQUE(_prefix) SHL_CONCATENATE(_prefix, __COUNTER__)

/* array element count */
#define SHL_ARRAY_LENGTH(_array) (sizeof(_array)/sizeof(*(_array)))

/* get parent pointer by container-type, member and member-pointer */
#define shl_container_of(_ptr, _type, _member) \
	({ \
		const typeof( ((_type *)0)->_member ) *__mptr = (_ptr); \
		(_type *)( (char *)__mptr - offsetof(_type, _member) ); \
	})

/* return maximum of two values and do strict type checking */
#define shl_max(_a, _b) \
	({ \
		typeof(_a) __a = (_a); \
		typeof(_b) __b = (_b); \
		(void) (&__a == &__b); \
		__a > __b ? __a : __b; \
	})

/* same as shl_max() but perform explicit cast beforehand */
#define shl_max_t(_type, _a, _b) \
	({ \
		_type __a = (_type)(_a); \
		_type __b = (_type)(_b); \
		__a > __b ? __a : __b; \
	})

/* return minimum of two values and do strict type checking */
#define shl_min(_a, _b) \
	({ \
		typeof(_a) __a = (_a); \
		typeof(_b) __b = (_b); \
		(void) (&__a == &__b); \
		__a < __b ? __a : __b; \
	})

/* same as shl_min() but perform explicit cast beforehand */
#define shl_min_t(_type, _a, _b) \
	({ \
		_type __a = (_type)(_a); \
		_type __b = (_type)(_b); \
		__a < __b ? __a : __b; \
	})

/* clamp value between low and high barriers */
#define shl_clamp(_val, _low, _high) \
	({ \
		typeof(_val) __v = (_val); \
		typeof(_low) __l = (_low); \
		typeof(_high) __h = (_high); \
		(void) (&__v == &__l); \
		(void) (&__v == &__h); \
		((__v > __h) ? __h : ((__v < __l) ? __l : __v)); \
	})

/* align to next higher power-of-2 (except for: 0 => 0, overflow => 0) */
static inline size_t SHL_ALIGN_POWER2(size_t u)
{
	unsigned int shift;

	/* clz(0) is undefined */
	if (u == 1)
		return 1;

	shift = sizeof(unsigned long long) * 8ULL - __builtin_clzll(u - 1ULL);
	return 1ULL << shift;
}

/* zero memory or type */
#define shl_memzero(_ptr, _size) (memset((_ptr), 0, (_size)))
#define shl_zero(_ptr) (shl_memzero(&(_ptr), sizeof(_ptr)))

/* ptr <=> uint casts */
#define SHL_PTR_TO_TYPE(_type, _ptr) ((_type)((uintptr_t)(_ptr)))
#define SHL_TYPE_TO_PTR(_type, _int) ((void*)((uintptr_t)(_int)))
#define SHL_PTR_TO_INT(_ptr) SHL_PTR_TO_TYPE(int, (_ptr))
#define SHL_INT_TO_PTR(_ptr) SHL_TYPE_TO_PTR(int, (_ptr))
#define SHL_PTR_TO_UINT(_ptr) SHL_PTR_TO_TYPE(unsigned int, (_ptr))
#define SHL_UINT_TO_PTR(_ptr) SHL_TYPE_TO_PTR(unsigned int, (_ptr))
#define SHL_PTR_TO_LONG(_ptr) SHL_PTR_TO_TYPE(long, (_ptr))
#define SHL_LONG_TO_PTR(_ptr) SHL_TYPE_TO_PTR(long, (_ptr))
#define SHL_PTR_TO_ULONG(_ptr) SHL_PTR_TO_TYPE(unsigned long, (_ptr))
#define SHL_ULONG_TO_PTR(_ptr) SHL_TYPE_TO_PTR(unsigned long, (_ptr))
#define SHL_PTR_TO_S32(_ptr) SHL_PTR_TO_TYPE(int32_t, (_ptr))
#define SHL_S32_TO_PTR(_ptr) SHL_TYPE_TO_PTR(int32_t, (_ptr))
#define SHL_PTR_TO_U32(_ptr) SHL_PTR_TO_TYPE(uint32_t, (_ptr))
#define SHL_U32_TO_PTR(_ptr) SHL_TYPE_TO_PTR(uint32_t, (_ptr))
#define SHL_PTR_TO_S64(_ptr) SHL_PTR_TO_TYPE(int64_t, (_ptr))
#define SHL_S64_TO_PTR(_ptr) SHL_TYPE_TO_PTR(int64_t, (_ptr))
#define SHL_PTR_TO_U64(_ptr) SHL_PTR_TO_TYPE(uint64_t, (_ptr))
#define SHL_U64_TO_PTR(_ptr) SHL_TYPE_TO_PTR(uint64_t, (_ptr))

/* compile-time assertions */
#define shl_assert_cc(_expr) static_assert(_expr, #_expr)

/*
 * Safe Multiplications
 * Multiplications are subject to overflows. These helpers guarantee that the
 * multiplication can be done safely and return -ERANGE if not.
 *
 * Note: This is horribly slow for ull/uint64_t as we need a division to test
 * for overflows. Take that into account when using these. For smaller integers,
 * we can simply use an upcast-multiplication which gcc should be smart enough
 * to optimize.
 */

#define SHL__REAL_MULT(_max, _val, _factor) \
	({ \
		(_factor == 0 || *(_val) <= (_max) / (_factor)) ? \
			((*(_val) *= (_factor)), 0) : \
			-ERANGE; \
	})

#define SHL__UPCAST_MULT(_type, _max, _val, _factor) \
	({ \
		_type v = *(_val) * (_type)(_factor); \
		(v <= (_max)) ? \
			((*(_val) = v), 0) : \
			-ERANGE; \
	})

static inline int shl_mult_ull(unsigned long long *val,
			       unsigned long long factor)
{
	return SHL__REAL_MULT(ULLONG_MAX, val, factor);
}

static inline int shl_mult_ul(unsigned long *val, unsigned long factor)
{
#if ULONG_MAX < ULLONG_MAX
	return SHL__UPCAST_MULT(unsigned long long, ULONG_MAX, val, factor);
#else
	shl_assert_cc(sizeof(unsigned long) == sizeof(unsigned long long));
	return shl_mult_ull((unsigned long long*)val, factor);
#endif
}

static inline int shl_mult_u(unsigned int *val, unsigned int factor)
{
#if UINT_MAX < ULONG_MAX
	return SHL__UPCAST_MULT(unsigned long, UINT_MAX, val, factor);
#elif UINT_MAX < ULLONG_MAX
	return SHL__UPCAST_MULT(unsigned long long, UINT_MAX, val, factor);
#else
	shl_assert_cc(sizeof(unsigned int) == sizeof(unsigned long long));
	return shl_mult_ull(val, factor);
#endif
}

static inline int shl_mult_u64(uint64_t *val, uint64_t factor)
{
	return SHL__REAL_MULT(UINT64_MAX, val, factor);
}

static inline int shl_mult_u32(uint32_t *val, uint32_t factor)
{
	return SHL__UPCAST_MULT(uint_fast64_t, UINT32_MAX, val, factor);
}

static inline int shl_mult_u16(uint16_t *val, uint16_t factor)
{
	return SHL__UPCAST_MULT(uint_fast32_t, UINT16_MAX, val, factor);
}

static inline int shl_mult_u8(uint8_t *val, uint8_t factor)
{
	return SHL__UPCAST_MULT(uint_fast16_t, UINT8_MAX, val, factor);
}

#endif  /* SHL_MACRO_H */
