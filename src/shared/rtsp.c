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
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <systemd/sd-event.h>
#include <time.h>
#include <unistd.h>
#include "rtsp.h"
#include "shl_dlist.h"
#include "shl_htable.h"
#include "shl_macro.h"
#include "shl_ring.h"
#include "shl_util.h"

/* 5s default timeout for messages */
#define RTSP_DEFAULT_TIMEOUT (5ULL * 1000ULL * 1000ULL)

/* CSeq numbers have separate namespaces for locally and remotely generated
 * messages. We use a single lookup-table, so mark all remotely generated
 * cookies as such to avoid conflicts with local cookies. */
#define RTSP_FLAG_REMOTE_COOKIE 0x8000000000000000ULL

struct rtsp {
	unsigned long ref;
	uint64_t cookies;
	int fd;
	sd_event_source *fd_source;

	sd_event *event;
	int64_t priority;
	struct shl_dlist matches;

	/* outgoing messages */
	struct shl_dlist outgoing;
	size_t outgoing_cnt;

	/* waiting messages */
	struct shl_htable waiting;
	size_t waiting_cnt;

	/* ring parser */
	struct rtsp_parser {
		struct rtsp_message *m;
		struct shl_ring buf;
		size_t buflen;

		enum {
			STATE_NEW,
			STATE_HEADER,
			STATE_HEADER_QUOTE,
			STATE_HEADER_NL,
			STATE_BODY,
			STATE_DATA_HEAD,
			STATE_DATA_BODY,
		} state;

		char last_chr;
		size_t remaining_body;

		size_t data_size;
		uint8_t data_channel;

		bool quoted : 1;
		bool dead : 1;
	} parser;

	bool is_dead : 1;
	bool is_calling : 1;
};

struct rtsp_match {
	struct shl_dlist list;
	rtsp_callback_fn cb_fn;
	void *data;

	bool is_removed : 1;
};

struct rtsp_header {
	char *key;
	char *value;
	size_t token_cnt;
	size_t token_used;
	char **tokens;
	char *line;
	size_t line_len;
};

struct rtsp_message {
	unsigned long ref;
	struct rtsp *bus;
	struct shl_dlist list;

	unsigned int type;
	uint64_t cookie;
	unsigned int major;
	unsigned int minor;

	/* unknown specific */
	char *unknown_head;

	/* request specific */
	char *request_method;
	char *request_uri;

	/* reply specific */
	unsigned int reply_code;
	char *reply_phrase;

	/* data specific */
	unsigned int data_channel;
	uint8_t *data_payload;
	size_t data_size;

	/* iterators */
	bool iter_body;
	struct rtsp_header *iter_header;
	size_t iter_token;

	/* headers */
	size_t header_cnt;
	size_t header_used;
	struct rtsp_header *headers;
	struct rtsp_header *header_clen;
	struct rtsp_header *header_ctype;
	struct rtsp_header *header_cseq;

	/* body */
	uint8_t *body;
	size_t body_size;
	size_t body_cnt;
	size_t body_used;
	struct rtsp_header *body_headers;

	/* transmission */
	sd_event_source *timer_source;
	rtsp_callback_fn cb_fn;
	void *fn_data;
	uint64_t timeout;
	uint8_t *raw;
	size_t raw_size;
	size_t sent;

	bool is_used : 1;
	bool is_sealed : 1;
	bool is_outgoing : 1;
	bool is_waiting : 1;
	bool is_sending : 1;
};

#define rtsp_message_from_htable(_p) \
	shl_htable_entry((_p), struct rtsp_message, cookie)

#define RTSP_FOREACH_WAITING(_i, _bus) \
	SHL_HTABLE_FOREACH_MACRO(_i, &(_bus)->waiting, rtsp_message_from_htable)

#define RTSP_FIRST_WAITING(_bus) \
	SHL_HTABLE_FIRST_MACRO(&(_bus)->waiting, rtsp_message_from_htable)

static void rtsp_free_match(struct rtsp_match *match);
static void rtsp_drop_message(struct rtsp_message *m);
static int rtsp_incoming_message(struct rtsp_message *m);

/*
 * Helpers
 * Some helpers that don't really belong into a specific group.
 */

static const char *code_descriptions[] = {
	[RTSP_CODE_CONTINUE]					= "Continue",

	[RTSP_CODE_OK]						= "OK",
	[RTSP_CODE_CREATED]					= "Created",

	[RTSP_CODE_LOW_ON_STORAGE_SPACE]			= "Low on Storage Space",

	[RTSP_CODE_MULTIPLE_CHOICES]				= "Multiple Choices",
	[RTSP_CODE_MOVED_PERMANENTLY]				= "Moved Permanently",
	[RTSP_CODE_MOVED_TEMPORARILY]				= "Moved Temporarily",
	[RTSP_CODE_SEE_OTHER]					= "See Other",
	[RTSP_CODE_NOT_MODIFIED]				= "Not Modified",
	[RTSP_CODE_USE_PROXY]					= "Use Proxy",

	[RTSP_CODE_BAD_REQUEST]					= "Bad Request",
	[RTSP_CODE_UNAUTHORIZED]				= "Unauthorized",
	[RTSP_CODE_PAYMENT_REQUIRED]				= "Payment Required",
	[RTSP_CODE_FORBIDDEN]					= "Forbidden",
	[RTSP_CODE_NOT_FOUND]					= "Not Found",
	[RTSP_CODE_METHOD_NOT_ALLOWED]				= "Method not Allowed",
	[RTSP_CODE_NOT_ACCEPTABLE]				= "Not Acceptable",
	[RTSP_CODE_PROXY_AUTHENTICATION_REQUIRED]		= "Proxy Authentication Required",
	[RTSP_CODE_REQUEST_TIMEOUT]				= "Request Time-out",
	[RTSP_CODE_GONE]					= "Gone",
	[RTSP_CODE_LENGTH_REQUIRED]				= "Length Required",
	[RTSP_CODE_PRECONDITION_FAILED]				= "Precondition Failed",
	[RTSP_CODE_REQUEST_ENTITY_TOO_LARGE]			= "Request Entity Too Large",
	[RTSP_CODE_REQUEST_URI_TOO_LARGE]			= "Request-URI too Large",
	[RTSP_CODE_UNSUPPORTED_MEDIA_TYPE]			= "Unsupported Media Type",

	[RTSP_CODE_PARAMETER_NOT_UNDERSTOOD]			= "Parameter not Understood",
	[RTSP_CODE_CONFERENCE_NOT_FOUND]			= "Conference not Found",
	[RTSP_CODE_NOT_ENOUGH_BANDWIDTH]			= "Not Enough Bandwidth",
	[RTSP_CODE_SESSION_NOT_FOUND]				= "Session not Found",
	[RTSP_CODE_METHOD_NOT_VALID_IN_THIS_STATE]		= "Method not Valid in this State",
	[RTSP_CODE_HEADER_FIELD_NOT_VALID_FOR_RESOURCE]		= "Header Field not Valid for Resource",
	[RTSP_CODE_INVALID_RANGE]				= "Invalid Range",
	[RTSP_CODE_PARAMETER_IS_READ_ONLY]			= "Parameter is Read-only",
	[RTSP_CODE_AGGREGATE_OPERATION_NOT_ALLOWED]		= "Aggregate Operation not Allowed",
	[RTSP_CODE_ONLY_AGGREGATE_OPERATION_ALLOWED]		= "Only Aggregate Operation Allowed",
	[RTSP_CODE_UNSUPPORTED_TRANSPORT]			= "Unsupported Transport",
	[RTSP_CODE_DESTINATION_UNREACHABLE]			= "Destination Unreachable",

	[RTSP_CODE_INTERNAL_SERVER_ERROR]			= "Internal Server Error",
	[RTSP_CODE_NOT_IMPLEMENTED]				= "Not Implemented",
	[RTSP_CODE_BAD_GATEWAY]					= "Bad Gateway",
	[RTSP_CODE_SERVICE_UNAVAILABLE]				= "Service Unavailable",
	[RTSP_CODE_GATEWAY_TIMEOUT]				= "Gateway Time-out",
	[RTSP_CODE_RTSP_VERSION_NOT_SUPPORTED]			= "RTSP Version not Supported",

	[RTSP_CODE_OPTION_NOT_SUPPORTED]			= "Option not Supported",

	[RTSP_CODE_CNT]						= NULL,
};

static const char *get_code_description(unsigned int code)
{
	const char *error = "Internal Error";

	if (code >= SHL_ARRAY_LENGTH(code_descriptions))
		return error;

	return code_descriptions[code] ? : error;
}

static size_t sanitize_line(char *line, size_t len)
{
	char *src, *dst, c, prev, last_c;
	size_t i;
	bool quoted, escaped;

	src = line;
	dst = line;
	last_c = 0;
	quoted = false;
	escaped = false;

	for (i = 0; i < len; ++i) {
		c = *src++;
		prev = last_c;
		last_c = c;

		if (escaped) {
			escaped = false;
			/* turn escaped binary zero into "\0" */
			if (c == '\0') {
				c = '0';
				last_c = c;
			}
		} else if (quoted) {
			if (c == '"') {
				quoted = false;
			} else if (c == '\0') {
				/* skip binary 0 */
				last_c = prev;
				continue;
			} else if (c == '\\') {
				escaped = true;
			}
		} else {
			/* ignore any binary 0 */
			if (c == '\0') {
				last_c = prev;
				continue;
			}

			/* turn new-lines/tabs into white-space */
			if (c == '\r' || c == '\n' || c == '\t') {
				c = ' ';
				last_c = c;
			}

			/* trim whitespace */
			if (c == ' ' && prev == ' ')
				continue;

			/* trim leading whitespace */
			if (c == ' ' && dst == line)
				continue;

			if (c == '"')
				quoted = true;
		}

		*dst++ = c;
	}

	/* terminate string with binary zero */
	*dst = 0;

	/* remove trailing whitespace */
	if (!quoted) {
		while (dst > line && *(dst - 1) == ' ')
			*--dst = 0;
	}

	return dst - line;
}

