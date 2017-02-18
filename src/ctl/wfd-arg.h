/*
 * MiracleCast - Wifi-Display/Miracast Implementation
 *
 * Copyright (c) 2013-2014 David Herrmann <dh.herrmann@gmail.com>
 *
 * MiracleCast is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * MiracleCast is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with MiracleCast; If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "shl_macro.h"

#ifndef CTL_ARG_H
#define CTL_ARG_H

#define wfd_arg_i8(_v) { .type = WFD_ARG_I8, .i8 = (_v) }
#define wfd_arg_u8(_v) { .type = WFD_ARG_U8, .u8 = (_v) }
#define wfd_arg_i16(_v) { .type = WFD_ARG_I16, .i16 = (_v) }
#define wfd_arg_u16(_v) { .type = WFD_ARG_U16, .u16 = (_v) }
#define wfd_arg_i32(_v) { .type = WFD_ARG_I32, .i32 = (_v) }
#define wfd_arg_u32(_v) { .type = WFD_ARG_U32, .u32 = (_v) }
#define wfd_arg_i64(_v) { .type = WFD_ARG_I64, .i64 = (_v) }
#define wfd_arg_u64(_v) { .type = WFD_ARG_U64, .u64 = (_v) }
#define wfd_arg_cstr(_v) { .type = WFD_ARG_CSTR, .ptr = (_v) }
#define wfd_arg_cptr(_v) { .type = WFD_ARG_CPTR, .ptr = (_v) }
#define wfd_arg_arg_list(_v) {									\
				.type = WFD_ARG_ARG_LIST,						\
				.ptr = &(struct wfd_arg_list) wfd_arg_list(_v)	\
}
#define wfd_arg_dict(_k, _v) {		\
	.type = WFD_ARG_DICT,			\
	.k = (struct wfd_arg[]){_k},	\
	.v = (struct wfd_arg[]){_v}		\
}

#if INT_MAX == INT64_MAX
#define wfd_arg_i(_v) wfd_arg_i64(_v)
#define wfd_arg_u(_v) wfd_arg_u64(_v)
#elif INT_MAX == INT32_MAX
#define wfd_arg_i(_v) wfd_arg_i32(_v)
#define wfd_arg_u(_v) wfd_arg_u32(_v)
#else
#error unsupported int size
#endif

#define wfd_arg_type_id(_t) _Generic((_t),	\
	int8_t: WFD_ARG_I8,						\
	uint8_t: WFD_ARG_U8,					\
	int16_t: WFD_ARG_I16,					\
	uint16_t: WFD_ARG_U16,					\
	int32_t: WFD_ARG_I32,					\
	uint32_t: WFD_ARG_U32,					\
	int64_t: WFD_ARG_I64,					\
	uint64_t: WFD_ARG_U64,					\
	const char *: WFD_ARG_CSTR,				\
	const wfd_arg_list *: WFD_ARG_ARG_LIST,	\
	char *: WFD_ARG_STR,					\
	void *: WFD_ARG_PTR,					\
	default: WFD_ARG_CPTR					\
)

#define wfd_arg_list(...)							{							\
	.argv = (struct wfd_arg[]) {												\
		__VA_ARGS__																\
	},																			\
	.discrete = true,															\
	.dynamic = false,															\
	.len = (sizeof((struct wfd_arg[]){ __VA_ARGS__ })/sizeof(struct wfd_arg))	\
}

#define wfd_arg_get(_a, _v) ({								\
	*(_v) = _Generic(*(_v),									\
		int8_t: wfd_arg_get_i8,								\
		uint8_t: wfd_arg_get_u8,							\
		int16_t: wfd_arg_get_i16,							\
		uint16_t: wfd_arg_get_u16,							\
		int32_t: wfd_arg_get_i32,							\
		uint32_t: wfd_arg_get_u32,							\
		int64_t: wfd_arg_get_i64,							\
		uint64_t: wfd_arg_get_u64,							\
		char *: wfd_arg_get_str,							\
		const struct wfd_arg_list *: wfd_arg_get_arg_list,	\
		const char *: wfd_arg_get_cstr,						\
		void *: wfd_arg_get_ptr,							\
		default: wfd_arg_get_cptr							\
	)(_a);													\
})

#define wfd_arg_get_dictk(_a, _k) ({				\
	assert(_a);										\
	assert(WFD_ARG_DICT == (_a)->type);				\
	wfd_arg_get((_a)->k, (_k));						\
})

#define wfd_arg_get_dictv(_a, _v) ({				\
	assert(_a);										\
	assert(WFD_ARG_DICT == (_a)->type);				\
	wfd_arg_get((_a)->v, (_v));						\
})

#define wfd_arg_get_dict(_a, _k, _v) ({				\
	assert(_a);										\
	assert(WFD_ARG_DICT == (_a)->type);				\
	wfd_arg_get_dictk(_a, _k);						\
	wfd_arg_get_dictv(_a, _v);						\
})

#define wfd_arg_list_get(_l, _i, _v) ({				\
	wfd_arg_get(wfd_arg_list_at((_l), (_i)), (_v));	\
})

#define wfd_arg_list_get_dictk(_l, _i, _k) ({				\
	wfd_arg_get_dictk(wfd_arg_list_at((_l), (_i)), (_k));	\
})

#define wfd_arg_list_get_dictv(_l, _i, _v) ({				\
	wfd_arg_get_dictv(wfd_arg_list_at((_l), (_i)), (_v));	\
})

#define wfd_arg_list_get_dict(_l, _i, _k, _v) ({				\
	wfd_arg_get_dict(wfd_arg_list_at((_l), (_i)), (_k), (_v));	\
})

enum wfd_arg_type
{
	WFD_ARG_NONE,
	WFD_ARG_I8,
	WFD_ARG_I16,
	WFD_ARG_I32,
	WFD_ARG_I64,
	WFD_ARG_U8,
	WFD_ARG_U16,
	WFD_ARG_U32,
	WFD_ARG_U64,
	WFD_ARG_STR,
	WFD_ARG_CSTR,
	WFD_ARG_PTR,
	WFD_ARG_CPTR,
	WFD_ARG_DICT,
	WFD_ARG_ARG_LIST,
};

struct wfd_arg
{
	enum wfd_arg_type type;
	union
	{
		int8_t i8;
		uint8_t u8;
		int16_t i16;
		uint16_t u16;
		int32_t i32;
		uint32_t u32;
		int64_t i64;
		uint64_t u64;
		struct {
			void *ptr;
			void (*free)(void *);
		};
		struct {
			struct wfd_arg *k;
			struct wfd_arg *v;
		};
	};
};

struct wfd_arg_list
{
	size_t len: sizeof(size_t) - 2;
	bool discrete: 1;
	bool dynamic: 1;

	union {
		struct wfd_arg * argv;
		struct wfd_arg args[0];
	};
};

int wfd_arg_list_new(struct wfd_arg_list **out);
void wfd_arg_list_clear(struct wfd_arg_list *l);
static inline void wfd_arg_list_free(struct wfd_arg_list *l);
static inline const struct wfd_arg * wfd_arg_list_at(const struct wfd_arg_list *l,
				int i);

static inline enum wfd_arg_type wfd_arg_get_type(struct wfd_arg *a);
static inline void wfd_arg_free_ptr(struct wfd_arg *a);
static inline void wfd_arg_clear(struct wfd_arg *a);

static inline int8_t wfd_arg_get_i8(const struct wfd_arg *a);
static inline void wfd_arg_set_i8(struct wfd_arg *a, int8_t v);

static inline uint8_t wfd_arg_get_u8(const struct wfd_arg *a);
static inline void wfd_arg_set_u8(struct wfd_arg *a, uint8_t v);

static inline int16_t wfd_arg_get_i16(const struct wfd_arg *a);
static inline void wfd_arg_set_i16(struct wfd_arg *a, int16_t v);

static inline uint16_t wfd_arg_get_u16(const struct wfd_arg *a);
static inline void wfd_arg_set_u16(struct wfd_arg *a, uint16_t v);

static inline int32_t wfd_arg_get_i32(const struct wfd_arg *a);
static inline void wfd_arg_set_i32(struct wfd_arg *a, int32_t v);

static inline uint32_t wfd_arg_get_u32(const struct wfd_arg *a);
static inline void wfd_arg_set_u32(struct wfd_arg *a, uint32_t v);

static inline int64_t wfd_arg_get_i64(const struct wfd_arg *a);
static inline void wfd_arg_set_i64(struct wfd_arg *a, int64_t v);

static inline uint64_t wfd_arg_get_u64(const struct wfd_arg *a);
static inline void wfd_arg_set_u64(struct wfd_arg *a, uint64_t v);

static inline const char * wfd_arg_get_cstr(const struct wfd_arg *a);
static inline void wfd_arg_set_cstr(struct wfd_arg *a, const char * v);

static inline void wfd_arg_take_str(struct wfd_arg *a, char *v);
static inline char * wfd_arg_get_str(const struct wfd_arg *a);
static inline int wfd_arg_set_str(struct wfd_arg *a, const char *v);;

static inline const void * wfd_arg_get_cptr(const struct wfd_arg *a);
static inline void wfd_arg_set_cptr(struct wfd_arg *a, const void * v);

static inline void wfd_arg_take_ptr(struct wfd_arg *a, void *v, void (*f)(void *));
static inline void * wfd_arg_get_ptr(const struct wfd_arg *a);

static inline void wfd_arg_take_arg_list(struct wfd_arg *a, struct wfd_arg_list *l);
static inline const struct wfd_arg_list * wfd_arg_get_arg_list(const struct wfd_arg *a);

#include "wfd-arg.inc"

#endif /* CTL_ARG_H */
