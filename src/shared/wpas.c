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

#define LOG_SUBSYSTEM "wpa"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <sys/un.h>
#include <systemd/sd-event.h>
#include <unistd.h>
#include "shl_dlist.h"
#include "shl_util.h"
#include "wpas.h"
#include "shl_log.h"

#define CTRL_PATH_TEMPLATE "/tmp/.miracle-wpas-%d-%lu"

#ifndef UNIX_PATH_MAX
#  define UNIX_PATH_MAX (sizeof(((struct sockaddr_un*)0)->sun_path))
#endif

/* default timeout for messages is 500ms */
#define WPAS_DEFAULT_TIMEOUT (500 * 1000ULL)

/* max message size */
#define WPAS_MAX_LEN 16384

struct wpas_message {
	unsigned long ref;
	struct wpas *w;

	struct shl_dlist list;
	wpas_callback_fn cb_fn;
	void *data;
	uint64_t cookie;
	uint64_t timeout;
	struct sockaddr_un peer;

	char *raw;
	size_t rawlen;
	unsigned int type;
	char *name;
	unsigned int level;
	char *ifname;

	size_t argc;
	size_t argv_size;
	char **argv;
	size_t iter;

	bool queued : 1;
	bool sent : 1;
	bool sealed : 1;
	bool removed : 1;
	bool has_peer : 1;
};

struct wpas_match {
	struct shl_dlist list;
	wpas_callback_fn cb_fn;
	void *data;

	bool removed : 1;
};

struct wpas {
	unsigned long ref;
	int fd;
	char fd_name[UNIX_PATH_MAX];
	char *ctrl_path;
	struct sockaddr_un peer;

	int priority;
	sd_event *event;
	sd_event_source *fd_source;
	sd_event_source *timer_source;

	struct shl_dlist match_list;

	uint64_t cookies;
	size_t msg_list_cnt;
	struct shl_dlist msg_list;
	char recvbuf[WPAS_MAX_LEN];

	bool server : 1;
	bool dead : 1;
	bool calling : 1;
};

/*
 * WPAS Message
 */

static int wpas_message_new(struct wpas *w,
			    const char *name,
			    struct wpas_message **out)
{
	_wpas_message_unref_ struct wpas_message *m = NULL;

	if (!w || !out)
		return -EINVAL;

	m = calloc(1, sizeof(*m));
	if (!m)
		return -ENOMEM;

	m->ref = 1;
	m->w = w;
	wpas_ref(w);
	m->type = WPAS_MESSAGE_UNKNOWN;

	if (!SHL_GREEDY_REALLOC0_T(m->argv, m->argv_size, 2))
		return -ENOMEM;

	if (name) {
		m->iter = 1;
		m->argc = 1;
		m->name = strdup(name);
		m->argv[0] = m->name;
		if (!m->name)
			return -ENOMEM;
	}

	*out = m;
	m = NULL;
	return 0;
}

int wpas_message_new_event(struct wpas *w,
			   const char *name,
			   unsigned int level,
			   struct wpas_message **out)
{
	struct wpas_message *m;
	int r;

	if (!w || !name || !*name || !out)
		return -EINVAL;

	r = wpas_message_new(w, name, &m);
	if (r < 0)
		return r;

	m->type = WPAS_MESSAGE_EVENT;
	m->level = level;

	*out = m;
	return 0;
}

int wpas_message_new_request(struct wpas *w,
			     const char *name,
			     struct wpas_message **out)
{
	struct wpas_message *m;
	int r;

	if (!w || !name || !*name || !out)
		return -EINVAL;

	r = wpas_message_new(w, name, &m);
	if (r < 0)
		return r;

	m->type = WPAS_MESSAGE_REQUEST;

	*out = m;
	return 0;
}

int wpas_message_new_reply(struct wpas *w,
			   struct wpas_message **out)
{
	struct wpas_message *m;
	int r;

	if (!w || !out)
		return -EINVAL;

	r = wpas_message_new(w, NULL, &m);
	if (r < 0)
		return r;

	m->type = WPAS_MESSAGE_REPLY;

	*out = m;
	return 0;
}

int wpas_message_new_reply_for(struct wpas *w,
			       struct wpas_message *request,
			       struct wpas_message **out)
{
	struct wpas_message *m;
	int r;

	if (!w || !request || !out || !request->has_peer)
		return -EINVAL;

	r = wpas_message_new_reply(w, &m);
	if (r < 0)
		return r;

	memcpy(&m->peer, &request->peer, sizeof(m->peer));
	m->has_peer = true;

	*out = m;
	return 0;
}

void wpas_message_ref(struct wpas_message *m)
{
	if (!m || !m->ref)
		return;

	++m->ref;
}

void wpas_message_unref(struct wpas_message *m)
{
	if (!m || !m->ref || --m->ref)
		return;

	shl_strv_free(m->argv);
	wpas_unref(m->w);
	free(m->ifname);
	free(m->raw);
	free(m);
}

