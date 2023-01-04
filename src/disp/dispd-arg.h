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

#ifndef DISPD_ARG_H
#define DISPD_ARG_H

#define dispd_arg_i8(_v) { .type = DISPD_ARG_I8, .i8 = (_v) }
#define dispd_arg_u8(_v) { .type = DISPD_ARG_U8, .u8 = (_v) }
#define dispd_arg_i16(_v) { .type = DISPD_ARG_I16, .i16 = (_v) }
#define dispd_arg_u16(_v) { .type = DISPD_ARG_U16, .u16 = (_v) }
#define dispd_arg_i32(_v) { .type = DISPD_ARG_I32, .i32 = (_v) }
#define dispd_arg_u32(_v) { .type = DISPD_ARG_U32, .u32 = (_v) }
#define dispd_arg_i64(_v) { .type = DISPD_ARG_I64, .i64 = (_v) }
#define dispd_arg_u64(_v) { .type = DISPD_ARG_U64, .u64 = (_v) }
#define dispd_arg_cstr(_v) { .type = DISPD_ARG_CSTR, .ptr = (_v) }
#define dispd_arg_cptr(_v) { .type = DISPD_ARG_CPTR, .ptr = (_v) }
#define dispd_arg_arg_list(_v) {									\
				.type = DISPD_ARG_ARG_LIST,						\
				.ptr = &(struct dispd_arg_list) dispd_arg_list(_v)	\
}
#define dispd_arg_dict(_k, _v) {		\
	.type = DISPD_ARG_DICT,			\
	.k = (struct dispd_arg[]){_k},	\
	.v = (struct dispd_arg[]){_v}		\
}

#if INT_MAX == INT64_MAX
#define dispd_arg_i(_v) dispd_arg_i64(_v)
#define dispd_arg_u(_v) dispd_arg_u64(_v)
#elif INT_MAX == INT32_MAX
#define dispd_arg_i(_v) dispd_arg_i32(_v)
#define dispd_arg_u(_v) dispd_arg_u32(_v)
#else
#error unsupported int size
#endif

#define dispd_arg_type_id(_t) _Generic((_t),	\
	int8_t: DISPD_ARG_I8,						\
	uint8_t: DISPD_ARG_U8,					\
	int16_t: DISPD_ARG_I16,					\
	uint16_t: DISPD_ARG_U16,					\
	int32_t: DISPD_ARG_I32,					\
	uint32_t: DISPD_ARG_U32,					\
	int64_t: DISPD_ARG_I64,					\
	uint64_t: DISPD_ARG_U64,					\
	const char *: DISPD_ARG_CSTR,				\
	const dispd_arg_list *: DISPD_ARG_ARG_LIST,	\
	char *: DISPD_ARG_STR,					\
	void *: DISPD_ARG_PTR,					\
	default: DISPD_ARG_CPTR					\
)

#define dispd_arg_list(...)							{							\
	.argv = (struct dispd_arg[]) {												\
		__VA_ARGS__																\
	},																			\
	.discrete = true,															\
	.dynamic = false,															\
	.len = (sizeof((struct dispd_arg[]){ __VA_ARGS__ })/sizeof(struct dispd_arg))	\
}

#define dispd_arg_get(_a, _v) ({								\
	*(_v) = _Generic(*(_v),									\
		int8_t: dispd_arg_get_i8,								\
		uint8_t: dispd_arg_get_u8,							\
		int16_t: dispd_arg_get_i16,							\
		uint16_t: dispd_arg_get_u16,							\
		int32_t: dispd_arg_get_i32,							\
		uint32_t: dispd_arg_get_u32,							\
		int64_t: dispd_arg_get_i64,							\
		uint64_t: dispd_arg_get_u64,							\
		char *: dispd_arg_get_str,							\
		const struct dispd_arg_list *: dispd_arg_get_arg_list,	\
		const char *: dispd_arg_get_cstr,						\
		void *: dispd_arg_get_ptr,							\
		default: dispd_arg_get_cptr							\
	)(_a);													\
})

#define dispd_arg_get_dictk(_a, _k) ({				\
	assert(_a);										\
	assert(DISPD_ARG_DICT == (_a)->type);				\
	dispd_arg_get((_a)->k, (_k));						\
})

#define dispd_arg_get_dictv(_a, _v) ({				\
	assert(_a);										\
	assert(DISPD_ARG_DICT == (_a)->type);				\
	dispd_arg_get((_a)->v, (_v));						\
})

#define dispd_arg_get_dict(_a, _k, _v) ({				\
	assert(_a);										\
	assert(DISPD_ARG_DICT == (_a)->type);				\
	dispd_arg_get_dictk(_a, _k);						\
	dispd_arg_get_dictv(_a, _v);						\
})