/*
 * Messages
 * The message-layer is responsible of message handling for users. It does not
 * do the wire-protocol parsing! It is solely responsible for the user API to
 * assemble and inspect messages.
 *
 * We use per-message iterators to allow simply message-assembly and parsing in
 * a sequential manner. We do some limited container-formats, so you can dive
 * into a header, parse its contents and exit it again.
 *
 * Note that messages provide sealing-capabilities. Once a message is sealed,
 * it can never be modified again. All messages that are submitted to the bus
 * layer, or are received from the bus layer, are always sealed.
 */

static int rtsp_message_new(struct rtsp *bus,
			    struct rtsp_message **out)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;

	if (!bus || !out)
		return -EINVAL;

	m = calloc(1, sizeof(*m));
	if (!m)
		return -ENOMEM;

	m->ref = 1;
	m->bus = bus;
	rtsp_ref(bus);
	m->type = RTSP_MESSAGE_UNKNOWN;
	m->major = 1;
	m->minor = 0;

	*out = m;
	m = NULL;
	return 0;
}

static int rtsp_message_new_unknown(struct rtsp *bus,
				    struct rtsp_message **out,
				    const char *head)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r;

	if (!bus || !out || !head)
		return -EINVAL;

	r = rtsp_message_new(bus, &m);
	if (r < 0)
		return r;

	m->type = RTSP_MESSAGE_UNKNOWN;
	m->unknown_head = strdup(head);
	if (!m->unknown_head)
		return -ENOMEM;

	*out = m;
	m = NULL;
	return 0;
}

static int rtsp_message_new_request_n(struct rtsp *bus,
				      struct rtsp_message **out,
				      const char *method,
				      size_t methodlen,
				      const char *uri,
				      size_t urilen)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r;

	if (!bus || !out)
		return -EINVAL;
	if (shl_isempty(method) || shl_isempty(uri) || !methodlen || !urilen)
		return -EINVAL;

	r = rtsp_message_new(bus, &m);
	if (r < 0)
		return r;

	m->type = RTSP_MESSAGE_REQUEST;

	m->request_method = strndup(method, methodlen);
	if (!m->request_method)
		return -ENOMEM;

	m->request_uri = strndup(uri, urilen);
	if (!m->request_uri)
		return -ENOMEM;

	*out = m;
	m = NULL;
	return 0;
}

int rtsp_message_new_request(struct rtsp *bus,
			     struct rtsp_message **out,
			     const char *method,
			     const char *uri)
{
	if (!method || !uri)
		return -EINVAL;

	return rtsp_message_new_request_n(bus,
					  out,
					  method,
					  strlen(method),
					  uri,
					  strlen(uri));
}

static int rtsp_message_new_raw_reply(struct rtsp *bus,
				      struct rtsp_message **out,
				      unsigned int code,
				      const char *phrase)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r;

	if (!bus || !out)
		return -EINVAL;
	if (code == RTSP_ANY_CODE)
		return -EINVAL;

	r = rtsp_message_new(bus, &m);
	if (r < 0)
		return r;

	m->type = RTSP_MESSAGE_REPLY;
	m->reply_code = code;

	if (shl_isempty(phrase))
		m->reply_phrase = strdup(get_code_description(code));
	else
		m->reply_phrase = strdup(phrase);
	if (!m->reply_phrase)
		return -ENOMEM;

	*out = m;
	m = NULL;
	return 0;
}

int rtsp_message_new_reply(struct rtsp *bus,
			   struct rtsp_message **out,
			   uint64_t cookie,
			   unsigned int code,
			   const char *phrase)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r;

	if (!bus || !out || !cookie)
		return -EINVAL;

	r = rtsp_message_new_raw_reply(bus, &m, code, phrase);
	if (r < 0)
		return r;

	m->cookie = cookie | RTSP_FLAG_REMOTE_COOKIE;

	*out = m;
	m = NULL;
	return 0;
}

int rtsp_message_new_reply_for(struct rtsp_message *orig,
			       struct rtsp_message **out,
			       unsigned int code,
			       const char *phrase)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r;

	if (!orig || !out)
		return -EINVAL;
	/* @orig must be a message received from the remote peer */
	if (!orig->is_used || !(orig->cookie & RTSP_FLAG_REMOTE_COOKIE))
		return -EINVAL;

	r = rtsp_message_new_reply(orig->bus, &m, orig->cookie, code, phrase);
	if (r < 0)
		return r;

	*out = m;
	m = NULL;
	return 0;
}

int rtsp_message_new_data(struct rtsp *bus,
			  struct rtsp_message **out,
			  unsigned int channel,
			  const void *payload,
			  size_t size)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r;

	if (!bus || !out)
		return -EINVAL;
	if (channel == RTSP_ANY_CHANNEL)
		return -EINVAL;
	if (size > 0 && !payload)
		return -EINVAL;

	r = rtsp_message_new(bus, &m);
	if (r < 0)
		return r;

	m->type = RTSP_MESSAGE_DATA;

	m->data_channel = channel;
	m->data_size = size;
	if (size > 0) {
		m->data_payload = malloc(size);
		if (!m->data_payload)
			return -ENOMEM;

		memcpy(m->data_payload, payload, size);
	}

	*out = m;
	m = NULL;
	return 0;
}

void rtsp_message_ref(struct rtsp_message *m)
{
	if (!m || !m->ref)
		return;

	++m->ref;
}

void rtsp_message_unref(struct rtsp_message *m)
{
	size_t i;

	if (!m || !m->ref || --m->ref)
		return;

	for (i = 0; i < m->body_used; ++i) {
		free(m->body_headers[i].key);
		free(m->body_headers[i].value);
		shl_strv_free(m->body_headers[i].tokens);
		free(m->body_headers[i].line);
	}
	free(m->body_headers);

	for (i = 0; i < m->header_used; ++i) {
		free(m->headers[i].key);
		free(m->headers[i].value);
		shl_strv_free(m->headers[i].tokens);
		free(m->headers[i].line);
	}
	free(m->headers);

	free(m->raw);
	free(m->body);

	free(m->data_payload);
	free(m->reply_phrase);
	free(m->request_uri);
	free(m->request_method);
	free(m->unknown_head);

	rtsp_unref(m->bus);
	free(m);
}

bool rtsp_message_is_request(struct rtsp_message *m,
			     const char *method,
			     const char *uri)
{
	return m && m->type == RTSP_MESSAGE_REQUEST &&
	       (!method || !strcasecmp(m->request_method, method)) &&
	       (!uri || !strcmp(m->request_uri, uri));
}

bool rtsp_message_is_reply(struct rtsp_message *m,
			   unsigned int code,
			   const char *phrase)
{
	return m && m->type == RTSP_MESSAGE_REPLY &&
	       (code == RTSP_ANY_CODE || m->reply_code == code) &&
	       (!phrase || !strcmp(m->reply_phrase, phrase));
}

bool rtsp_message_is_data(struct rtsp_message *m,
			  unsigned int channel)
{
	return m && m->type == RTSP_MESSAGE_DATA &&
	       (channel == RTSP_ANY_CHANNEL || m->data_channel == channel);
}

unsigned int rtsp_message_get_type(struct rtsp_message *m)
{
	if (!m)
		return RTSP_MESSAGE_UNKNOWN;

	return m->type;
}

const char *rtsp_message_get_method(struct rtsp_message *m)
{
	if (!m)
		return NULL;

	return m->request_method;
}

const char *rtsp_message_get_uri(struct rtsp_message *m)
{
	if (!m)
		return NULL;

	return m->request_uri;
}

unsigned int rtsp_message_get_code(struct rtsp_message *m)
{
	if (!m)
		return RTSP_ANY_CODE;

	return m->reply_code;
}

const char *rtsp_message_get_phrase(struct rtsp_message *m)
{
	if (!m)
		return NULL;

	return m->reply_phrase;
}

unsigned int rtsp_message_get_channel(struct rtsp_message *m)
{
	if (!m)
		return RTSP_ANY_CHANNEL;

	return m->data_channel;
}

const void *rtsp_message_get_payload(struct rtsp_message *m)
{
	if (!m)
		return NULL;

	return m->data_payload;
}

size_t rtsp_message_get_payload_size(struct rtsp_message *m)
{
	if (!m)
		return 0;

	return m->data_size;
}

struct rtsp *rtsp_message_get_bus(struct rtsp_message *m)
{
	if (!m)
		return NULL;

	return m->bus;
}

uint64_t rtsp_message_get_cookie(struct rtsp_message *m)
{
	if (!m)
		return 0;

	return m->cookie & ~RTSP_FLAG_REMOTE_COOKIE;
}

bool rtsp_message_is_sealed(struct rtsp_message *m)
{
	return m && m->is_sealed;
}

static int rtsp_header_set_value(struct rtsp_header *h,
				 const char *value,
				 size_t valuelen,
				 bool force)
{
	int r;

	if (!valuelen || shl_isempty(value))
		return -EINVAL;

	if (!force) {
		if (h->value || h->token_used || h->line)
			return -EINVAL;
	} else {
		shl_strv_free(h->tokens);
		h->tokens = NULL;
		h->token_used = 0;
		h->token_cnt = 0;

		free(h->value);
		h->value = NULL;

		free(h->line);
		h->line = NULL;
	}

	h->value = strndup(value, valuelen);
	if (!h->value)
		return -ENOMEM;

	r = shl_qstr_tokenize(value, &h->tokens);
	if (r < 0) {
		free(h->value);
		h->value = NULL;
		return -ENOMEM;
	}

	h->token_cnt = r + 1;
	h->token_used = r;

	return 0;
}

static int rtsp_message_append_header(struct rtsp_message *m,
				      struct rtsp_header **out,
				      const char *key,
				      size_t keylen,
				      const char *value,
				      size_t valuelen)
{
	struct rtsp_header *h;
	int r;

	if (!m || !out || !key)
		return -EINVAL;
	if (m->type == RTSP_MESSAGE_DATA)
		return -EINVAL;

	if (m->iter_body) {
		if (!SHL_GREEDY_REALLOC0_T(m->body_headers,
					   m->body_cnt,
					   m->body_used + 1))
			return -ENOMEM;

		h = &m->body_headers[m->body_used];
	} else {
		if (!SHL_GREEDY_REALLOC0_T(m->headers,
					   m->header_cnt,
					   m->header_used + 1))
			return -ENOMEM;

		h = &m->headers[m->header_used];
	}

	h->key = strndup(key, keylen);
	if (!h->key)
		return -ENOMEM;

	if (valuelen) {
		r = rtsp_header_set_value(h, value, valuelen, true);
		if (r < 0) {
			free(h->key);
			return -ENOMEM;
		}
	}

	if (m->iter_body) {
		++m->body_used;
	} else {
		if (!strcasecmp(h->key, "Content-Length"))
			m->header_clen = h;
		if (!strcasecmp(h->key, "Content-Type"))
			m->header_ctype = h;
		else if (!strcasecmp(h->key, "CSeq"))
			m->header_cseq = h;

		++m->header_used;
	}

	*out = h;
	return 0;
}

