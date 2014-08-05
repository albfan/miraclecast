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

#ifndef MIRACLE_WPAS_H
#define MIRACLE_WPAS_H

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <systemd/sd-event.h>

/* types */

struct wpas;
struct wpas_message;

typedef int (*wpas_callback_fn) (struct wpas *w,
				 struct wpas_message *m,
				 void *data);

enum {
	WPAS_MESSAGE_UNKNOWN,
	WPAS_MESSAGE_EVENT,
	WPAS_MESSAGE_REQUEST,
	WPAS_MESSAGE_REPLY,
	WPAS_TYPE_CNT,
};

enum {
	WPAS_LEVEL_UNKNOWN,
	WPAS_LEVEL_MSGDUMP,
	WPAS_LEVEL_DEBUG,
	WPAS_LEVEL_INFO,
	WPAS_LEVEL_WARNING,
	WPAS_LEVEL_ERROR,
	WPAS_LEVEL_CNT
};

#define WPAS_TYPE_STRING			's'
#define WPAS_TYPE_INT32				'i'
#define WPAS_TYPE_UINT32			'u'
#define WPAS_TYPE_DICT				'e'

/* bus */

int wpas_open(const char *ctrl_path, struct wpas **out);
int wpas_create(const char *ctrl_path, struct wpas **out);
void wpas_ref(struct wpas *w);
void wpas_unref(struct wpas *w);

int wpas_call_async(struct wpas *w,
		    struct wpas_message *m,
		    wpas_callback_fn cb_fn,
		    void *data,
		    uint64_t timeout,
		    uint64_t *cookie);
void wpas_call_async_cancel(struct wpas *w, uint64_t cookie);
int wpas_send(struct wpas *w,
	      struct wpas_message *m,
	      uint64_t timeout);

int wpas_attach_event(struct wpas *w, sd_event *event, int priority);
void wpas_detach_event(struct wpas *w);

int wpas_add_match(struct wpas *w, wpas_callback_fn cb_fn, void *data);
void wpas_remove_match(struct wpas *w, wpas_callback_fn cb_fn, void *data);

bool wpas_is_dead(struct wpas *w);
bool wpas_is_server(struct wpas *w);

static inline void wpas_unref_p(struct wpas **w)
{
	wpas_unref(*w);
}

#define _wpas_unref_ __attribute__((__cleanup__(wpas_unref_p)))

/* messages */

int wpas_message_new_event(struct wpas *w,
			   const char *name,
			   unsigned int level,
			   struct wpas_message **out);
int wpas_message_new_request(struct wpas *w,
			     const char *name,
			     struct wpas_message **out);
int wpas_message_new_reply(struct wpas *w,
			   struct wpas_message **out);
int wpas_message_new_reply_for(struct wpas *w,
			       struct wpas_message *request,
			       struct wpas_message **out);
void wpas_message_ref(struct wpas_message *m);
void wpas_message_unref(struct wpas_message *m);

bool wpas_message_is_event(struct wpas_message *msg, const char *name);
bool wpas_message_is_request(struct wpas_message *msg, const char *name);
bool wpas_message_is_reply(struct wpas_message *msg);
bool wpas_message_is_ok(struct wpas_message *msg);
bool wpas_message_is_fail(struct wpas_message *msg);

uint64_t wpas_message_get_cookie(struct wpas_message *msg);
struct wpas *wpas_message_get_bus(struct wpas_message *msg);
unsigned int wpas_message_get_type(struct wpas_message *msg);
unsigned int wpas_message_get_level(struct wpas_message *msg);
const char *wpas_message_get_name(struct wpas_message *msg);
const char *wpas_message_get_raw(struct wpas_message *msg);
const char *wpas_message_get_ifname(struct wpas_message *msg);
bool wpas_message_is_sealed(struct wpas_message *msg);

const char *wpas_message_get_peer(struct wpas_message *msg);
char *wpas_message_get_escaped_peer(struct wpas_message *msg);
void wpas_message_set_peer(struct wpas_message *msg, const char *peer);

int wpas_message_append_basic(struct wpas_message *m, char type, ...);
int wpas_message_appendv_basic(struct wpas_message *m,
			       char type,
			       va_list *args);
int wpas_message_append(struct wpas_message *m, const char *types, ...);
int wpas_message_appendv(struct wpas_message *m,
			 const char *types,
			 va_list *args);
int wpas_message_seal(struct wpas_message *m);

int wpas_message_read_basic(struct wpas_message *m, char type, void *out);
int wpas_message_read(struct wpas_message *m, const char *types, ...);
int wpas_message_skip_basic(struct wpas_message *m, char type);
int wpas_message_skip(struct wpas_message *m, const char *types);
void wpas_message_rewind(struct wpas_message *m);

int wpas_message_argv_read(struct wpas_message *m,
			   unsigned int pos,
			   char type,
			   void *out);
int wpas_message_dict_read(struct wpas_message *m,
			   const char *name,
			   char type,
			   void *out);

static inline void wpas_message_unref_p(struct wpas_message **m)
{
	wpas_message_unref(*m);
}

#define _wpas_message_unref_ __attribute__((__cleanup__(wpas_message_unref_p)))

#endif /* MIRACLE_WPAS_H */