bool wpas_message_is_event(struct wpas_message *msg, const char *name)
{
	return msg &&
	       msg->type == WPAS_MESSAGE_EVENT &&
	       (!name || !strcasecmp(name, msg->name));
}

bool wpas_message_is_request(struct wpas_message *msg, const char *name)
{
	return msg &&
	       msg->type == WPAS_MESSAGE_REQUEST &&
	       (!name || !strcasecmp(name, msg->name));
}

bool wpas_message_is_reply(struct wpas_message *msg)
{
	return msg &&
	       msg->type == WPAS_MESSAGE_REPLY;
}

bool wpas_message_is_ok(struct wpas_message *msg)
{
	return msg && wpas_message_is_reply(msg) && !strcmp(msg->raw, "OK\n");
}

bool wpas_message_is_fail(struct wpas_message *msg)
{
	return msg && wpas_message_is_reply(msg) && !strcmp(msg->raw, "FAIL\n");
}

uint64_t wpas_message_get_cookie(struct wpas_message *msg)
{
	return msg ? msg->cookie : 0;
}

struct wpas *wpas_message_get_bus(struct wpas_message *msg)
{
	return msg ? msg->w : NULL;
}

unsigned int wpas_message_get_type(struct wpas_message *msg)
{
	return msg ? msg->type : WPAS_MESSAGE_UNKNOWN;
}

unsigned int wpas_message_get_level(struct wpas_message *msg)
{
	if (msg && msg->type == WPAS_MESSAGE_EVENT)
		return msg->level;
	else
		return WPAS_LEVEL_UNKNOWN;
}

const char *wpas_message_get_name(struct wpas_message *msg)
{
	return msg ? msg->name : NULL;
}

const char *wpas_message_get_raw(struct wpas_message *msg)
{
	return msg ? msg->raw : NULL;
}

const char *wpas_message_get_ifname(struct wpas_message *msg)
{
	return msg ? msg->ifname : NULL;
}

bool wpas_message_is_sealed(struct wpas_message *msg)
{
	return !msg || msg->sealed;
}

const char *wpas_message_get_peer(struct wpas_message *msg)
{
	if (!msg || !msg->has_peer)
		return NULL;

	return msg->peer.sun_path;
}

char *wpas_message_get_escaped_peer(struct wpas_message *msg)
{
	if (!msg)
		return NULL;
	if (!msg->has_peer)
		return strdup("<none>");

	if (msg->peer.sun_path[0])
		return strdup(msg->peer.sun_path);
	else
		return shl_strcat("@abstract:", msg->peer.sun_path + 1);
}

void wpas_message_set_peer(struct wpas_message *msg, const char *peer)
{
	if (!msg || msg->sealed)
		return;

	if (peer) {
		if (*peer) {
			strncpy(msg->peer.sun_path,
				peer,
				sizeof(msg->peer.sun_path) - 1);
		} else {
			msg->peer.sun_path[0] = 0;
			strncpy(msg->peer.sun_path + 1,
				peer + 1,
				sizeof(msg->peer.sun_path) - 2);
		}

		msg->has_peer = true;
		msg->peer.sun_path[sizeof(msg->peer.sun_path) - 1] = 0;
	} else {
		msg->has_peer = false;
		memset(msg->peer.sun_path, 0, sizeof(msg->peer.sun_path));
	}
}

int wpas_message_append_basic(struct wpas_message *m, char type, ...)
{
	va_list args;
	int r;

	va_start(args, type);
	r = wpas_message_appendv_basic(m, type, &args);
	va_end(args);

	return r;
}

int wpas_message_appendv_basic(struct wpas_message *m,
			       char type,
			       va_list *args)
{
	_shl_free_ char *str = NULL;
	char buf[128] = { };
	const char *orig;
	const char *s, *t;
	uint32_t u32;
	int32_t i32;

	if (!m)
		return -EINVAL;
	if (m->sealed)
		return -EBUSY;

	if (!SHL_GREEDY_REALLOC0_T(m->argv, m->argv_size, m->argc + 2))
		return -ENOMEM;

	switch (type) {
	case WPAS_TYPE_STRING:
		orig = va_arg(*args, const char*);
		if (!orig)
			return -EINVAL;

		break;
	case WPAS_TYPE_INT32:
		i32 = va_arg(*args, int32_t);
		sprintf(buf, "%" PRId32, i32);
		orig = buf;
		break;
	case WPAS_TYPE_UINT32:
		u32 = va_arg(*args, uint32_t);
		sprintf(buf, "%" PRIu32, u32);
		orig = buf;
		break;
	case WPAS_TYPE_DICT:
		s = va_arg(*args, const char*);
		if (!s)
			return -EINVAL;

		t = va_arg(*args, const char*);
		if (!t)
			return -EINVAL;

		str = shl_strjoin(s, "=", t, NULL);
		if (!str)
			return -ENOMEM;

		break;
	default:
		return -EINVAL;
	}

	if (!str)
		str = strdup(orig);
	if (!str)
		return -ENOMEM;

	m->argv[m->argc++] = str;
	m->argv[m->argc] = NULL;
	str = NULL;
	return 0;
}