static int rtsp_message_append_header_line(struct rtsp_message *m,
					   struct rtsp_header **out,
					   const char *line)
{
	struct rtsp_header *h;
	const char *value;
	char *t;
	size_t keylen, valuelen;
	int r;

	if (!line)
		return -EINVAL;

	t = malloc(strlen(line) + 3);
	if (!t)
		return -ENOMEM;

	value = strchrnul(line, ':');
	keylen = value - line;
	if (*value) {
		++value;
		valuelen = strlen(value);
	} else {
		value = NULL;
		valuelen = 0;
	}

	while (keylen > 0 && line[keylen - 1] == ' ')
		--keylen;

	while (valuelen > 0 && value[valuelen - 1] == ' ')
		--valuelen;

	while (valuelen > 0 && value[0] == ' ') {
		++value;
		--valuelen;
	}

	r = rtsp_message_append_header(m,
				       &h,
				       line,
				       keylen,
				       value,
				       valuelen);
	if (r < 0) {
		free(t);
		return r;
	}

	h->line = t;
	t = stpcpy(t, line);
	*t++ = '\r';
	*t++ = '\n';
	*t = '\0';
	h->line_len = t - h->line;

	if (out)
		*out = h;

	return 0;
}

static int rtsp_header_append_token(struct rtsp_header *h, const char *token)
{
	if (!h || !token || h->line || h->value)
		return -EINVAL;

	if (!SHL_GREEDY_REALLOC0_T(h->tokens,
				   h->token_cnt,
				   h->token_used + 2))
		return -ENOMEM;

	h->tokens[h->token_used] = strdup(token);
	if (!h->tokens[h->token_used])
		return -ENOMEM;

	++h->token_used;
	return 0;
}

static int rtsp_header_serialize(struct rtsp_header *h)
{
	static char *empty_strv[1] = { NULL };
	char *t;
	int r;

	if (!h)
		return -EINVAL;
	if (h->line)
		return 0;

	if (!h->value) {
		r = shl_qstr_join(h->tokens ? : empty_strv, &h->value);
		if (r < 0)
			return r;
	}

	t = malloc(strlen(h->key) + strlen(h->value) + 5);
	if (!t)
		return -ENOMEM;

	h->line = t;
	t = stpcpy(t, h->key);
	*t++ = ':';
	*t++ = ' ';
	t = stpcpy(t, h->value);
	*t++ = '\r';
	*t++ = '\n';
	*t = '\0';
	h->line_len = t - h->line;

	return 0;
}

int rtsp_message_append_line(struct rtsp_message *m, const char *line)
{
	int r;

	if (!m || !line || m->type == RTSP_MESSAGE_DATA)
		return -EINVAL;
	if (m->is_sealed)
		return -EBUSY;
	if (m->iter_header)
		return -EINVAL;

	r = rtsp_message_append_header_line(m, NULL, line);
	if (r < 0)
		return r;

	return 0;
}

int rtsp_message_open_header(struct rtsp_message *m, const char *name)
{
	struct rtsp_header *h;
	int r;

	if (!m || shl_isempty(name) || m->type == RTSP_MESSAGE_DATA)
		return -EINVAL;
	if (m->is_sealed)
		return -EBUSY;
	if (m->iter_header)
		return -EINVAL;

	r = rtsp_message_append_header(m, &h, name, strlen(name), NULL, 0);
	if (r < 0)
		return r;

	m->iter_header = h;

	return 0;
}

int rtsp_message_close_header(struct rtsp_message *m)
{
	int r;

	if (!m || m->type == RTSP_MESSAGE_DATA)
		return -EINVAL;
	if (m->is_sealed)
		return -EBUSY;
	if (!m->iter_header)
		return -EINVAL;

	r = rtsp_header_serialize(m->iter_header);
	if (r < 0)
		return r;

	m->iter_header = NULL;

	return 0;
}

int rtsp_message_open_body(struct rtsp_message *m)
{
	if (!m || m->type == RTSP_MESSAGE_DATA)
		return -EINVAL;
	if (m->is_sealed)
		return -EBUSY;
	if (m->iter_header || m->iter_body)
		return -EINVAL;

	m->iter_body = true;

	return 0;
}

int rtsp_message_close_body(struct rtsp_message *m)
{
	if (!m || m->type == RTSP_MESSAGE_DATA)
		return -EINVAL;
	if (m->is_sealed)
		return -EBUSY;
	if (!m->iter_body)
		return -EINVAL;
	if (m->iter_header)
		return -EINVAL;

	m->iter_body = false;

	return 0;
}

int rtsp_message_append_basic(struct rtsp_message *m,
			      char type,
			      ...)
{
	va_list args;
	int r;

	va_start(args, type);
	r = rtsp_message_appendv_basic(m, type, &args);
	va_end(args);

	return r;
}

int rtsp_message_appendv_basic(struct rtsp_message *m,
			       char type,
			       va_list *args)
{
	char buf[128] = { };
	const char *orig;
	uint32_t u32;
	int32_t i32;

	if (!m || m->type == RTSP_MESSAGE_DATA)
		return -EINVAL;
	if (m->is_sealed)
		return -EBUSY;

	switch (type) {
	case RTSP_TYPE_RAW:
		orig = va_arg(*args, const char*);
		if (!orig)
			orig = "";

		if (m->iter_header)
			return rtsp_header_set_value(m->iter_header,
						     orig,
						     strlen(orig),
						     false);
		else
			return rtsp_message_append_line(m, orig);
	case RTSP_TYPE_HEADER_START:
		orig = va_arg(*args, const char*);

		return rtsp_message_open_header(m, orig);
	case RTSP_TYPE_HEADER_END:
		return rtsp_message_close_header(m);
	case RTSP_TYPE_BODY_START:
		return rtsp_message_open_body(m);
	case RTSP_TYPE_BODY_END:
		return rtsp_message_close_body(m);
	}

	if (!m->iter_header)
		return -EINVAL;

	switch (type) {
	case RTSP_TYPE_STRING:
		orig = va_arg(*args, const char*);
		if (!orig)
			orig = "";

		break;
	case RTSP_TYPE_INT32:
		i32 = va_arg(*args, int32_t);
		sprintf(buf, "%" PRId32, i32);
		orig = buf;
		break;
	case RTSP_TYPE_UINT32:
		u32 = va_arg(*args, uint32_t);
		sprintf(buf, "%" PRIu32, u32);
		orig = buf;
		break;
	default:
		return -EINVAL;
	}

	return rtsp_header_append_token(m->iter_header, orig);
}

int rtsp_message_append(struct rtsp_message *m,
			const char *types,
			...)
{
	va_list args;
	int r;

	va_start(args, types);
	r = rtsp_message_appendv(m, types, &args);
	va_end(args);

	return r;
}

int rtsp_message_appendv(struct rtsp_message *m,
			 const char *types,
			 va_list *args)
{
	int r;

	if (!m || m->type == RTSP_MESSAGE_DATA)
		return -EINVAL;
	if (m->is_sealed)
		return -EBUSY;
	if (!types)
		return 0;

	for ( ; *types; ++types) {
		r = rtsp_message_appendv_basic(m, *types, args);
		if (r < 0)
			return r;
	}

	return 0;
}

static int rtsp_message_serialize_common(struct rtsp_message *m)
{
	_shl_free_ char *head = NULL, *headers = NULL, *body = NULL;
	char buf[128];
	char *raw, *p, *cbody;
	size_t rawlen, i, l, body_size;
	int r;

	switch (m->type) {
	case RTSP_MESSAGE_UNKNOWN:
		head = shl_strcat(m->unknown_head, "\r\n");
		if (!head)
			return -ENOMEM;

		break;
	case RTSP_MESSAGE_REQUEST:
		r = asprintf(&head, "%s %s RTSP/%u.%u\r\n",
			     m->request_method,
			     m->request_uri,
			     m->major,
			     m->minor);
		if (r < 0)
			return -ENOMEM;

		break;
	case RTSP_MESSAGE_REPLY:
		r = asprintf(&head, "RTSP/%u.%u %u %s\r\n",
			     m->major,
			     m->minor,
			     m->reply_code,
			     m->reply_phrase);
		if (r < 0)
			return -ENOMEM;

		break;
	default:
		return -EINVAL;
	}

	rawlen = strlen(head);

	/* concat body */

	if (m->body) {
		body_size = m->body_size;
		cbody = (void*)m->body;
	} else {
		l = 0;
		for (i = 0; i < m->body_used; ++i)
			l += m->body_headers[i].line_len;

		body = malloc(l + 1);
		if (!body)
			return -ENOMEM;

		p = (char*)body;
		for (i = 0; i < m->body_used; ++i)
			p = stpcpy(p, m->body_headers[i].line);

		*p = 0;

		body_size = p - body;
		cbody = body;
	}

	rawlen += body_size;

	/* set content-length header */

	if (m->header_clen) {
		sprintf(buf, "%zu", body_size);
		r = rtsp_header_set_value(m->header_clen,
					  buf,
					  strlen(buf),
					  true);
		if (r < 0)
			return r;

		r = rtsp_header_serialize(m->header_clen);
		if (r < 0)
			return r;
	} else if (body_size) {
		rtsp_message_close_header(m);
		rtsp_message_close_body(m);
		r = rtsp_message_append(m, "<u>",
					"Content-Length",
					(uint32_t)body_size);
		if (r < 0)
			return r;
	}

	/* set content-type header */

	if (m->body_used && m->header_ctype) {
		r = rtsp_header_set_value(m->header_ctype,
					  "text/parameters",
					  15,
					  true);
		if (r < 0)
			return r;

		r = rtsp_header_serialize(m->header_ctype);
		if (r < 0)
			return r;
	} else if (m->body_used) {
		rtsp_message_close_header(m);
		rtsp_message_close_body(m);
		r = rtsp_message_append(m, "<s>",
					"Content-Type",
					"text/parameters");
		if (r < 0)
			return r;
	}

	/* set cseq header */

	sprintf(buf, "%llu", m->cookie & ~RTSP_FLAG_REMOTE_COOKIE);
	if (m->header_cseq) {
		r = rtsp_header_set_value(m->header_cseq,
					  buf,
					  strlen(buf),
					  true);
		if (r < 0)
			return r;

		r = rtsp_header_serialize(m->header_cseq);
		if (r < 0)
			return r;
	} else {
		rtsp_message_close_header(m);
		rtsp_message_close_body(m);
		r = rtsp_message_append(m, "<s>",
					"CSeq",
					buf);
		if (r < 0)
			return r;
	}

	/* concat headers */

	l = 0;
	for (i = 0; i < m->header_used; ++i)
		l += m->headers[i].line_len;

	headers = malloc(l + 1);
	if (!headers)
		return -ENOMEM;

	p = headers;
	for (i = 0; i < m->header_used; ++i)
		p = stpcpy(p, m->headers[i].line);

	*p = 0;
	rawlen += p - headers;

	/* final concat */

	rawlen += 2;
	raw = malloc(rawlen + 1);
	if (!raw)
		return -ENOMEM;

	p = raw;
	p = stpcpy(p, head);
	p = stpcpy(p, headers);
	*p++ = '\r';
	*p++ = '\n';
	memcpy(p, cbody, body_size);
	p += body_size;

	/* for debugging */
	*p = 0;

	m->raw = (void*)raw;
	m->raw_size = rawlen;

	m->body = (void*)cbody;
	m->body_size = body_size;
	body = NULL;

	return 0;
}

