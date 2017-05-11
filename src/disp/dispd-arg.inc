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
#define dispd_arg_getter(_t, _s, _S) \
static inline _t dispd_arg_get_##_s(const struct dispd_arg *a) \
{ \
	assert(a); \
	assert(DISPD_ARG_##_S == a->type); \
	return a->_s; \
}

#define dispd_arg_setter(_t, _s, _S) \
static inline void dispd_arg_set_##_s(struct dispd_arg *a, _t v)		\
{																	\
	assert(a);														\
	assert(!a->type || DISPD_ARG_##_S == a->type);					\
	*a = (struct dispd_arg) { .type = DISPD_ARG_##_S, ._s = v };		\
}

dispd_arg_getter(int8_t, i8, I8)
dispd_arg_setter(int8_t, i8, I8)
dispd_arg_getter(uint8_t, u8, U8)
dispd_arg_setter(uint8_t, u8, U8)
dispd_arg_getter(int16_t, i16, I16)
dispd_arg_setter(int16_t, i16, I16)
dispd_arg_getter(uint16_t, u16, U16)
dispd_arg_setter(uint16_t, u16, U16)
dispd_arg_getter(int32_t, i32, I32)
dispd_arg_setter(int32_t, i32, I32)
dispd_arg_getter(uint32_t, u32, U32)
dispd_arg_setter(uint32_t, u32, U32)
dispd_arg_getter(int64_t, i64, I64)
dispd_arg_setter(int64_t, i64, I64)
dispd_arg_getter(uint64_t, u64, U64)
dispd_arg_setter(uint64_t, u64, U64)

static inline void dispd_arg_list_free(struct dispd_arg_list *l)
{
	dispd_arg_list_clear(l);
	free(l);
}

static inline const struct dispd_arg * dispd_arg_list_at(const struct dispd_arg_list *l,
				int i)
{
	assert(l);
	assert(i >= 0 && i < l->len);
	return l->discrete ? &l->argv[i] : &l->args[i];
}

static inline enum dispd_arg_type dispd_arg_get_type(struct dispd_arg *a)
{
	assert(a);

	return a->type;
}

static inline void dispd_arg_free_ptr(struct dispd_arg *a)
{
	if(!a || (DISPD_ARG_STR != a->type && DISPD_ARG_PTR != a->type)) {
		return;
	}

	if(a->ptr && a->free) {
		(*a->free)(a->ptr);
	}
}

static inline void dispd_arg_clear(struct dispd_arg *a)
{
	if(a) {
		dispd_arg_free_ptr(a);
		memset(a, 0, sizeof(*a));
	}
}

static inline const char * dispd_arg_get_cstr(const struct dispd_arg *a)
{
	assert(a);
	assert(DISPD_ARG_CSTR == a->type || DISPD_ARG_STR == a->type);

	return a->ptr;
}

static inline void dispd_arg_set_cstr(struct dispd_arg *a, const char * v)
{
	assert(a);
	assert(!a->type || DISPD_ARG_CSTR == a->type);

	*a = (struct dispd_arg) { .type = DISPD_ARG_CSTR, .ptr = (void *) v };
}

static inline char * dispd_arg_get_str(const struct dispd_arg *a)
{
	assert(a);
	assert(DISPD_ARG_STR == a->type);

	return a->ptr;
}

static inline void dispd_arg_take_str(struct dispd_arg *a, char *v)
{
	assert(a);
	assert(!a->type || DISPD_ARG_STR == a->type || DISPD_ARG_CSTR == a->type);

	dispd_arg_free_ptr(a);
	*a = (struct dispd_arg) { .type = DISPD_ARG_STR, .ptr = v, .free = free };
}

static inline int dispd_arg_set_str(struct dispd_arg *a, const char *v)
{
	char *s;

	assert(a);
	assert(!a->type || DISPD_ARG_STR == a->type);

	s = strdup(v);
	if(!s) {
		return -ENOMEM;
	}

	dispd_arg_take_str(a, s);

	return 0;
}

static inline const void * dispd_arg_get_cptr(const struct dispd_arg *a)
{
	assert(a);
	assert(DISPD_ARG_PTR <= a->type && DISPD_ARG_CPTR == a->type);

	return a->ptr;
}

static inline void dispd_arg_set_cptr(struct dispd_arg *a, const void * v)
{
	assert(a);
	assert(!a->type || DISPD_ARG_CSTR == a->type);

	*a = (struct dispd_arg) { .type = DISPD_ARG_CPTR, .ptr = (void *) v };
}

static inline void * dispd_arg_get_ptr(const struct dispd_arg *a)
{
	assert(a);
	assert(DISPD_ARG_PTR == a->type || DISPD_ARG_STR == a->type);

	return a->ptr;
}

static inline void dispd_arg_take_ptr(struct dispd_arg *a, void *v, void (*f)(void *))
{
	assert(a);
	assert(!a->type || DISPD_ARG_PTR == a->type);

	dispd_arg_free_ptr(a);
	*a = (struct dispd_arg) { .type = DISPD_ARG_PTR, .ptr = v, .free = f };
}

static inline void dispd_arg_take_arg_list(struct dispd_arg *a, struct dispd_arg_list *l)
{
	assert(a);
	assert(!a->type || DISPD_ARG_ARG_LIST == a->type);

	dispd_arg_free_ptr(a);
	*a = (struct dispd_arg) { .type = DISPD_ARG_ARG_LIST,
				.ptr = l,
				.free = (void (*)(void *)) dispd_arg_list_free };
}

static inline const struct dispd_arg_list * dispd_arg_get_arg_list(const struct dispd_arg *a)
{
	assert(a);
	assert(DISPD_ARG_ARG_LIST == a->type);

	return a->ptr;
}