int wpas_message_append(struct wpas_message *m, const char *types, ...)
{
	va_list args;
	int r;

	va_start(args, types);
	r = wpas_message_appendv(m, types, &args);
	va_end(args);

	return r;
}

int wpas_message_appendv(struct wpas_message *m,
			 const char *types,
			 va_list *args)
{
	int r;

	if (!m)
		return -EINVAL;
	if (m->sealed)
		return -EBUSY;

	/* This is not atomic, so if we fail in the middle, we will not
	 * revert our previous work. We could do that, but why bother.. If
	 * this fails, we have bigger problems and no-one should continue
	 * trying to add more data, anyway. */

	for ( ; *types; ++types) {
		r = wpas_message_appendv_basic(m, *types, args);
		if (r < 0)
			return r;
	}

	return 0;
}

int wpas_message_seal(struct wpas_message *m)
{
	_shl_free_ char *str = NULL;
	char *t, buf[128];
	int r;

	if (!m)
		return -EINVAL;
	if (m->sealed)
		return 0;

	r = shl_qstr_join(m->argv, &str);
	if (r < 0)
		return r;

	if (m->type == WPAS_MESSAGE_EVENT) {
		sprintf(buf, "<%u>", m->level);
		t = shl_strcat(buf, str);
		if (!t)
			return -ENOMEM;

		free(str);
		str = t;
	}

	m->rawlen = r;
	m->raw = str;
	str = NULL;
	m->sealed = true;

	return 0;
}

int wpas_message_read_basic(struct wpas_message *m, char type, void *out)
{
	const char *entry;

	if (!m || m->iter >= m->argc || !out)
		return -EINVAL;

	entry = m->argv[m->iter];
	switch (type) {
	case WPAS_TYPE_STRING:
		*(const char**)out = entry;
		break;
	case WPAS_TYPE_INT32:
		if (sscanf(entry, "%" SCNd32, (int32_t*)out) != 1)
			return -EINVAL;
		break;
	case WPAS_TYPE_UINT32:
		if (sscanf(entry, "%" SCNu32, (uint32_t*)out) != 1)
			return -EINVAL;
		break;
	case WPAS_TYPE_DICT:
		entry = strchr(entry, '=');
		if (!entry)
			return -EINVAL;

		*(const char**)out = entry + 1;
		break;
	default:
		return -EINVAL;
	}

	++m->iter;
	return 0;
}

int wpas_message_read(struct wpas_message *m, const char *types, ...)
{
	va_list args;
	void *arg;
	int r;

	if (!m)
		return -EINVAL;

	r = 0;
	va_start(args, types);

	for ( ; *types; ++types) {
		arg = va_arg(args, void*);
		r = wpas_message_read_basic(m, *types, arg);
		if (r < 0)
			break;
	}

	va_end(args);

	return r;
}