static int rtsp_message_serialize_data(struct rtsp_message *m)
{
	uint8_t *raw;
	size_t rawlen;

	rawlen = 1 + 1 + 2 + m->data_size;
	raw = malloc(rawlen + 1);
	if (!raw)
		return -ENOMEM;

	raw[0] = '$';
	raw[1] = m->data_channel;
	raw[2] = (m->data_size & 0xff00U) >> 8;
	raw[3] = (m->data_size & 0x00ffU);
	if (m->data_size)
		memcpy(&raw[4], m->data_payload, m->data_size);

	/* for debugging */
	raw[rawlen] = 0;

	m->raw = raw;
	m->raw_size = rawlen;

	return 0;
}

int rtsp_message_set_cookie(struct rtsp_message *m, uint64_t cookie)
{
	if (!m)
		return -EINVAL;
	if (m->is_sealed)
		return -EBUSY;

	m->cookie = cookie & ~RTSP_FLAG_REMOTE_COOKIE;
	if (m->type == RTSP_MESSAGE_REPLY)
		m->cookie |= RTSP_FLAG_REMOTE_COOKIE;

	return 0;
}

int rtsp_message_seal(struct rtsp_message *m)
{
	int r;

	if (!m)
		return -EINVAL;
	if (m->is_sealed)
		return 0;
	if (m->iter_body || m->iter_header)
		return -EINVAL;

	if (!m->cookie)
		m->cookie = ++m->bus->cookies ? : ++m->bus->cookies;
	if (m->type == RTSP_MESSAGE_REPLY)
		m->cookie |= RTSP_FLAG_REMOTE_COOKIE;

	switch (m->type) {
	case RTSP_MESSAGE_UNKNOWN:
	case RTSP_MESSAGE_REQUEST:
	case RTSP_MESSAGE_REPLY:
		r = rtsp_message_serialize_common(m);
		if (r < 0)
			return r;

		break;
	case RTSP_MESSAGE_DATA:
		r = rtsp_message_serialize_data(m);
		if (r < 0)
			return r;

		break;
	}

	m->is_sealed = true;

	return 0;
}

int rtsp_message_enter_header(struct rtsp_message *m, const char *name)
{
	size_t i;

	if (!m || shl_isempty(name) || m->type == RTSP_MESSAGE_DATA)
		return -EINVAL;
	if (!m->is_sealed)
		return -EBUSY;
	if (m->iter_header)
		return -EINVAL;

	if (m->iter_body) {
		for (i = 0; i < m->body_used; ++i) {
			if (!strcasecmp(m->body_headers[i].key, name)) {
				m->iter_header = &m->body_headers[i];
				m->iter_token = 0;
				return 0;
			}
		}
	} else {
		for (i = 0; i < m->header_used; ++i) {
			if (!strcasecmp(m->headers[i].key, name)) {
				m->iter_header = &m->headers[i];
				m->iter_token = 0;
				return 0;
			}
		}
	}

	return -ENOENT;
}

void rtsp_message_exit_header(struct rtsp_message *m)
{
	if (!m || !m->is_sealed || m->type == RTSP_MESSAGE_DATA)
		return;
	if (!m->iter_header)
		return;

	m->iter_header = NULL;
}

int rtsp_message_enter_body(struct rtsp_message *m)
{
	if (!m || m->type == RTSP_MESSAGE_DATA)
		return -EINVAL;
	if (!m->is_sealed)
		return -EBUSY;
	if (m->iter_header)
		return -EINVAL;
	if (m->iter_body)
		return -EINVAL;

	m->iter_body = true;

	return 0;
}

void rtsp_message_exit_body(struct rtsp_message *m)
{
	if (!m || !m->is_sealed || m->type == RTSP_MESSAGE_DATA)
		return;
	if (!m->iter_body)
		return;

	m->iter_body = false;
	m->iter_header = NULL;
}

int rtsp_message_read_basic(struct rtsp_message *m,
			    char type,
			    ...)
{
	va_list args;
	int r;

	va_start(args, type);
	r = rtsp_message_readv_basic(m, type, &args);
	va_end(args);

	return r;
}

int rtsp_message_readv_basic(struct rtsp_message *m,
			     char type,
			     va_list *args)
{
	const char *key;
	const char **out_str, *entry;
	int32_t i32, *out_i32;
	uint32_t u32, *out_u32;

	if (!m || m->type == RTSP_MESSAGE_DATA)
		return -EINVAL;
	if (!m->is_sealed)
		return -EBUSY;

	switch (type) {
	case RTSP_TYPE_RAW:
		if (!m->iter_header)
			return -EINVAL;

		out_str = va_arg(*args, const char**);
		if (out_str)
			*out_str = m->iter_header->value ? : "";

		return 0;
	case RTSP_TYPE_HEADER_START:
		key = va_arg(*args, const char*);

		return rtsp_message_enter_header(m, key);
	case RTSP_TYPE_HEADER_END:
		rtsp_message_exit_header(m);
		return 0;
	case RTSP_TYPE_BODY_START:
		return rtsp_message_enter_body(m);
	case RTSP_TYPE_BODY_END:
		rtsp_message_exit_body(m);
		return 0;
	}

	if (!m->iter_header)
		return -EINVAL;
	if (m->iter_token >= m->iter_header->token_used)
		return -ENOENT;

	entry = m->iter_header->tokens[m->iter_token];

	switch (type) {
	case RTSP_TYPE_STRING:
		out_str = va_arg(*args, const char**);
		if (out_str)
			*out_str = entry;

		break;
	case RTSP_TYPE_INT32:
		if (sscanf(entry, "%" SCNd32, &i32) != 1)
			return -EINVAL;

		out_i32 = va_arg(*args, int32_t*);
		if (out_i32)
			*out_i32 = i32;

		break;
	case RTSP_TYPE_UINT32:
		if (sscanf(entry, "%" SCNu32, &u32) != 1)
			return -EINVAL;

		out_u32 = va_arg(*args, uint32_t*);
		if (out_u32)
			*out_u32 = u32;

		break;
	case RTSP_TYPE_HEX32:
		if (sscanf(entry, "%" SCNx32, &u32) != 1)
			return -EINVAL;

		out_u32 = va_arg(*args, uint32_t*);
		if (out_u32)
			*out_u32 = u32;

		break;
	case RTSP_TYPE_SKIP:
		/* just increment token */
		break;
	default:
		return -EINVAL;
	}

	++m->iter_token;

	return 0;
}

int rtsp_message_read(struct rtsp_message *m,
		      const char *types,
		      ...)
{
	va_list args;
	int r;

	va_start(args, types);
	r = rtsp_message_readv(m, types, &args);
	va_end(args);

	return r;
}

int rtsp_message_readv(struct rtsp_message *m,
		       const char *types,
		       va_list *args)
{
	int r;

	if (!m || m->type == RTSP_MESSAGE_DATA)
		return -EINVAL;
	if (!m->is_sealed)
		return -EBUSY;
	if (!types)
		return 0;

	for ( ; *types; ++types) {
		r = rtsp_message_readv_basic(m, *types, args);
		if (r < 0) {
			if (m->iter_body)
				rtsp_message_exit_body(m);
			if (m->iter_header)
				rtsp_message_exit_header(m);
			return r;
		}
	}

	return 0;
}

int rtsp_message_skip_basic(struct rtsp_message *m, char type)
{
	return rtsp_message_read_basic(m, type, NULL, NULL, NULL, NULL);
}

int rtsp_message_skip(struct rtsp_message *m, const char *types)
{
	int r;

	if (!m || m->type == RTSP_MESSAGE_DATA)
		return -EINVAL;
	if (!m->is_sealed)
		return -EBUSY;
	if (!types)
		return 0;

	for ( ; *types; ++types) {
		r = rtsp_message_skip_basic(m, *types);
		if (r < 0)
			return r;
	}

	return 0;
}

int rtsp_message_rewind(struct rtsp_message *m, bool complete)
{
	if (!m || m->type == RTSP_MESSAGE_DATA)
		return -EINVAL;
	if (!m->is_sealed)
		return -EBUSY;

	m->iter_token = 0;
	if (complete) {
		m->iter_body = false;
		m->iter_header = NULL;
	}

	return 0;
}

void *rtsp_message_get_body(struct rtsp_message *m)
{
	if (!m || m->type == RTSP_MESSAGE_DATA)
		return NULL;
	if (!m->is_sealed)
		return NULL;

	return m->body;
}

