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
#include <errno.h>
#include <assert.h>
#include <string.h>
#include "wfd-arg.h"

int wfd_arg_list_new(struct wfd_arg_list **out)
{
	assert(out);

	struct wfd_arg_list *l = calloc(1, sizeof(struct wfd_arg_list));
	if(!l) {
		return -ENOMEM;
	}

	l->dynamic = true;

	*out = l;

	return 0;
}

void wfd_arg_list_clear(struct wfd_arg_list *l)
{
	int i;
	struct wfd_arg *arg;

	if(!l || !l->dynamic) {
		return;
	}

	arg = l->discrete ? l->argv : l->args;
	for(i = 0; i < l->len; i ++) {
		if((WFD_ARG_STR == arg->type || WFD_ARG_PTR == arg->type)
						&& arg->ptr && arg->free) {
			(*arg->free)(arg->ptr);
		}
	}

	if(l->discrete) {
		free(l->argv);
	}
}

