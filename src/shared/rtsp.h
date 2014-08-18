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

#ifndef MIRACLE_RTSP_H
#define MIRACLE_RTSP_H

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <systemd/sd-event.h>

/* types */

struct rtsp;
struct rtsp_message;

#define RTSP_ANY_CODE (~0U)
#define RTSP_ANY_CHANNEL (~0U)

enum {
	RTSP_MESSAGE_UNKNOWN,
	RTSP_MESSAGE_REQUEST,
	RTSP_MESSAGE_REPLY,
	RTSP_MESSAGE_DATA,
	RTSP_MESSAGE_CNT,
};

#define RTSP_TYPE_STRING			's'
#define RTSP_TYPE_INT32				'i'
#define RTSP_TYPE_UINT32			'u'
#define RTSP_TYPE_HEX32				'h'
#define RTSP_TYPE_SKIP				'*'
#define RTSP_TYPE_RAW				'&'
#define RTSP_TYPE_HEADER_START			'<'
#define RTSP_TYPE_HEADER_END			'>'
#define RTSP_TYPE_BODY_START			'{'
#define RTSP_TYPE_BODY_END			'}'

enum {
	RTSP_CODE_CONTINUE = 100,

	RTSP_CODE_OK = 200,
	RTSP_CODE_CREATED,

	RTSP_CODE_LOW_ON_STORAGE_SPACE = 250,

	RTSP_CODE_MULTIPLE_CHOICES = 300,
	RTSP_CODE_MOVED_PERMANENTLY,
	RTSP_CODE_MOVED_TEMPORARILY,
	RTSP_CODE_SEE_OTHER,
	RTSP_CODE_NOT_MODIFIED,
	RTSP_CODE_USE_PROXY,

	RTSP_CODE_BAD_REQUEST = 400,
	RTSP_CODE_UNAUTHORIZED,
	RTSP_CODE_PAYMENT_REQUIRED,
	RTSP_CODE_FORBIDDEN,
	RTSP_CODE_NOT_FOUND,
	RTSP_CODE_METHOD_NOT_ALLOWED,
	RTSP_CODE_NOT_ACCEPTABLE,
	RTSP_CODE_PROXY_AUTHENTICATION_REQUIRED,
	RTSP_CODE_REQUEST_TIMEOUT,
	RTSP_CODE__PLACEHOLDER__1,
	RTSP_CODE_GONE,
	RTSP_CODE_LENGTH_REQUIRED,
	RTSP_CODE_PRECONDITION_FAILED,
	RTSP_CODE_REQUEST_ENTITY_TOO_LARGE,
	RTSP_CODE_REQUEST_URI_TOO_LARGE,
	RTSP_CODE_UNSUPPORTED_MEDIA_TYPE,

	RTSP_CODE_PARAMETER_NOT_UNDERSTOOD = 451,
	RTSP_CODE_CONFERENCE_NOT_FOUND,
	RTSP_CODE_NOT_ENOUGH_BANDWIDTH,
	RTSP_CODE_SESSION_NOT_FOUND,
	RTSP_CODE_METHOD_NOT_VALID_IN_THIS_STATE,
	RTSP_CODE_HEADER_FIELD_NOT_VALID_FOR_RESOURCE,
	RTSP_CODE_INVALID_RANGE,
	RTSP_CODE_PARAMETER_IS_READ_ONLY,
	RTSP_CODE_AGGREGATE_OPERATION_NOT_ALLOWED,
	RTSP_CODE_ONLY_AGGREGATE_OPERATION_ALLOWED,
	RTSP_CODE_UNSUPPORTED_TRANSPORT,
	RTSP_CODE_DESTINATION_UNREACHABLE,

	RTSP_CODE_INTERNAL_SERVER_ERROR = 500,
	RTSP_CODE_NOT_IMPLEMENTED,
	RTSP_CODE_BAD_GATEWAY,
	RTSP_CODE_SERVICE_UNAVAILABLE,
	RTSP_CODE_GATEWAY_TIMEOUT,
	RTSP_CODE_RTSP_VERSION_NOT_SUPPORTED,

	RTSP_CODE_OPTION_NOT_SUPPORTED = 551,

	RTSP_CODE_CNT
};

typedef int (*rtsp_callback_fn) (struct rtsp *bus,
				 struct rtsp_message *m,
				 void *data);

/*
 * Bus
 */

int rtsp_open(struct rtsp **out, int fd);
void rtsp_ref(struct rtsp *bus);
void rtsp_unref(struct rtsp *bus);

static inline void rtsp_unref_p(struct rtsp **bus)
{
	rtsp_unref(*bus);
}

#define _rtsp_unref_ __attribute__((__cleanup__(rtsp_unref_p)))

bool rtsp_is_dead(struct rtsp *bus);

int rtsp_attach_event(struct rtsp *bus, sd_event *event, int priority);
void rtsp_detach_event(struct rtsp *bus);