size_t rtsp_message_get_body_size(struct rtsp_message *m)
{
	if (!m || m->type == RTSP_MESSAGE_DATA)
		return 0;
	if (!m->is_sealed)
		return 0;

	return m->body_size;
}

void *rtsp_message_get_raw(struct rtsp_message *m)
{
	if (!m)
		return NULL;
	if (!m->is_sealed)
		return NULL;

	return m->raw;
}

size_t rtsp_message_get_raw_size(struct rtsp_message *m)
{
	if (!m)
		return 0;
	if (!m->is_sealed)
		return 0;

	return m->raw_size;
}

/*
 * Message Assembly
 * These helpers take the raw RTSP input strings, parse them line by line to
 * assemble an rtsp_message object.
 */

static int rtsp_message_from_request(struct rtsp *bus,
				     struct rtsp_message **out,
				     const char *line)
{
	struct rtsp_message *m;
	unsigned int major, minor;
	size_t cmdlen, urllen;
	const char *next, *prev, *cmd, *url;
	int r;

	if (!bus || !line)
		return -EINVAL;

	/*
	 * Requests look like this:
	 *   <cmd> <url> RTSP/<major>.<minor>
	 */

	next = line;

	/* parse <cmd> */
	cmd = line;
	next = strchr(next, ' ');
	if (!next || next == cmd)
		goto error;
	cmdlen = next - cmd;

	/* skip " " */
	++next;

	/* parse <url> */
	url = next;
	next = strchr(next, ' ');
	if (!next || next == url)
		goto error;
	urllen = next - url;

	/* skip " " */
	++next;

	/* parse "RTSP/" */
	if (strncasecmp(next, "RTSP/", 5))
		goto error;
	next += 5;

	/* parse "%u" */
	prev = next;
	shl_atoi_u(prev, 10, (const char**)&next, &major);
	if (next == prev || *next != '.')
		goto error;

	/* skip "." */
	++next;

	/* parse "%u" */
	prev = next;
	shl_atoi_u(prev, 10, (const char**)&next, &minor);
	if (next == prev || *next)
		goto error;

	r = rtsp_message_new_request_n(bus, &m, cmd, cmdlen, url, urllen);
	if (r < 0)
		return r;

	m->major = major;
	m->minor = minor;

	*out = m;
	return 0;

error:
	/*
	 * Invalid request line.. Set type to UNKNOWN and let the caller deal
	 * with it. We will not try to send any error to avoid triggering
	 * another error if the remote side doesn't understand proper RTSP (or
	 * if our implementation is buggy).
	 */

	return rtsp_message_new_unknown(bus, out, line);
}

static int rtsp_message_from_reply(struct rtsp *bus,
				   struct rtsp_message **out,
				   const char *line)
{
	struct rtsp_message *m;
	unsigned int major, minor, code;
	const char *prev, *next, *str;
	int r;

	if (!bus || !out || !line)
		return -EINVAL;

	/*
	 * Responses look like this:
	 *   RTSP/<major>.<minor> <code> <string..>
	 *   RTSP/%u.%u %u %s
	 * We first parse the RTSP version and code. Everything appended to
	 * this is optional and represents the error string.
	 */

	/* parse "RTSP/" */
	if (strncasecmp(line, "RTSP/", 5))
		goto error;
	next = &line[5];

	/* parse "%u" */
	prev = next;
	shl_atoi_u(prev, 10, (const char**)&next, &major);
	if (next == prev || *next != '.')
		goto error;

	/* skip "." */
	++next;

	/* parse "%u" */
	prev = next;
	shl_atoi_u(prev, 10, (const char**)&next, &minor);
	if (next == prev || *next != ' ')
		goto error;

	/* skip " " */
	++next;

	/* parse: %u */
	prev = next;
	shl_atoi_u(prev, 10, (const char**)&next, &code);
	if (next == prev)
		goto error;
	if (*next && *next != ' ')
		goto error;

	/* skip " " */
	if (*next)
		++next;

	/* parse: %s */
	str = next;

	r = rtsp_message_new_raw_reply(bus, &m, code, str);
	if (r < 0)
		return r;

	m->major = major;
	m->minor = minor;

	*out = m;
	return 0;

error:
	/*
	 * Couldn't parse line. Avoid sending an error message as we could
	 * trigger another error and end up in an endless error loop. Instead,
	 * set message type to UNKNOWN and let the caller deal with it.
	 */

	return rtsp_message_new_unknown(bus, out, line);
}

static int rtsp_message_from_head(struct rtsp *bus,
				  struct rtsp_message **out,
				  const char *line)
{
	if (!bus || !out || !line)
		return -EINVAL;

	if (!strncasecmp(line, "RTSP/", 5))
		return rtsp_message_from_reply(bus, out, line);
	else
		return rtsp_message_from_request(bus, out, line);
}

static size_t rtsp__strncspn(const char *str,
			     size_t len,
			     const char *reject)
{
	size_t i, j;

	for (i = 0; i < len; ++i)
		for (j = 0; reject[j]; ++j)
			if (str[i] == reject[j])
				return i;

	return i;
}

static int rtsp_message_append_body(struct rtsp_message *m,
				    const void *body,
				    size_t len)
{
	_shl_free_ char *line = NULL;
	const char *d, *v;
	void *t;
	size_t dl, vl;
	int r;

	if (!m)
		return -EINVAL;
	if (len > 0 && !body)
		return -EINVAL;

	/* if body is empty, nothing to do */
	if (!len)
		return 0;

	/* Usually, we should verify the content-length
	 * parameter here. However, that's not needed if the
	 * input is of fixed length, so we skip that. It's
	 * the caller's responsibility to do that. */

	/* if content-type is not text/parameters, append the binary blob */
	if (!m->header_ctype ||
	    !m->header_ctype->value ||
	    strcmp(m->header_ctype->value, "text/parameters")) {
		t = malloc(len + 1);
		if (!t)
			return -ENOMEM;

		free(m->body);
		m->body = t;
		memcpy(m->body, body, len);
		m->body_size = len;
		return 0;
	}

	r = rtsp_message_open_body(m);
	if (r < 0)
		return r;

	d = body;
	while (len > 0) {
		dl = rtsp__strncspn(d, len, "\r\n");

		v = d;
		vl = dl;

		/* allow \r, \n, and \r\n as terminator */
		if (dl < len) {
			++dl;
			if (d[dl] == '\r') {
				if (dl < len && d[dl] == '\n')
					++dl;
			}
		}

		d += dl;
		len -= dl;

		/* ignore empty body lines */
		if (vl > 0) {
			free(line);
			line = malloc(vl + 1);
			if (!line)
				return -ENOMEM;

			memcpy(line, v, vl);
			line[vl] = 0;
			sanitize_line(line, vl);

			/* full header; append to message */
			r = rtsp_message_append_header_line(m, NULL, line);
			if (r < 0)
				return r;
		}
	}

	r = rtsp_message_close_body(m);
	if (r < 0)
		return r;

	return 0;
}

int rtsp_message_new_from_raw(struct rtsp *bus,
			      struct rtsp_message **out,
			      const void *data,
			      size_t len)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	_shl_free_ char *line = NULL;
	const char *d, *v;
	size_t dl, vl;
	int r;

	if (!bus)
		return -EINVAL;
	if (len > 0 && !data)
		return -EINVAL;

	d = data;
	while (len > 0) {
		dl = rtsp__strncspn(d, len, "\r\n");

		v = d;
		vl = dl;

		/* allow \r, \n, and \r\n as terminator */
		if (dl < len && d[dl++] == '\r')
			if (dl < len && d[dl] == '\n')
				++dl;

		d += dl;
		len -= dl;

		if (!vl) {
			/* empty line; start of body */

			if (!m) {
				r = rtsp_message_from_head(bus, &m, "");
				if (r < 0)
					return r;
			}

			r = rtsp_message_append_body(m, d, len);
			if (r < 0)
				return r;

			break;
		} else {
			free(line);
			line = malloc(vl + 1);
			if (!line)
				return -ENOMEM;

			memcpy(line, v, vl);
			line[vl] = 0;
			sanitize_line(line, vl);

			if (m) {
				/* full header; append to message */
				r = rtsp_message_append_header_line(m,
								    NULL,
								    line);
			} else {
				/* head line; create message */
				r = rtsp_message_from_head(bus, &m, line);
			}

			if (r < 0)
				return r;
		}
	}

	if (!m)
		return -EINVAL;

	r = rtsp_message_seal(m);
	if (r < 0)
		return r;

	*out = m;
	m = NULL;

	return 0;
}

/*
 * Parser State Machine
 * The parser state-machine is quite simple. We take an input buffer of
 * arbitrary length from the caller and feed it byte by byte into the state
 * machine.
 *
 * Parsing RTSP messages is rather troublesome due to the ASCII-nature. It's
 * easy to parse as is, but has lots of corner-cases which we want to be
 * compatible to maybe broken implementations. Thus, we need this
 * state-machine.
 *
 * All we do here is split the endless input stream into header-lines. The
 * header-lines are not handled by the state-machine itself but passed on. If a
 * message contains an entity payload, we parse the body. Otherwise, we submit
 * the message and continue parsing the next one.
 */

static int parser_append_header(struct rtsp *bus,
				char *line)
{
	struct rtsp_parser *dec = &bus->parser;
	struct rtsp_header *h;
	size_t clen;
	const char *next;
	int r;

	r = rtsp_message_append_header_line(dec->m,
					    &h,
					    line);
	if (r < 0)
		return r;

	if (h == dec->m->header_clen) {
		/* Screwed content-length line? We cannot recover from that as
		 * the attached entity is of unknown length. Abort.. */
		if (h->token_used < 1)
			return -EINVAL;

		r = shl_atoi_z(h->tokens[0], 10, &next, &clen);
		if (r < 0 || *next)
			return -EINVAL;

		/* overwrite previous lengths */
		dec->remaining_body = clen;
	} else if (h == dec->m->header_cseq) {
		if (h->token_used >= 1) {
			r = shl_atoi_z(h->tokens[0], 10, &next, &clen);
			if (r >= 0 &&
			    !*next &&
			    !(clen & RTSP_FLAG_REMOTE_COOKIE)) {
				/* overwrite previous values */
				dec->m->cookie = clen | RTSP_FLAG_REMOTE_COOKIE;
			}
		}
	}

	return r;
}