int wpas_message_skip_basic(struct wpas_message *m, char type)
{
	const char *entry;
	uint32_t u32;
	int32_t i32;

	if (!m || m->iter >= m->argc)
		return -EINVAL;

	entry = m->argv[m->iter];
	switch (type) {
	case WPAS_TYPE_STRING:
		break;
	case WPAS_TYPE_INT32:
		if (sscanf(entry, "%" SCNd32, &i32) != 1)
			return -EINVAL;
		break;
	case WPAS_TYPE_UINT32:
		if (sscanf(entry, "%" SCNu32, &u32) != 1)
			return -EINVAL;
		break;
	case WPAS_TYPE_DICT:
		entry = strchr(entry, '=');
		if (!entry)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	++m->iter;
	return 0;
}

int wpas_message_skip(struct wpas_message *m, const char *types)
{
	int r;

	if (!m)
		return -EINVAL;

	for ( ; *types; ++types) {
		r = wpas_message_skip_basic(m, *types);
		if (r < 0)
			return r;
	}

	return 0;
}

void wpas_message_rewind(struct wpas_message *m)
{
	if (!m)
		return;

	m->iter = m->name ? 1 : 0;
}

int wpas_message_argv_read(struct wpas_message *m,
			   unsigned int pos,
			   char type,
			   void *out)
{
	const char *entry;

	if (!m || !out)
		return -EINVAL;

	if (m->name && !++pos)
		return -EINVAL;

	if (pos >= m->argc)
		return -EINVAL;

	entry = m->argv[pos];

	switch (type) {
	case WPAS_TYPE_STRING:
		*(const char**)out = entry;
		break;
	case WPAS_TYPE_INT32:
		if (sscanf(entry, "%" SCNd32, (int32_t*)out) != 1)
			return -EINVAL;
		break;
	case WPAS_TYPE_UINT32:
		if (sscanf(entry, "%" SCNu32, (uint32_t*)out) != 1)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int wpas_message_dict_read(struct wpas_message *m,
			   const char *name,
			   char type,
			   void *out)
{
	const char *entry;
	size_t i, l;

	if (!m || !name || !out)
		return -EINVAL;

	for (i = m->name ? 1 : 0; i < m->argc; ++i) {
		entry = strchr(m->argv[i], '=');
		if (!entry)
			continue;

		l = entry - m->argv[i];
		if (strncmp(m->argv[i], name, l) || name[l])
			continue;

		++entry;

		switch (type) {
		case WPAS_TYPE_STRING:
			*(const char**)out = entry;
			break;
		case WPAS_TYPE_INT32:
			if (sscanf(entry, "%" SCNd32, (int32_t*)out) != 1)
				return -EINVAL;
			break;
		case WPAS_TYPE_UINT32:
			if (sscanf(entry, "%" SCNu32, (uint32_t*)out) != 1)
				return -EINVAL;
			break;
		default:
			return -EINVAL;
		}

		return 0;
	}

	return -ENOENT;
}

static int wpas__parse_args(struct wpas_message *m,
			    char **argv,
			    int argc)
{
	int i, r;

	for (i = 0; i < argc; ++i) {
		r = wpas_message_append_basic(m, 's', argv[i]);
		if (r < 0)
			return r;
	}

	return 0;
}

static int wpas__parse_message(struct wpas *w,
			       char *raw,
			       size_t len,
			       struct sockaddr_un *src,
			       struct wpas_message **out)
{
	_wpas_message_unref_ struct wpas_message *m = NULL;
	_shl_strv_free_ char **args = NULL;
	const char *ifname = NULL;
	unsigned int level;
	char *pos;
	char *orig_raw = raw;
	int r, num;
	bool is_event = false;

	log_trace("raw message: %s", raw);

	if ((pos = shl_startswith(raw, "IFNAME="))) {
		ifname = pos;
		pos = strchrnul(pos, ' ');
		if (*pos)
			pos++;

		len -= pos - raw;
		raw = pos;
	}

	is_event = len > 0 && raw[0] == '<';

	/* replies are split on new-lines, everything else like a qstr */
	if (!w->server && !is_event) {
		num = shl_strsplit_n(raw, len, "\n", &args);
		if (num < 0)
			return num;
	} else {
		num = shl_qstr_tokenize_n(raw, len, &args);
		if (num < 0)
			return num;
	}

	if (!w->server && is_event) {
		pos = strchr(args[0], '>');
		if (pos && pos[1]) {
			*pos = 0;
			level = atoi(args[0] + 1);
			*pos = '>';

			r = wpas_message_new_event(w, &pos[1], level, &m);
			if (r < 0)
				return r;

			r = wpas__parse_args(m, args + 1, num - 1);
			if (r < 0)
				return r;
		}
	} else if (!w->server) {
		r = wpas_message_new_reply(w, &m);
		if (r < 0)
			return r;

		r = wpas__parse_args(m, args, num);
		if (r < 0)
			return r;
	} else if (w->server && num && args[0][0]) {
		r = wpas_message_new_request(w, args[0], &m);
		if (r < 0)
			return r;

		r = wpas__parse_args(m, args + 1, num - 1);
		if (r < 0)
			return r;
	}

	if (!m) {
		r = wpas_message_new(w, "", &m);
		if (r < 0)
			return r;
	}

	m->sealed = true;
	m->rawlen = len;
	m->raw = strdup(orig_raw);
	if (!m->raw)
		return -ENOMEM;

	if (ifname) {
		m->ifname = strndup(ifname, strchrnul(ifname, ' ') - ifname);
		if (!m->ifname)
			return -ENOMEM;
	}

	/* copy message source */
	memcpy(&m->peer, src, sizeof(*src));
	m->has_peer = true;

	*out = m;
	m = NULL;
	return 0;
}

/*
 * WPAS Bus
 */

static void wpas__match_free(struct wpas_match *match)
{
	if (!match)
		return;

	shl_dlist_unlink(&match->list);
	free(match);
}

static void wpas__call(struct wpas *w, struct wpas_message *m)
{
	struct shl_dlist *i, *t;
	struct wpas_match *match;
	int r;

	w->calling = true;
	shl_dlist_for_each_safe(i, t, &w->match_list) {
		match = shl_dlist_entry(i, struct wpas_match, list);
		r = match->cb_fn(w, m, match->data);
		if (r != 0)
			break;
	}
	w->calling = false;

	shl_dlist_for_each_safe(i, t, &w->match_list) {
		match = shl_dlist_entry(i, struct wpas_match, list);
		if (match->removed)
			wpas__match_free(match);
	}
}

static int wpas__message_call(struct wpas_message *m, struct wpas_message *a)
{
	if (m->cb_fn)
		return m->cb_fn(m->w, a, m->data);
	else
		return 0;
}

static void wpas__link_message(struct wpas *w, struct wpas_message *m)
{
	shl_dlist_link_tail(&w->msg_list, &m->list);
	m->queued = true;
	++w->msg_list_cnt;
	wpas_message_ref(m);
}

static void wpas__unlink_message(struct wpas *w, struct wpas_message *m)
{
	shl_dlist_unlink(&m->list);
	m->queued = false;
	--w->msg_list_cnt;
	wpas_message_unref(m);
}

static struct wpas_message *wpas__get_current(struct wpas *w)
{
	if (shl_dlist_empty(&w->msg_list))
		return NULL;

	return shl_dlist_first_entry(&w->msg_list, struct wpas_message, list);
}

static int wpas__bind_client_socket(int fd, char *name)
{
	static unsigned long real_counter;
	unsigned long counter;
	struct sockaddr_un src;
	int r;
	bool tried = false;

	/* TODO: make wpa_supplicant allow unbound clients */

	/* Yes, this counter is racy, but wpa_supplicant doesn't provide support
	 * for unbound clients (it crashes..). We could add a current-time based
	 * random part, but that might leave stupid pipes around in /tmp. So
	 * lets just use this internal counter and blame
	 * wpa_supplicant.. Yey! */
	counter = ++real_counter;

	memset(&src, 0, sizeof(src));
	src.sun_family = AF_UNIX;

	snprintf(name,
		 UNIX_PATH_MAX - 1,
		 CTRL_PATH_TEMPLATE,
		 (int)getpid(),
		 counter);
	name[UNIX_PATH_MAX - 1] = 0;

try_again:
	strcpy(src.sun_path, name);

	r = bind(fd, (struct sockaddr*)&src, sizeof(src));
	if (r < 0) {
		if (errno == EADDRINUSE && !tried) {
			tried = true;
			unlink(name);
			goto try_again;
		}

		return -errno;
	}

	return 0;
}

static int wpas__connect_client_socket(int fd,
				       const char *ctrl_path,
				       struct sockaddr_un *out)
{
	int r;
	struct sockaddr_un dst;
	size_t len;

	memset(&dst, 0, sizeof(dst));
	dst.sun_family = AF_UNIX;

	len = strlen(ctrl_path);
	if (!strncmp(ctrl_path, "@abstract:", 10)) {
		if (len > sizeof(dst.sun_path) - 12)
			return -EINVAL;

		dst.sun_path[0] = 0;
		dst.sun_path[sizeof(dst.sun_path) - 1] = 0;
		strncpy(&dst.sun_path[1], &ctrl_path[10],
			sizeof(dst.sun_path) - 2);
	} else {
		if (len > sizeof(dst.sun_path) - 1)
			return -EINVAL;

		dst.sun_path[sizeof(dst.sun_path) - 1] = 0;
		strncpy(dst.sun_path, ctrl_path, sizeof(dst.sun_path) - 1);
	}

	r = connect(fd, (struct sockaddr*)&dst, sizeof(dst));
	if (r < 0)
		return -errno;

	memcpy(out, &dst, sizeof(dst));
	return 0;
}

static int wpas__new_client_socket(const char *ctrl_path,
				   char *name,
				   struct sockaddr_un *peer)
{
	int fd, r;

	name[0] = 0;

	fd = socket(PF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0)
		return -errno;

	r = wpas__bind_client_socket(fd, name);
	if (r < 0)
		goto error;

	r = wpas__connect_client_socket(fd, ctrl_path, peer);
	if (r < 0)
		goto error;

	return fd;

error:
	if (name[0]) {
		unlink(name);
		name[0] = 0;
	}

	close(fd);
	return r;
}

static int wpas__bind_server_socket(int fd, const char *ctrl_path, char *name)
{
	struct sockaddr_un src;
	bool abstract = false;
	size_t len;
	int r;

	memset(&src, 0, sizeof(src));
	src.sun_family = AF_UNIX;

	len = strlen(ctrl_path);
	if (!strncmp(ctrl_path, "@abstract:", 10)) {
		if (len > sizeof(src.sun_path) - 12)
			return -EINVAL;

		abstract = true;
		src.sun_path[0] = 0;
		src.sun_path[sizeof(src.sun_path) - 1] = 0;
		strncpy(&src.sun_path[1], &ctrl_path[10],
			sizeof(src.sun_path) - 2);
	} else {
		if (len > sizeof(src.sun_path) - 1)
			return -EINVAL;

		src.sun_path[sizeof(src.sun_path) - 1] = 0;
		strncpy(src.sun_path, ctrl_path, sizeof(src.sun_path) - 1);
	}

	r = bind(fd, (struct sockaddr*)&src, sizeof(src));
	if (r < 0) {
		if (abstract)
			return -EADDRINUSE;

		r = connect(fd, (struct sockaddr*)&src, sizeof(src));
		if (r < 0) {
			unlink(ctrl_path);
			r = bind(fd, (struct sockaddr*)&src, sizeof(src));
			if (r < 0)
				return -errno;
		} else {
			return -EADDRINUSE;
		}
	}

	strncpy(name, src.sun_path, UNIX_PATH_MAX - 1);
	name[UNIX_PATH_MAX - 1] = 0;

	return 0;
}

static int wpas__new_server_socket(const char *ctrl_path, char *name)
{
	int fd, r;

	name[0] = 0;

	fd = socket(PF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0)
		return -errno;

	r = wpas__bind_server_socket(fd, ctrl_path, name);
	if (r < 0)
		goto error;

	return fd;

error:
	close(fd);
	return r;
}

static void wpas__close(struct wpas *w)
{
	if (w->fd >= 0) {
		close(w->fd);
		w->fd = -1;
	}

	if (w->fd_name[0]) {
		unlink(w->fd_name);
		w->fd_name[0] = 0;
	}
}

static void wpas__hup(struct wpas *w)
{
	if (w->dead)
		return;

	wpas_detach_event(w);
	wpas__close(w);
	w->dead = true;
	wpas__call(w, NULL);
}

static int wpas_new(const char *ctrl_path, bool server, struct wpas **out)
{
	_wpas_unref_ struct wpas *w = NULL;

	if (!ctrl_path || !out)
		return -EINVAL;

	w = calloc(1, sizeof(*w));
	if (!w)
		return -ENOMEM;

	w->ref = 1;
	w->fd = -1;
	w->server = server;
	shl_dlist_init(&w->match_list);
	shl_dlist_init(&w->msg_list);

	w->ctrl_path = strdup(ctrl_path);
	if (!ctrl_path)
		return -ENOMEM;

	if (server)
		w->fd = wpas__new_server_socket(ctrl_path, w->fd_name);
	else
		w->fd = wpas__new_client_socket(ctrl_path,
						w->fd_name,
						&w->peer);
	if (w->fd < 0)
		return w->fd;

	*out = w;
	w = NULL;
	return 0;
}

int wpas_open(const char *ctrl_path, struct wpas **out)
{
	return wpas_new(ctrl_path, false, out);
}

int wpas_create(const char *ctrl_path, struct wpas **out)
{
	return wpas_new(ctrl_path, true, out);
}

void wpas_ref(struct wpas *w)
{
	if (!w || !w->ref)
		return;

	++w->ref;
}

void wpas_unref(struct wpas *w)
{
	struct wpas_match *match;
	struct shl_dlist *i;
	struct wpas_message *m;
	bool q;

	if (!w || !w->ref)
		return;

	/* If our ref-count is exactly the size of our internal queue, no-one
	 * else but the queued messages hold a reference to us. Therefore, we
	 * check for each message whether anyone holds a reference to them
	 * (besides us), and if not, we drop the whole queue. This will drop
	 * the remaining reference to us so the decrement below this block
	 * will drop to zero. */
	if (w->ref <= w->msg_list_cnt + 1) {
		q = true;
		shl_dlist_for_each(i, &w->msg_list) {
			m = shl_dlist_entry(i, struct wpas_message, list);
			if (m->ref > 1) {
				q = false;
				break;
			}
		}

		if (q) {
			/* drop our queue */
			while (!shl_dlist_empty(&w->msg_list)) {
				m = shl_dlist_first_entry(&w->msg_list,
							  struct wpas_message,
							  list);
				wpas__unlink_message(w, m);
			}
		}
	}

	if (!w->ref || --w->ref)
		return;

	wpas_detach_event(w);

	while (!shl_dlist_empty(&w->match_list)) {
		match = shl_dlist_first_entry(&w->match_list,
					      struct wpas_match,
					      list);
		wpas__match_free(match);
	}

	wpas__close(w);
	free(w->ctrl_path);
	free(w);
}

int wpas_call_async(struct wpas *w,
		    struct wpas_message *m,
		    wpas_callback_fn cb_fn,
		    void *data,
		    uint64_t timeout,
		    uint64_t *cookie)
{
	int r;

	if (!w || !m || m->w != w)
		return -EINVAL;
	if (m->queued || m->sent)
		return -EALREADY;
	if (w->server || m->type != WPAS_MESSAGE_REQUEST || m->has_peer)
		return -EINVAL;

	memcpy(&m->peer, &w->peer, sizeof(m->peer));
	m->has_peer = true;

	r = wpas_message_seal(m);
	if (r < 0)
		return r;

	m->cb_fn = cb_fn;
	m->data = data;
	m->timeout = timeout ? : WPAS_DEFAULT_TIMEOUT;
	m->timeout += shl_now(CLOCK_MONOTONIC);
	m->cookie = ++w->cookies ? : ++w->cookies;

	wpas__link_message(w, m);

	if (cookie)
		*cookie = m->cookie;

	return 0;
}

void wpas_call_async_cancel(struct wpas *w, uint64_t cookie)
{
	struct wpas_message *m;
	struct shl_dlist *i;

	if (!w || !cookie)
		return;

	shl_dlist_for_each(i, &w->msg_list) {
		m = shl_dlist_entry(i, struct wpas_message, list);
		if (m->cookie != cookie)
			continue;

		if (m->sent)
			m->removed = true;
		else
			wpas__unlink_message(w, m);

		return;
	}
}

int wpas_send(struct wpas *w,
	      struct wpas_message *m,
	      uint64_t timeout)
{
	int r;

	if (!w || !m || m->w != w)
		return -EINVAL;
	if (m->queued || m->sent)
		return -EALREADY;
	if (!m->has_peer && w->server)
		return -EINVAL;

	if (!m->has_peer) {
		memcpy(&m->peer, &w->peer, sizeof(m->peer));
		m->has_peer = true;
	}

	r = wpas_message_seal(m);
	if (r < 0)
		return r;

	m->cb_fn = NULL;
	m->data = NULL;
	m->timeout = timeout ? : WPAS_DEFAULT_TIMEOUT;
	m->timeout += shl_now(CLOCK_MONOTONIC);
	m->cookie = 0;

	wpas__link_message(w, m);

	return 0;
}

static int wpas__send(struct wpas *w, struct wpas_message *m)
{
	ssize_t l;

	if (!m->rawlen)
		return 0;

	l = sendto(w->fd,
		   m->raw,
		   m->rawlen,
		   MSG_NOSIGNAL,
		   w->server ? &m->peer : NULL,
		   w->server ? sizeof(m->peer) : 0);
	if (l < 0) {
		if (errno == EAGAIN || errno == EINTR)
			return -EAGAIN;

		return -errno;
	} else if (!l) {
		return -EAGAIN;
	}

	return 0;
}

static int wpas__write(struct wpas *w)
{
	struct wpas_message *m;
	int r;

	m = wpas__get_current(w);
	if (!m || m->sent)
		return 0;

	r = wpas__send(w, m);
	if (r < 0)
		return r;

	m->sent = true;
	if (!m->cookie)
		wpas__unlink_message(w, m);

	return 0;
}

static int wpas__read_message(struct wpas *w, struct wpas_message **out)
{
	struct sockaddr_un src = { };
	socklen_t src_len = sizeof(src);
	ssize_t l;

	l = recvfrom(w->fd,
		     w->recvbuf,
		     sizeof(w->recvbuf) - 1,
		     MSG_DONTWAIT,
		     (struct sockaddr*)&src,
		     &src_len);
	if (l < 0) {
		if (errno == EAGAIN || errno == EINTR)
			return -EAGAIN;

		return -errno;
	} else if (!l) {
		return -EAGAIN;
	} else if (src_len > sizeof(src)) {
		return -EFAULT;
	} else if (l > sizeof(w->recvbuf) - 1) {
		l = sizeof(w->recvbuf) - 1;
	}

	w->recvbuf[l] = 0;
	return wpas__parse_message(w, w->recvbuf, l, &src, out);
}

static int wpas__read(struct wpas *w)
{
	_wpas_message_unref_ struct wpas_message *a = NULL;
	_wpas_message_unref_ struct wpas_message *m = NULL;
	int r;

	r = wpas__read_message(w, &a);
	if (r < 0)
		return r;

	switch (a->type) {
	case WPAS_MESSAGE_UNKNOWN:
	case WPAS_MESSAGE_REQUEST:
	case WPAS_MESSAGE_EVENT:
		wpas__call(w, a);
		break;
	case WPAS_MESSAGE_REPLY:
		m = wpas__get_current(w);
		wpas_message_ref(m);
		if (!m || !m->sent)
			break;

		wpas__unlink_message(w, m);
		if (m->removed)
			break;

		wpas__message_call(m, a);
		break;
	}

	return 0;
}

static int wpas_io_fn(sd_event_source *source, int fd, uint32_t mask, void *d)
{
	struct wpas *w = d;
	int r, write_r;

	/* make sure WPAS stays around during any user-callbacks */
	wpas_ref(w);

	/*
	 * If writing fails, there might still be messages in the in-queue even
	 * though EPOLLIN might not have been set, yet. So in any case, if
	 * writing failed, try reading some left-overs from the queue. Only if
	 * the queue is empty, we handle any possible write-errors.
	 */

	if (mask & EPOLLOUT) {
		write_r = wpas__write(w);
		if (write_r == -EAGAIN)
			write_r = 0;
	} else {
		write_r = 0;
	}

	if (mask & EPOLLIN || write_r < 0) {
		/* Read one packet from the FD and return. Don't block the
		 * event loop by reading in a loop. We're called again if
		 * there's still data so make sure higher priority tasks will
		 * get a change to interrupt us. */
		r = wpas__read(w);
		if (r < 0 && r != -EAGAIN)
			goto error;

		/* If EPOLLIN was set, there definitely was data in the queue
		 * and there *might* be more. So always return here instead of
		 * falling back to EPOLLHUP below. The next iteration will read
		 * remaining data and once EPOLLIN is no longer set, we will
		 * handle EPOLLHUP. */
		if (r >= 0)
			goto out;
	}

	/* If we got here with an error, there definitely is no data left in
	 * the input-queue. We can finally handle the HUP and be done. */
	if (mask & (EPOLLHUP | EPOLLERR) || write_r < 0)
		goto error;

	goto out;

error:
	wpas__hup(w);
out:
	wpas_unref(w);
	return 0;
}

static int wpas_io_prepare_fn(sd_event_source *source, void *d)
{
	struct wpas *w = d;
	struct wpas_message *m;
	uint32_t mask;
	int r;

	m = wpas__get_current(w);

	mask = EPOLLHUP | EPOLLERR | EPOLLIN;
	if (m && !m->sent)
		mask |= EPOLLOUT;

	r = sd_event_source_set_io_events(w->fd_source, mask);
	if (r < 0)
		return r;

	if (m) {
		r = sd_event_source_set_time(w->timer_source, m->timeout);
		if (r < 0)
			return r;

		r = sd_event_source_set_enabled(w->timer_source, SD_EVENT_ON);
		if (r < 0)
			return r;
	} else {
		r = sd_event_source_set_enabled(w->timer_source, SD_EVENT_OFF);
		if (r < 0)
			return r;
	}

	return 0;
}

static int wpas_timer_fn(sd_event_source *source, uint64_t timeout, void *d)
{
	struct wpas *w = d;
	struct wpas_message *m;

	/* make sure WPAS stays around during any user-callbacks */
	wpas_ref(w);

	/* always disable timer, ->prepare() takes care of rescheduling */
	sd_event_source_set_enabled(w->timer_source, SD_EVENT_OFF);

	/* No message? What was this timer for? */
	m = wpas__get_current(w);
	if (!m)
		goto out;

	/* A message timed out. We cannot drop it because there might be a
	 * delayed response coming in and WPAS doesn't provide serials/cookies.
	 * We also cannot reopen the connection as this might cause missing
	 * async-events. So lets just notify the HUP callback and close it. */

	wpas__hup(w);

out:
	wpas_unref(w);
	return 0;
}

int wpas_attach_event(struct wpas *w, sd_event *event, int priority)
{
	uint32_t mask;
	int r;

	if (!w)
		return -EINVAL;
	if (w->dead)
		return -ENOTCONN;
	if (w->event)
		return -EALREADY;

	w->priority = priority;

	if (event) {
		w->event = sd_event_ref(event);
	} else {
		r = sd_event_default(&w->event);
		if (r < 0)
			return r;
	}

	mask = EPOLLHUP | EPOLLERR | EPOLLIN;
	r = sd_event_add_io(w->event,
			    &w->fd_source,
			    w->fd,
			    mask,
			    wpas_io_fn,
			    w);
	if (r < 0)
		goto error;

	r = sd_event_source_set_priority(w->fd_source, priority);
	if (r < 0)
		goto error;

	r = sd_event_source_set_prepare(w->fd_source, wpas_io_prepare_fn);
	if (r < 0)
		goto error;

	r = sd_event_add_time(w->event,
			      &w->timer_source,
			      CLOCK_MONOTONIC,
			      0,
			      0,
			      wpas_timer_fn,
			      w);
	if (r < 0)
		goto error;

	r = sd_event_source_set_enabled(w->timer_source, SD_EVENT_OFF);
	if (r < 0)
		goto error;

	r = sd_event_source_set_priority(w->timer_source, priority);
	if (r < 0)
		goto error;

	return 0;

error:
	wpas_detach_event(w);
	return r;
}

void wpas_detach_event(struct wpas *w)
{
	if (!w || !w->event)
		return;

	w->event = sd_event_unref(w->event);
	w->fd_source = sd_event_source_unref(w->fd_source);
	w->timer_source = sd_event_source_unref(w->timer_source);
}

int wpas_add_match(struct wpas *w, wpas_callback_fn cb_fn, void *data)
{
	struct wpas_match *match;

	if (!w || !cb_fn)
		return -EINVAL;

	match = calloc(1, sizeof(*match));
	if (!match)
		return -ENOMEM;

	match->cb_fn = cb_fn;
	match->data = data;

	/* Add matches at the end so matches are called in the same order they
	 * are registered. Note that you can register the same match multiple
	 * times just fine. However, removal will be done in reversed order. */
	shl_dlist_link_tail(&w->match_list, &match->list);

	return 0;
}

void wpas_remove_match(struct wpas *w, wpas_callback_fn cb_fn, void *data)
{
	struct shl_dlist *i;
	struct wpas_match *match;

	if (!w || !cb_fn)
		return;

	/* Traverse the list in reverse order so we remove the last added
	 * match first, in case a match is added multiple times. */
	shl_dlist_for_each_reverse(i, &w->match_list) {
		match = shl_dlist_entry(i, struct wpas_match, list);
		if (match->cb_fn == cb_fn && match->data == data) {
			if (w->calling)
				match->removed = true;
			else
				wpas__match_free(match);

			return;
		}
	}
}

bool wpas_is_dead(struct wpas *w)
{
	return !w || w->dead;
}

bool wpas_is_server(struct wpas *w)
{
	return w && w->server;
}