int rtsp_add_match(struct rtsp *bus, rtsp_callback_fn cb_fn, void *data);
void rtsp_remove_match(struct rtsp *bus, rtsp_callback_fn cb_fn, void *data);

int rtsp_send(struct rtsp *bus, struct rtsp_message *m);
int rtsp_call_async(struct rtsp *bus,
		    struct rtsp_message *m,
		    rtsp_callback_fn cb_fn,
		    void *data,
		    uint64_t timeout,
		    uint64_t *cookie);
void rtsp_call_async_cancel(struct rtsp *bus, uint64_t cookie);

/*
 * Messages
 */

int rtsp_message_new_request(struct rtsp *bus,
			     struct rtsp_message **out,
			     const char *method,
			     const char *uri);
int rtsp_message_new_reply(struct rtsp *bus,
			   struct rtsp_message **out,
			   uint64_t cookie,
			   unsigned int code,
			   const char *phrase);
int rtsp_message_new_reply_for(struct rtsp_message *orig,
			       struct rtsp_message **out,
			       unsigned int code,
			       const char *phrase);
int rtsp_message_new_data(struct rtsp *bus,
			  struct rtsp_message **out,
			  unsigned int channel,
			  const void *payload,
			  size_t size);
int rtsp_message_new_from_raw(struct rtsp *bus,
			      struct rtsp_message **out,
			      const void *data,
			      size_t len);
void rtsp_message_ref(struct rtsp_message *m);
void rtsp_message_unref(struct rtsp_message *m);

static inline void rtsp_message_unref_p(struct rtsp_message **m)
{
	rtsp_message_unref(*m);
}

#define _rtsp_message_unref_ __attribute__((__cleanup__(rtsp_message_unref_p)))

bool rtsp_message_is_request(struct rtsp_message *m,
			     const char *method,
			     const char *uri);
bool rtsp_message_is_reply(struct rtsp_message *m,
			   unsigned int code,
			   const char *phrase);
bool rtsp_message_is_data(struct rtsp_message *m,
			  unsigned int channel);

/* message attributes */

unsigned int rtsp_message_get_type(struct rtsp_message *m);
const char *rtsp_message_get_method(struct rtsp_message *m);
const char *rtsp_message_get_uri(struct rtsp_message *m);
unsigned int rtsp_message_get_code(struct rtsp_message *m);
const char *rtsp_message_get_phrase(struct rtsp_message *m);
unsigned int rtsp_message_get_channel(struct rtsp_message *m);
const void *rtsp_message_get_payload(struct rtsp_message *m);
size_t rtsp_message_get_payload_size(struct rtsp_message *m);

struct rtsp *rtsp_message_get_bus(struct rtsp_message *m);
uint64_t rtsp_message_get_cookie(struct rtsp_message *m);
bool rtsp_message_is_sealed(struct rtsp_message *m);

/* appending arguments */

int rtsp_message_append_line(struct rtsp_message *m, const char *line);
int rtsp_message_open_header(struct rtsp_message *m, const char *name);
int rtsp_message_close_header(struct rtsp_message *m);
int rtsp_message_open_body(struct rtsp_message *m);
int rtsp_message_close_body(struct rtsp_message *m);

int rtsp_message_append_basic(struct rtsp_message *m,
			      char type,
			      ...);
int rtsp_message_appendv_basic(struct rtsp_message *m,
			       char type,
			       va_list *args);
int rtsp_message_append(struct rtsp_message *m,
			const char *types,
			...);
int rtsp_message_appendv(struct rtsp_message *m,
			 const char *types,
			 va_list *args);

int rtsp_message_set_cookie(struct rtsp_message *m, uint64_t cookie);
int rtsp_message_seal(struct rtsp_message *m);

/* parsing arguments */

int rtsp_message_enter_header(struct rtsp_message *m, const char *name);
void rtsp_message_exit_header(struct rtsp_message *m);
int rtsp_message_enter_body(struct rtsp_message *m);
void rtsp_message_exit_body(struct rtsp_message *m);

int rtsp_message_read_basic(struct rtsp_message *m,
			    char type,
			    ...);
int rtsp_message_readv_basic(struct rtsp_message *m,
			     char type,
			     va_list *args);
int rtsp_message_read(struct rtsp_message *m,
		      const char *types,
		      ...);
int rtsp_message_readv(struct rtsp_message *m,
		       const char *types,
		       va_list *args);

int rtsp_message_skip_basic(struct rtsp_message *m, char type);
int rtsp_message_skip(struct rtsp_message *m, const char *types);

int rtsp_message_rewind(struct rtsp_message *m, bool complete);

void *rtsp_message_get_body(struct rtsp_message *m);
size_t rtsp_message_get_body_size(struct rtsp_message *m);
void *rtsp_message_get_raw(struct rtsp_message *m);
size_t rtsp_message_get_raw_size(struct rtsp_message *m);

#endif /* MIRACLE_RTSP_H */