static int parser_finish_header_line(struct rtsp *bus)
{
	struct rtsp_parser *dec = &bus->parser;
	_shl_free_ char *line = NULL;
	int r;

	line = malloc(dec->buflen + 1);
	if (!line)
		return -ENOMEM;

	shl_ring_copy(&dec->buf, line, dec->buflen);
	line[dec->buflen] = 0;
	sanitize_line(line, dec->buflen);

	if (!dec->m)
		r = rtsp_message_from_head(bus, &dec->m, line);
	else
		r = parser_append_header(bus, line);

	return r;
}

static int parser_submit(struct rtsp *bus)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	struct rtsp_parser *dec = &bus->parser;
	int r;

	if (!dec->m)
		return 0;

	m = dec->m;
	dec->m = NULL;

	r = rtsp_message_seal(m);
	if (r < 0)
		return r;

	m->is_used = true;

	return rtsp_incoming_message(m);
}

static int parser_submit_data(struct rtsp *bus, uint8_t *p)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	struct rtsp_parser *dec = &bus->parser;
	int r;

	r = rtsp_message_new_data(bus,
				  &m,
				  dec->data_channel,
				  p,
				  dec->data_size);
	if (r < 0) {
		free(p);
		return r;
	}

	r = rtsp_message_seal(m);
	if (r < 0)
		return r;

	m->is_used = true;

	return rtsp_incoming_message(m);
}

static int parser_feed_char_new(struct rtsp *bus, char ch)
{
	struct rtsp_parser *dec = &bus->parser;

	switch (ch) {
	case '\r':
	case '\n':
	case '\t':
	case ' ':
		/* If no msg has been started, yet, we ignore LWS for
		 * compatibility reasons. Note that they're actually not
		 * allowed, but should be ignored by implementations. */
		++dec->buflen;
		break;
	case '$':
		/* Interleaved data. Followed by 1 byte channel-id and 2-byte
		 * data-length. */
		dec->state = STATE_DATA_HEAD;
		dec->data_channel = 0;
		dec->data_size = 0;

		/* clear any previous whitespace and leading '$' */
		shl_ring_pull(&dec->buf, dec->buflen + 1);
		dec->buflen = 0;
		break;
	default:
		/* Clear any pending data in the ring-buffer and then just
		 * push the char into the buffer. Any char except LWS is fine
		 * here. */
		dec->state = STATE_HEADER;
		dec->remaining_body = 0;

		shl_ring_pull(&dec->buf, dec->buflen);
		dec->buflen = 1;
		break;
	}

	return 0;
}

static int parser_feed_char_header(struct rtsp *bus, char ch)
{
	struct rtsp_parser *dec = &bus->parser;
	int r;

	switch (ch) {
	case '\r':
		if (dec->last_chr == '\r' || dec->last_chr == '\n') {
			/* \r\r means empty new-line. We actually allow \r\r\n,
			 * too. \n\r means empty new-line, too, but might also
			 * be finished off as \n\r\n so go to STATE_HEADER_NL
			 * to optionally complete the new-line.
			 * However, if the body is empty, we need to finish the
			 * msg early as there might be no \n coming.. */
			dec->state = STATE_HEADER_NL;

			/* First finish the last header line if any. Don't
			 * include the current \r as it is already part of the
			 * empty following line. */
			r = parser_finish_header_line(bus);
			if (r < 0)
				return r;

			/* discard buffer *and* whitespace */
			shl_ring_pull(&dec->buf, dec->buflen + 1);
			dec->buflen = 0;

			/* No remaining body. Finish message! */
			if (!dec->remaining_body) {
				r = parser_submit(bus);
				if (r < 0)
					return r;
			}
		} else {
			/* '\r' following any character just means newline
			 * (optionally followed by \n). We don't do anything as
			 * it might be a continuation line. */
			++dec->buflen;
		}
		break;
	case '\n':
		if (dec->last_chr == '\n') {
			/* We got \n\n, which means we need to finish the
			 * current header-line. If there's no remaining body,
			 * we immediately finish the message and go to
			 * STATE_NEW. Otherwise, we go to STATE_BODY
			 * straight. */

			/* don't include second \n in header-line */
			r = parser_finish_header_line(bus);
			if (r < 0)
				return r;

			/* discard buffer *and* whitespace */
			shl_ring_pull(&dec->buf, dec->buflen + 1);
			dec->buflen = 0;

			if (dec->remaining_body) {
				dec->state = STATE_BODY;
			} else {
				dec->state = STATE_NEW;
				r = parser_submit(bus);
				if (r < 0)
					return r;
			}
		} else if (dec->last_chr == '\r') {
			/* We got an \r\n. We cannot finish the header line as
			 * it might be a continuation line. Next character
			 * decides what to do. Don't do anything here.
			 * \r\n\r cannot happen here as it is handled by
			 * STATE_HEADER_NL. */
			++dec->buflen;
		} else {
			/* Same as above, we cannot finish the line as it
			 * might be a continuation line. Do nothing. */
			++dec->buflen;
		}
		break;
	case '\t':
	case ' ':
		/* Whitespace. Simply push into buffer and don't do anything.
		 * In case of a continuation line, nothing has to be done,
		 * either. */
		++dec->buflen;
		break;
	default:
		if (dec->last_chr == '\r' || dec->last_chr == '\n') {
			/* Last line is complete and this is no whitespace,
			 * thus it's not a continuation line.
			 * Finish the line. */

			/* don't include new char in line */
			r = parser_finish_header_line(bus);
			if (r < 0)
				return r;
			shl_ring_pull(&dec->buf, dec->buflen);
			dec->buflen = 0;
		}

		/* consume character and handle special chars */
		++dec->buflen;
		if (ch == '"') {
			/* go to STATE_HEADER_QUOTE */
			dec->state = STATE_HEADER_QUOTE;
			dec->quoted = false;
		}

		break;
	}

	return 0;
}

static int parser_feed_char_header_quote(struct rtsp *bus, char ch)
{
	struct rtsp_parser *dec = &bus->parser;

	if (dec->last_chr == '\\' && !dec->quoted) {
		/* This character is quoted, so copy it unparsed. To handle
		 * double-backslash, we set the "quoted" bit. */
		++dec->buflen;
		dec->quoted = true;
	} else {
		dec->quoted = false;

		/* consume character and handle special chars */
		++dec->buflen;
		if (ch == '"')
			dec->state = STATE_HEADER;
	}

	return 0;
}

static int parser_feed_char_body(struct rtsp *bus, char ch)
{
	struct rtsp_parser *dec = &bus->parser;
	char *line;
	int r;

	/* If remaining_body was already 0, the message had no body. Note that
	 * messages without body are finished early, so no need to call
	 * decoder_submit() here. Simply forward @ch to STATE_NEW.
	 * @rlen is usually 0. We don't care and forward it, too. */
	if (!dec->remaining_body) {
		dec->state = STATE_NEW;
		return parser_feed_char_new(bus, ch);
	}

	/* *any* character is allowed as body */
	++dec->buflen;

	if (!--dec->remaining_body) {
		/* full body received, copy it and go to STATE_NEW */

		if (dec->m) {
			line = malloc(dec->buflen + 1);
			if (!line)
				return -ENOMEM;

			shl_ring_copy(&dec->buf, line, dec->buflen);
			line[dec->buflen] = 0;

			r = rtsp_message_append_body(dec->m,
						     line,
						     dec->buflen);
			if (r >= 0)
				r = parser_submit(bus);

			free(line);
		} else {
			r = 0;
		}

		dec->state = STATE_NEW;
		shl_ring_pull(&dec->buf, dec->buflen);
		dec->buflen = 0;

		if (r < 0)
			return r;
	}

	return 0;
}

static int parser_feed_char_header_nl(struct rtsp *bus, char ch)
{
	struct rtsp_parser *dec = &bus->parser;

	/* STATE_HEADER_NL means we received an empty line ending with \r. The
	 * standard requires a following \n but advises implementations to
	 * accept \r on itself, too.
	 * What we do is to parse a \n as end-of-header and any character as
	 * end-of-header plus start-of-body. Note that we discard anything in
	 * the ring-buffer that has already been parsed (which normally can
	 * nothing, but lets be safe). */

	if (ch == '\n') {
		/* discard transition chars plus new \n */
		shl_ring_pull(&dec->buf, dec->buflen + 1);
		dec->buflen = 0;

		dec->state = STATE_BODY;
		if (!dec->remaining_body)
			dec->state = STATE_NEW;

		return 0;
	} else {
		/* discard any transition chars and push @ch into body */
		shl_ring_pull(&dec->buf, dec->buflen);
		dec->buflen = 0;

		dec->state = STATE_BODY;
		return parser_feed_char_body(bus, ch);
	}
}

static int parser_feed_char_data_head(struct rtsp *bus, char ch)
{
	struct rtsp_parser *dec = &bus->parser;
	uint8_t buf[3];

	/* Read 1 byte channel-id and 2 byte body length. */

	if (++dec->buflen >= 3) {
		shl_ring_copy(&dec->buf, buf, 3);
		shl_ring_pull(&dec->buf, dec->buflen);
		dec->buflen = 0;

		dec->data_channel = buf[0];
		dec->data_size = (((uint16_t)buf[1]) << 8) | (uint16_t)buf[2];
		dec->state = STATE_DATA_BODY;
	}

	return 0;
}

static int parser_feed_char_data_body(struct rtsp *bus, char ch)
{
	struct rtsp_parser *dec = &bus->parser;
	uint8_t *buf;
	int r;

	/* Read @dec->data_size bytes of raw data. */

	if (++dec->buflen >= dec->data_size) {
		buf = malloc(dec->data_size + 1);
		if (!buf)
			return -ENOMEM;

		/* Not really needed, but in case it's actually a text-payload
		 * make sure it's 0-terminated to work around client bugs. */
		buf[dec->data_size] = 0;

		shl_ring_copy(&dec->buf, buf, dec->data_size);

		r = parser_submit_data(bus, buf);
		free(buf);

		dec->state = STATE_NEW;
		shl_ring_pull(&dec->buf, dec->buflen);
		dec->buflen = 0;

		if (r < 0)
			return r;
	}

	return 0;
}