#define dispd_arg_list_get(_l, _i, _v) ({				\
	dispd_arg_get(dispd_arg_list_at((_l), (_i)), (_v));	\
})

#define dispd_arg_list_get_dictk(_l, _i, _k) ({				\
	dispd_arg_get_dictk(dispd_arg_list_at((_l), (_i)), (_k));	\
})

#define dispd_arg_list_get_dictv(_l, _i, _v) ({				\
	dispd_arg_get_dictv(dispd_arg_list_at((_l), (_i)), (_v));	\
})

#define dispd_arg_list_get_dict(_l, _i, _k, _v) ({				\
	dispd_arg_get_dict(dispd_arg_list_at((_l), (_i)), (_k), (_v));	\
})

enum dispd_arg_type
{
	DISPD_ARG_NONE,
	DISPD_ARG_I8,
	DISPD_ARG_I16,
	DISPD_ARG_I32,
	DISPD_ARG_I64,
	DISPD_ARG_U8,
	DISPD_ARG_U16,
	DISPD_ARG_U32,
	DISPD_ARG_U64,
	DISPD_ARG_STR,
	DISPD_ARG_CSTR,
	DISPD_ARG_PTR,
	DISPD_ARG_CPTR,
	DISPD_ARG_DICT,
	DISPD_ARG_ARG_LIST,
};

struct dispd_arg
{
	enum dispd_arg_type type;
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
			struct dispd_arg *k;
			struct dispd_arg *v;
		};
	};
};

struct dispd_arg_list
{
	size_t len: sizeof(size_t) - 2;
	bool discrete: 1;
	bool dynamic: 1;

	union {
		struct dispd_arg * argv;
		struct dispd_arg args[0];
	};
};

int dispd_arg_list_new(struct dispd_arg_list **out);
void dispd_arg_list_clear(struct dispd_arg_list *l);
static inline void dispd_arg_list_free(struct dispd_arg_list *l);
static inline const struct dispd_arg * dispd_arg_list_at(const struct dispd_arg_list *l,
				int i);

static inline enum dispd_arg_type dispd_arg_get_type(struct dispd_arg *a);
static inline void dispd_arg_free_ptr(struct dispd_arg *a);
static inline void dispd_arg_clear(struct dispd_arg *a);

static inline int8_t dispd_arg_get_i8(const struct dispd_arg *a);
static inline void dispd_arg_set_i8(struct dispd_arg *a, int8_t v);

static inline uint8_t dispd_arg_get_u8(const struct dispd_arg *a);
static inline void dispd_arg_set_u8(struct dispd_arg *a, uint8_t v);

static inline int16_t dispd_arg_get_i16(const struct dispd_arg *a);
static inline void dispd_arg_set_i16(struct dispd_arg *a, int16_t v);

static inline uint16_t dispd_arg_get_u16(const struct dispd_arg *a);
static inline void dispd_arg_set_u16(struct dispd_arg *a, uint16_t v);

static inline int32_t dispd_arg_get_i32(const struct dispd_arg *a);
static inline void dispd_arg_set_i32(struct dispd_arg *a, int32_t v);

static inline uint32_t dispd_arg_get_u32(const struct dispd_arg *a);
static inline void dispd_arg_set_u32(struct dispd_arg *a, uint32_t v);

static inline int64_t dispd_arg_get_i64(const struct dispd_arg *a);
static inline void dispd_arg_set_i64(struct dispd_arg *a, int64_t v);

static inline uint64_t dispd_arg_get_u64(const struct dispd_arg *a);
static inline void dispd_arg_set_u64(struct dispd_arg *a, uint64_t v);

static inline const char * dispd_arg_get_cstr(const struct dispd_arg *a);
static inline void dispd_arg_set_cstr(struct dispd_arg *a, const char * v);

static inline void dispd_arg_take_str(struct dispd_arg *a, char *v);
static inline char * dispd_arg_get_str(const struct dispd_arg *a);
static inline int dispd_arg_set_str(struct dispd_arg *a, const char *v);;

static inline const void * dispd_arg_get_cptr(const struct dispd_arg *a);
static inline void dispd_arg_set_cptr(struct dispd_arg *a, const void * v);

static inline void dispd_arg_take_ptr(struct dispd_arg *a, void *v, void (*f)(void *));
static inline void * dispd_arg_get_ptr(const struct dispd_arg *a);

static inline void dispd_arg_take_arg_list(struct dispd_arg *a, struct dispd_arg_list *l);
static inline const struct dispd_arg_list * dispd_arg_get_arg_list(const struct dispd_arg *a);

#include "dispd-arg.inc"

#endif /* DISPD_ARG_H */
