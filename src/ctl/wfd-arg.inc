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
#define wfd_arg_getter(_t, _s, _S) \
static inline _t wfd_arg_get_##_s(const struct wfd_arg *a) \
{ \
	assert(a); \
	assert(WFD_ARG_##_S == a->type); \
	return a->_s; \
}

#define wfd_arg_setter(_t, _s, _S) \
static inline void wfd_arg_set_##_t(struct wfd_arg *a, _t v)		\
{																	\
	assert(a);														\
	assert(!a->type || WFD_ARG_##_S == a->type);					\
	*a = (struct wfd_arg) { .type = WFD_ARG_##_S, ._s = v };		\
}

wfd_arg_getter(int8_t, i8, I8)
wfd_arg_setter(int8_t, i8, I8)
wfd_arg_getter(uint8_t, u8, U8)
wfd_arg_setter(uint8_t, u8, U8)
wfd_arg_getter(int16_t, i16, I16)
wfd_arg_setter(int16_t, i16, I16)
wfd_arg_getter(uint16_t, u16, U16)
wfd_arg_setter(uint16_t, u16, U16)
wfd_arg_getter(int32_t, i32, I32)
wfd_arg_setter(int32_t, i32, I32)
wfd_arg_getter(uint32_t, u32, U32)
wfd_arg_setter(uint32_t, u32, U32)
wfd_arg_getter(int64_t, i64, I64)
wfd_arg_setter(int64_t, i64, I64)
wfd_arg_getter(uint64_t, u64, U64)
wfd_arg_setter(uint64_t, u64, U64)

static inline void wfd_arg_list_free(struct wfd_arg_list *l)
{
	wfd_arg_list_clear(l);
	free(l);
}

static inline const struct wfd_arg * wfd_arg_list_at(const struct wfd_arg_list *l,
				int i)
{
	assert(l);
	assert(i >= 0 && i < l->len);
	return l->discrete ? &l->argv[i] : &l->args[i];
}

static inline enum wfd_arg_type wfd_arg_get_type(struct wfd_arg *a)
{
	assert(a);

	return a->type;
}

static inline void wfd_arg_free_ptr(struct wfd_arg *a)
{
	if(!a || (WFD_ARG_STR != a->type && WFD_ARG_PTR != a->type)) {
		return;
	}

	if(a->ptr && a->free) {
		(*a->free)(a->ptr);
	}
}

static inline void wfd_arg_clear(struct wfd_arg *a)
{
	if(a) {
		wfd_arg_free_ptr(a);
		memset(a, 0, sizeof(*a));
	}
}

static inline const char * wfd_arg_get_cstr(const struct wfd_arg *a)
{
	assert(a);
	assert(WFD_ARG_CSTR == a->type || WFD_ARG_STR == a->type);

	return a->ptr;
}

static inline void wfd_arg_set_cstr(struct wfd_arg *a, const char * v)
{
	assert(a);
	assert(!a->type || WFD_ARG_CSTR == a->type);

	*a = (struct wfd_arg) { .type = WFD_ARG_CSTR, .ptr = (void *) v };
}

static inline char * wfd_arg_get_str(const struct wfd_arg *a)
{
	assert(a);
	assert(WFD_ARG_STR == a->type);

	return a->ptr;
}

static inline void wfd_arg_take_str(struct wfd_arg *a, char *v)
{
	assert(a);
	assert(!a->type || WFD_ARG_STR == a->type || WFD_ARG_CSTR == a->type);

	wfd_arg_free_ptr(a);
	*a = (struct wfd_arg) { .type = WFD_ARG_STR, .ptr = v, .free = free };
}

static inline int wfd_arg_set_str(struct wfd_arg *a, const char *v)
{
	char *s;

	assert(a);
	assert(!a->type || WFD_ARG_STR == a->type);

	s = strdup(v);
	if(!s) {
		return -ENOMEM;
	}

	wfd_arg_take_str(a, s);

	return 0;
}

static inline const void * wfd_arg_get_cptr(const struct wfd_arg *a)
{
	assert(a);
	assert(WFD_ARG_PTR <= a->type && WFD_ARG_CPTR == a->type);

	return a->ptr;
}

static inline void wfd_arg_set_cptr(struct wfd_arg *a, const void * v)
{
	assert(a);
	assert(!a->type || WFD_ARG_CSTR == a->type);

	*a = (struct wfd_arg) { .type = WFD_ARG_CPTR, .ptr = (void *) v };
}

static inline void * wfd_arg_get_ptr(const struct wfd_arg *a)
{
	assert(a);
	assert(WFD_ARG_PTR == a->type || WFD_ARG_STR == a->type);

	return a->ptr;
}

static inline void wfd_arg_take_ptr(struct wfd_arg *a, void *v, void (*f)(void *))
{
	assert(a);
	assert(!a->type || WFD_ARG_PTR == a->type);

	wfd_arg_free_ptr(a);
	*a = (struct wfd_arg) { .type = WFD_ARG_PTR, .ptr = v, .free = f };
}

static inline void wfd_arg_take_arg_list(struct wfd_arg *a, struct wfd_arg_list *l)
{
	assert(a);
	assert(!a->type || WFD_ARG_ARG_LIST == a->type);

	wfd_arg_free_ptr(a);
	*a = (struct wfd_arg) { .type = WFD_ARG_ARG_LIST,
				.ptr = l,
				.free = (void (*)(void *)) wfd_arg_list_free };
}

static inline const struct wfd_arg_list * wfd_arg_get_arg_list(const struct wfd_arg *a)
{
	assert(a);
	assert(WFD_ARG_ARG_LIST == a->type);

	return a->ptr;
}