static int parser_feed_char(struct rtsp *bus, char ch)
{
	struct rtsp_parser *dec = &bus->parser;
	int r = 0;

	switch (dec->state) {
	case STATE_NEW:
		r = parser_feed_char_new(bus, ch);
		break;
	case STATE_HEADER:
		r = parser_feed_char_header(bus, ch);
		break;
	case STATE_HEADER_QUOTE:
		r = parser_feed_char_header_quote(bus, ch);
		break;
	case STATE_HEADER_NL:
		r = parser_feed_char_header_nl(bus, ch);
		break;
	case STATE_BODY:
		r = parser_feed_char_body(bus, ch);
		break;
	case STATE_DATA_HEAD:
		r = parser_feed_char_data_head(bus, ch);
		break;
	case STATE_DATA_BODY:
		r = parser_feed_char_data_body(bus, ch);
		break;
	}

	return r;
}

static int rtsp_parse_data(struct rtsp *bus,
			   const char *buf,
			   size_t len)
{
	struct rtsp_parser *dec = &bus->parser;
	size_t i;
	int r;

	if (!len)
		return -EAGAIN;

	/*
	 * We keep dec->buflen as cache for the current parsed-buffer size. We
	 * need to push the whole input-buffer into our parser-buffer and go
	 * through it one-by-one. The parser increments dec->buflen for each of
	 * these and once we're done, we verify our state is consistent.
	 */

	dec->buflen = shl_ring_get_size(&dec->buf);
	r = shl_ring_push(&dec->buf, buf, len);
	if (r < 0)
		return r;

	for (i = 0; i < len; ++i) {
		r = parser_feed_char(bus, buf[i]);
		if (r < 0)
			return r;

		dec->last_chr = buf[i];
	}

	/* check for internal parser inconsistencies; should not happen! */
	if (dec->buflen != shl_ring_get_size(&dec->buf))
		return -EFAULT;

	return 0;
}

/*
 * Bus Management
 * The bus layer is responsible of sending and receiving messages. It hooks
 * into any sd-event loop and properly serializes rtsp_message objects to the
 * given file-descriptor and vice-versa.
 *
 * On any I/O error, the bus layer tries to drain the input-queue and then pass
 * the HUP to the user. This way, we try to get all messages that the remote
 * side sent to us, before we give up and close the stream.
 *
 * Note that this layer is independent of the underlying transport. However, we
 * require the transport to be stream-based. Packet-based transports are not
 * supported and will fail silently.
 */

static int rtsp_call_message(struct rtsp_message *m,
			     struct rtsp_message *reply)
{
	int r;

	/* protect users by making sure arguments stay around */
	rtsp_message_ref(m);
	rtsp_message_ref(reply);

	if (m->cb_fn)
		r = m->cb_fn(m->bus, reply, m->fn_data);
	else
		r = 0;

	rtsp_message_unref(reply);
	rtsp_message_unref(m);

	return r;
}

static int rtsp_call_reply(struct rtsp *bus, struct rtsp_message *reply)
{
	struct rtsp_message *m;
	uint64_t *elem;
	int r;

	if (!shl_htable_lookup_u64(&bus->waiting,
				   reply->cookie & ~RTSP_FLAG_REMOTE_COOKIE,
				   &elem))
		return 0;

	m = rtsp_message_from_htable(elem);
	rtsp_message_ref(m);

	rtsp_drop_message(m);
	r = rtsp_call_message(m, reply);

	rtsp_message_unref(m);
	return r;
}

static int rtsp_call(struct rtsp *bus, struct rtsp_message *m)
{
	struct rtsp_match *match;
	struct shl_dlist *i, *t;
	int r;

	/* make sure bus and message stay around during any callbacks */
	rtsp_ref(bus);
	rtsp_message_ref(m);

	r = 0;

	bus->is_calling = true;
	shl_dlist_for_each(i, &bus->matches) {
		match = shl_dlist_entry(i, struct rtsp_match, list);
		r = match->cb_fn(bus, m, match->data);
		if (r != 0)
			break;
	}
	bus->is_calling = false;

	shl_dlist_for_each_safe(i, t, &bus->matches) {
		match = shl_dlist_entry(i, struct rtsp_match, list);
		if (match->is_removed)
			rtsp_free_match(match);
	}

	rtsp_message_unref(m);
	rtsp_unref(bus);

	return r;
}

static int rtsp_hup(struct rtsp *bus)
{
	if (bus->is_dead)
		return 0;

	rtsp_detach_event(bus);
	bus->is_dead = true;
	return rtsp_call(bus, NULL);
}

static int rtsp_timer_fn(sd_event_source *src, uint64_t usec, void *data)
{
	struct rtsp_message *m = data;
	int r;

	/* make sure message stays around during unlinking and callbacks */
	rtsp_message_ref(m);

	sd_event_source_set_enabled(m->timer_source, SD_EVENT_OFF);
	rtsp_drop_message(m);
	r = rtsp_call_message(m, NULL);

	rtsp_message_unref(m);

	return r;
}

static int rtsp_link_waiting(struct rtsp_message *m)
{
	int r;

	r = shl_htable_insert_u64(&m->bus->waiting, &m->cookie);
	if (r < 0)
		return r;

	/* no need to wait for timeout if no-body listens */
	if (m->bus->event && m->cb_fn) {
		r = sd_event_add_time(m->bus->event,
				      &m->timer_source,
				      CLOCK_MONOTONIC,
				      m->timeout,
				      0,
				      rtsp_timer_fn,
				      m);
		if (r < 0)
			goto error;

		r = sd_event_source_set_priority(m->timer_source,
						 m->bus->priority);
		if (r < 0)
			goto error;
	}

	m->is_waiting = true;
	++m->bus->waiting_cnt;
	rtsp_message_ref(m);

	return 0;

error:
	sd_event_source_unref(m->timer_source);
	m->timer_source = NULL;
	shl_htable_remove_u64(&m->bus->waiting, m->cookie, NULL);
	return r;
}

static void rtsp_unlink_waiting(struct rtsp_message *m)
{
	if (m->is_waiting) {
		sd_event_source_unref(m->timer_source);
		m->timer_source = NULL;
		shl_htable_remove_u64(&m->bus->waiting, m->cookie, NULL);
		m->is_waiting = false;
		--m->bus->waiting_cnt;
		rtsp_message_unref(m);
	}
}

static void rtsp_link_outgoing(struct rtsp_message *m)
{
	shl_dlist_link_tail(&m->bus->outgoing, &m->list);
	m->is_outgoing = true;
	++m->bus->outgoing_cnt;
	rtsp_message_ref(m);
}

static void rtsp_unlink_outgoing(struct rtsp_message *m)
{
	if (m->is_outgoing) {
		shl_dlist_unlink(&m->list);
		m->is_outgoing = false;
		m->is_sending = false;
		--m->bus->outgoing_cnt;
		rtsp_message_unref(m);
	}
}

static int rtsp_incoming_message(struct rtsp_message *m)
{
	int r;

	switch (m->type) {
	case RTSP_MESSAGE_UNKNOWN:
	case RTSP_MESSAGE_REQUEST:
	case RTSP_MESSAGE_DATA:
		/* simply forward all these to the match-handlers */
		r = rtsp_call(m->bus, m);
		if (r < 0)
			return r;

		break;
	case RTSP_MESSAGE_REPLY:
		/* find the waiting request and invoke the handler */
		r = rtsp_call_reply(m->bus, m);
		if (r < 0)
			return r;

		break;
	}

	return 0;
}

static int rtsp_read(struct rtsp *bus)
{
	char buf[4096];
	ssize_t res;

	res = recv(bus->fd,
		   buf,
		   sizeof(buf),
		   MSG_DONTWAIT);
	if (res < 0) {
		if (errno == EAGAIN || errno == EINTR)
			return -EAGAIN;

		return -errno;
	} else if (!res) {
		/* there're no 0-length packets on streams; this is EOF */
		return -EPIPE;
	} else if (res > sizeof(buf)) {
		res = sizeof(buf);
	}

	/* parses all messages and calls rtsp_incoming_message() for each */
	return rtsp_parse_data(bus, buf, res);
}

static int rtsp_write_message(struct rtsp_message *m)
{
	size_t remaining;
	ssize_t res;

	m->is_sending = true;
	remaining = m->raw_size - m->sent;
	res = send(m->bus->fd,
		   &m->raw[m->sent],
		   remaining,
		   MSG_NOSIGNAL | MSG_DONTWAIT);
	if (res < 0) {
		if (errno == EAGAIN || errno == EINTR)
			return -EAGAIN;

		return -errno;
	} else if (res > (ssize_t)remaining) {
		res = remaining;
	}

	m->sent += res;
	if (m->sent >= m->raw_size) {
		/* no need to wait for answer if no-body listens */
		if (!m->cb_fn)
			rtsp_unlink_waiting(m);

		/* might destroy the message */
		rtsp_unlink_outgoing(m);
	}

	return 0;
}

static int rtsp_write(struct rtsp *bus)
{
	struct rtsp_message *m;

	if (shl_dlist_empty(&bus->outgoing))
		return 0;

	m = shl_dlist_first_entry(&bus->outgoing, struct rtsp_message, list);
	return rtsp_write_message(m);
}

static int rtsp_io_fn(sd_event_source *src, int fd, uint32_t mask, void *data)
{
	struct rtsp *bus = data;
	int r, write_r;

	/* make sure bus stays around during any possible callbacks */
	rtsp_ref(bus);

	/*
	 * Whenever we encounter I/O errors, we have to make sure to drain the
	 * input queue first, before we handle any HUP. A server might send us
	 * a message and immediately close the queue. We must not handle the
	 * HUP first or we loose data.
	 * Therefore, if we read a message successfully, we always return
	 * success and wait for the next event-loop iteration. Furthermore,
	 * whenever there is a write-error, we must try reading from the input
	 * queue even if EPOLLIN is not set. The input might have arrived in
	 * between epoll_wait() and send(). Therefore, write-errors are only
	 * ever handled if the input-queue is empty. In all other cases they
	 * are ignored until either reading fails or the input queue is empty.
	 */

	if (mask & EPOLLOUT) {
		write_r = rtsp_write(bus);
		if (write_r == -EAGAIN)
			write_r = 0;
	} else {
		write_r = 0;
	}

	if (mask & EPOLLIN || write_r < 0) {
		r = rtsp_read(bus);
		if (r < 0 && r != -EAGAIN)
			goto error;
		else if (r >= 0)
			goto out;
	}

	if (!(mask & (EPOLLHUP | EPOLLERR)) && write_r >= 0) {
		r = 0;
		goto out;
	}

	/* I/O error, forward HUP to match-handlers */

error:
	r = rtsp_hup(bus);
out:
	rtsp_unref(bus);
	return r;
}

static int rtsp_io_prepare_fn(sd_event_source *src, void *data)
{
	struct rtsp *bus = data;
	uint32_t mask;
	int r;

	mask = EPOLLHUP | EPOLLERR | EPOLLIN;
	if (!shl_dlist_empty(&bus->outgoing))
		mask |= EPOLLOUT;

	r = sd_event_source_set_io_events(bus->fd_source, mask);
	if (r < 0)
		return r;

	return 0;
}

int rtsp_open(struct rtsp **out, int fd)
{
	_rtsp_unref_ struct rtsp *bus = NULL;

	if (!out || fd < 0)
		return -EINVAL;

	bus = calloc(1, sizeof(*bus));
	if (!bus)
		return -ENOMEM;

	bus->ref = 1;
	bus->fd = fd;
	shl_dlist_init(&bus->matches);
	shl_dlist_init(&bus->outgoing);
	shl_htable_init_u64(&bus->waiting);

	*out = bus;
	bus = NULL;
	return 0;
}

void rtsp_ref(struct rtsp *bus)
{
	if (!bus || !bus->ref)
		return;

	++bus->ref;
}

void rtsp_unref(struct rtsp *bus)
{
	struct rtsp_message *m;
	struct rtsp_match *match;
	struct shl_dlist *i;
	size_t refs;
	bool q;

	if (!bus || !bus->ref)
		return;

	/* If the reference count is equal to the number of messages we have
	 * in our internal queues plus the reference we're about to drop, then
	 * all remaining references are self-references. Therefore, going over
	 * all messages and in case they also have no external references, drop
	 * all queues so bus->ref drops to 1 and we can free it. */
	refs = bus->outgoing_cnt + bus->waiting_cnt + 1;
	if (bus->parser.m)
		++refs;

	if (bus->ref <= refs) {
		q = true;
		shl_dlist_for_each(i, &bus->outgoing) {
			m = shl_dlist_entry(i, struct rtsp_message, list);
			if (m->ref > 1) {
				q = false;
				break;
			}
		}

		if (q) {
			RTSP_FOREACH_WAITING(m, bus) {
				if (m->ref > 1) {
					q = false;
					break;
				}
			}
		}

		if (q) {
			while (!shl_dlist_empty(&bus->outgoing)) {
				m = shl_dlist_first_entry(&bus->outgoing,
							  struct rtsp_message,
							  list);
				rtsp_unlink_outgoing(m);
			}

			while ((m = RTSP_FIRST_WAITING(bus)))
				rtsp_unlink_waiting(m);

			rtsp_message_unref(bus->parser.m);
			bus->parser.m = NULL;
		}
	}

	if (!bus->ref || --bus->ref)
		return;

	while (!shl_dlist_empty(&bus->matches)) {
		match = shl_dlist_first_entry(&bus->matches,
					      struct rtsp_match,
					      list);
		rtsp_free_match(match);
	}

	rtsp_detach_event(bus);
	shl_ring_clear(&bus->parser.buf);
	shl_htable_clear_u64(&bus->waiting, NULL, NULL);
	close(bus->fd);
	free(bus);
}

bool rtsp_is_dead(struct rtsp *bus)
{
	return !bus || bus->is_dead;
}

int rtsp_attach_event(struct rtsp *bus, sd_event *event, int priority)
{
	struct rtsp_message *m;
	int r;

	if (!bus)
		return -EINVAL;
	if (bus->is_dead)
		return -EINVAL;
	if (bus->event)
		return -EALREADY;

	if (event) {
		bus->event = event;
		sd_event_ref(event);
	} else {
		r = sd_event_default(&bus->event);
		if (r < 0)
			return r;
	}

	bus->priority = priority;

	r = sd_event_add_io(bus->event,
			    &bus->fd_source,
			    bus->fd,
			    EPOLLHUP | EPOLLERR | EPOLLIN,
			    rtsp_io_fn,
			    bus);
	if (r < 0)
		goto error;

	r = sd_event_source_set_priority(bus->fd_source, priority);
	if (r < 0)
		goto error;

	r = sd_event_source_set_prepare(bus->fd_source, rtsp_io_prepare_fn);
	if (r < 0)
		goto error;

	RTSP_FOREACH_WAITING(m, bus) {
		/* no need to wait for timeout if no-body listens */
		if (!m->cb_fn)
			continue;

		r = sd_event_add_time(bus->event,
				      &m->timer_source,
				      CLOCK_MONOTONIC,
				      m->timeout,
				      0,
				      rtsp_timer_fn,
				      m);
		if (r < 0)
			goto error;

		r = sd_event_source_set_priority(m->timer_source, priority);
		if (r < 0)
			goto error;
	}

	return 0;

error:
	rtsp_detach_event(bus);
	return r;
}

void rtsp_detach_event(struct rtsp *bus)
{
	struct rtsp_message *m;

	if (!bus || !bus->event)
		return;

	RTSP_FOREACH_WAITING(m, bus) {
		sd_event_source_unref(m->timer_source);
		m->timer_source = NULL;
	}

	sd_event_source_unref(bus->fd_source);
	bus->fd_source = NULL;
	sd_event_unref(bus->event);
	bus->event = NULL;
}

/**
 * rtsp_add_match() - Add match-callback
 * @bus: rtsp bus to register callback on
 * @cb_fn: function to be used as callback
 * @data: user-context data that is passed through unchanged
 *
 * The given callback is called for each incoming request that was not matched
 * automatically to scheduled transactions. Note that you can register many
 * callbacks and they're called in the order they're registered. If a callback
 * handled a message, no further callbacks are called.
 *
 * You can register multiple callbacks with the _same_ @cb_fn and @data just
 * fine. However, once you unregister them, they're always unregistered in the
 * reverse order you registered them in.
 *
 * All match-callbacks are automatically removed when @bus is destroyed.
 *
 * Returns:
 * True on success, negative error code on failure.
 */
int rtsp_add_match(struct rtsp *bus, rtsp_callback_fn cb_fn, void *data)
{
	struct rtsp_match *match;

	if (!bus || !cb_fn)
		return -EINVAL;

	match = calloc(1, sizeof(*match));
	if (!match)
		return -ENOMEM;

	match->cb_fn = cb_fn;
	match->data = data;

	shl_dlist_link_tail(&bus->matches, &match->list);

	return 0;
}

/**
 * rtsp_remove_match() - Remove match-callback
 * @bus: rtsp bus to unregister callback from
 * @cb_fn: callback function to unregister
 * @data: user-context data used during registration
 *
 * This reverts a previous call to rtsp_add_match(). If a given callback is not
 * found, nothing is done. Note that if you register a callback with the same
 * cb_fn+data combination multiple times, this only removes the last of them.
 *
 * All match-callbacks are automatically removed when @bus is destroyed.
 */
void rtsp_remove_match(struct rtsp *bus, rtsp_callback_fn cb_fn, void *data)
{
	struct rtsp_match *match;
	struct shl_dlist *i;

	if (!bus || !cb_fn)
		return;

	shl_dlist_for_each_reverse(i, &bus->matches) {
		match = shl_dlist_entry(i, struct rtsp_match, list);
		if (match->cb_fn == cb_fn && match->data == data) {
			if (bus->is_calling)
				match->is_removed = true;
			else
				rtsp_free_match(match);

			break;
		}
	}
}

static void rtsp_free_match(struct rtsp_match *match)
{
	if (!match)
		return;

	shl_dlist_unlink(&match->list);
	free(match);
}

int rtsp_send(struct rtsp *bus, struct rtsp_message *m)
{
	return rtsp_call_async(bus, m, NULL, NULL, 0, NULL);
}

int rtsp_call_async(struct rtsp *bus,
		    struct rtsp_message *m,
		    rtsp_callback_fn cb_fn,
		    void *data,
		    uint64_t timeout,
		    uint64_t *cookie)
{
	int r;

	if (!bus || bus->is_dead || !m || !m->cookie)
		return -EINVAL;
	if (m->bus != bus || m->is_outgoing || m->is_waiting || m->is_used)
		return -EINVAL;

	r = rtsp_message_seal(m);
	if (r < 0)
		return r;
	if (!m->raw)
		return -EINVAL;

	m->is_used = true;
	m->cb_fn = cb_fn;
	m->fn_data = data;
	m->timeout = timeout ? : RTSP_DEFAULT_TIMEOUT;
	m->timeout += shl_now(CLOCK_MONOTONIC);

	/* verify cookie and generate one if none is set */
	if (m->cookie & RTSP_FLAG_REMOTE_COOKIE) {
		if (m->type != RTSP_MESSAGE_UNKNOWN &&
		    m->type != RTSP_MESSAGE_REPLY)
			return -EINVAL;
	} else {
		if (m->type == RTSP_MESSAGE_REPLY)
			return -EINVAL;
	}

	/* needs m->cookie set correctly */
	r = rtsp_link_waiting(m);
	if (r < 0)
		return r;

	rtsp_link_outgoing(m);

	if (cookie)
		*cookie = m->cookie;

	return 0;
}

static void rtsp_drop_message(struct rtsp_message *m)
{
	if (!m)
		return;

	/* never interrupt messages while being partly sent */
	if (!m->is_sending)
		rtsp_unlink_outgoing(m);

	/* remove from waiting list so neither timeouts nor completions fire */
	rtsp_unlink_waiting(m);
}

void rtsp_call_async_cancel(struct rtsp *bus, uint64_t cookie)
{
	struct rtsp_message *m;
	uint64_t *elem;

	if (!bus || !cookie)
		return;

	if (!shl_htable_lookup_u64(&bus->waiting, cookie, &elem))
		return;

	m = rtsp_message_from_htable(elem);
	rtsp_drop_message(m);
}
