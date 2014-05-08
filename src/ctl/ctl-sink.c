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

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <systemd/sd-event.h>
#include <time.h>
#include <unistd.h>
#include "ctl.h"
#include "rtsp.h"
#include "shl_macro.h"
#include "shl_util.h"

struct ctl_sink {
	sd_event *event;

	char *target;
	struct sockaddr_storage addr;
	size_t addr_size;
	int fd;
	sd_event_source *fd_source;

	struct rtsp *rtsp;

	bool connected : 1;
	bool hup : 1;
};

/*
 * RTSP Session
 */

static int sink_req_fn(struct rtsp *bus, struct rtsp_message *m, void *data)
{
	cli_debug("INCOMING: %s\n", rtsp_message_get_raw(m));
	return 0;
}

static void sink_handle_options(struct ctl_sink *s,
				struct rtsp_message *m)
{
	_rtsp_message_unref_ struct rtsp_message *rep = NULL;
	int r;

	r = rtsp_message_new_reply_for(m, &rep, 200, NULL);
	if (r < 0)
		return cli_vERR(r);

	r = rtsp_message_append(rep, "<s>",
				"Public",
				"org.wfa.wfd1.0, GET_PARAMETER, SET_PARAMETER");
	if (r < 0)
		return cli_vERR(r);

	rtsp_message_seal(rep);
	cli_debug("OUTGOING: %s\n", rtsp_message_get_raw(rep));

	r = rtsp_send(s->rtsp, rep);
	if (r < 0)
		return cli_vERR(r);

	rtsp_message_unref(rep);
	rep = NULL;

	r = rtsp_message_new_request(s->rtsp,
				     &rep,
				     "OPTIONS",
				     "*");
	if (r < 0)
		return cli_vERR(r);

	r = rtsp_message_append(rep, "<s>",
				"Require",
				"org.wfa.wfd1.0");
	if (r < 0)
		return cli_vERR(r);

	rtsp_message_seal(rep);
	cli_debug("OUTGOING: %s\n", rtsp_message_get_raw(rep));

	r = rtsp_call_async(s->rtsp, rep, sink_req_fn, NULL, 0, NULL);
	if (r < 0)
		return cli_vERR(r);
}

static void sink_handle_get_parameter(struct ctl_sink *s,
				      struct rtsp_message *m)
{
	_rtsp_message_unref_ struct rtsp_message *rep = NULL;
	int r;

	r = rtsp_message_new_reply_for(m, &rep, 200, NULL);
	if (r < 0)
		return cli_vERR(r);

	r = rtsp_message_append(rep, "{&&&&}",
				"wfd_content_protection: none",
				"wfd_video_formats: 00 00 01 01 0000007f 003fffff 00000000 00 0000 0000 00 none none",
				"wfd_audio_codecs: LPCM 00000003 00",
				"wfd_client_rtp_ports: RTP/AVP/UDP;unicast 1991 0 mode=play");
	if (r < 0)
		return cli_vERR(r);

	rtsp_message_seal(rep);
	cli_debug("OUTGOING: %s\n", rtsp_message_get_raw(rep));

	r = rtsp_send(s->rtsp, rep);
	if (r < 0)
		return cli_vERR(r);
}

static void sink_handle_set_parameter(struct ctl_sink *s,
				      struct rtsp_message *m)
{
	_rtsp_message_unref_ struct rtsp_message *rep = NULL;
	const char *trigger;
	int r;

	r = rtsp_message_new_reply_for(m, &rep, 200, NULL);
	if (r < 0)
		return cli_vERR(r);

	rtsp_message_seal(rep);
	cli_debug("OUTGOING: %s\n", rtsp_message_get_raw(rep));

	r = rtsp_send(s->rtsp, rep);
	if (r < 0)
		return cli_vERR(r);

	rtsp_message_unref(rep);
	rep = NULL;

	r = rtsp_message_read(m, "{<s>}", "wfd_trigger_method", &trigger);
	if (r < 0)
		return;

	if (!strcmp(trigger, "SETUP")) {
		r = rtsp_message_new_request(s->rtsp,
					     &rep,
					     "SETUP",
					     "rtsp://localhost/wfd1.0/streamid=0");
		if (r < 0)
			return cli_vERR(r);

		r = rtsp_message_append(rep, "<s>",
					"Transport",
					"RTP/AVP/UDP;unicast;client_port=1991");
		if (r < 0)
			return cli_vERR(r);

		rtsp_message_seal(rep);
		cli_debug("OUTGOING: %s\n", rtsp_message_get_raw(rep));

		r = rtsp_call_async(s->rtsp, rep, sink_req_fn, NULL, 0, NULL);
		if (r < 0)
			return cli_vERR(r);

		rtsp_message_unref(rep);
		rep = NULL;

		r = rtsp_message_new_request(s->rtsp,
					     &rep,
					     "PLAY",
					     "rtsp://localhost/wfd1.0/streamid=0");
		if (r < 0)
			return cli_vERR(r);

		rtsp_message_seal(rep);
		cli_debug("OUTGOING: %s\n", rtsp_message_get_raw(rep));

		r = rtsp_call_async(s->rtsp, rep, sink_req_fn, NULL, 0, NULL);
		if (r < 0)
			return cli_vERR(r);
	}
}

static void sink_handle(struct ctl_sink *s,
			struct rtsp_message *m)
{
	const char *method;

	cli_debug("INCOMING: %s\n", rtsp_message_get_raw(m));

	method = rtsp_message_get_method(m);
	if (!method)
		return;

	if (!strcmp(method, "OPTIONS")) {
		sink_handle_options(s, m);
	} else if (!strcmp(method, "GET_PARAMETER")) {
		sink_handle_get_parameter(s, m);
	} else if (!strcmp(method, "SET_PARAMETER")) {
		sink_handle_set_parameter(s, m);
	}
}

static int sink_rtsp_fn(struct rtsp *bus,
			struct rtsp_message *m,
			void *data)
{
	struct ctl_sink *s = data;

	if (!m)
		s->hup = true;
	else
		sink_handle(s, m);

	if (s->hup) {
		ctl_sink_close(s);
		ctl_fn_sink_disconnected(s);
	}

	return 0;
}

/*
 * Sink I/O
 */

static void sink_connected(struct ctl_sink *s)
{
	int r, val;
	socklen_t len;

	if (s->connected || s->hup)
		return;

	sd_event_source_set_enabled(s->fd_source, SD_EVENT_OFF);

	len = sizeof(val);
	r = getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &val, &len);
	if (r < 0) {
		s->hup = true;
		cli_vERRNO();
		return;
	} else if (val) {
		s->hup = true;
		errno = val;
		cli_error("cannot connect to remote host (%d): %m",
			  errno);
		return;
	}

	cli_debug("connection established");

	r = rtsp_open(&s->rtsp, s->fd);
	if (r < 0)
		goto error;

	r = rtsp_attach_event(s->rtsp, s->event, 0);
	if (r < 0)
		goto error;

	r = rtsp_add_match(s->rtsp, sink_rtsp_fn, s);
	if (r < 0)
		goto error;

	s->connected = true;
	ctl_fn_sink_connected(s);
	return;

error:
	s->hup = true;
	cli_vERR(r);
}

static void sink_io(struct ctl_sink *s, uint32_t mask)
{
	if (mask & EPOLLOUT)
		sink_connected(s);

	if (mask & (EPOLLHUP | EPOLLERR)) {
		cli_notice("HUP/ERR on socket");
		s->hup = true;
	}

	if (s->hup) {
		ctl_sink_close(s);
		ctl_fn_sink_disconnected(s);
	}
}

static int sink_io_fn(sd_event_source *source,
		      int fd,
		      uint32_t mask,
		      void *data)
{
	sink_io(data, mask);
	return 0;
}

static int sink_connect(struct ctl_sink *s)
{
	int fd, r;

	if (!s)
		return cli_EINVAL();
	if (s->fd >= 0)
		return 0;
	if (!s->addr.ss_family || !s->addr_size)
		return cli_EINVAL();

	fd = socket(s->addr.ss_family,
		    SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
		    0);
	if (fd < 0)
		return cli_ERRNO();

	r = connect(fd, (struct sockaddr*)&s->addr, s->addr_size);
	if (r < 0) {
		r = -errno;
		if (r != -EINPROGRESS) {
			cli_vERR(r);
			goto err_close;
		}
	}

	r = sd_event_add_io(s->event,
			    &s->fd_source,
			    fd,
			    EPOLLHUP | EPOLLERR | EPOLLIN | EPOLLOUT | EPOLLET,
			    sink_io_fn,
			    s);
	if (r < 0) {
		cli_vERR(r);
		goto err_close;
	}

	s->fd = fd;
	return 0;

err_close:
	close(fd);
	return r;
}

static void sink_close(struct ctl_sink *s)
{
	if (!s || s->fd < 0)
		return;

	rtsp_remove_match(s->rtsp, sink_rtsp_fn, s);
	rtsp_detach_event(s->rtsp);
	rtsp_unref(s->rtsp);
	s->rtsp = NULL;
	sd_event_source_unref(s->fd_source);
	s->fd_source = NULL;
	close(s->fd);
	s->fd = -1;
	s->connected = false;
	s->hup = false;
}

/*
 * Sink Management
 */

int ctl_sink_new(struct ctl_sink **out,
		 sd_event *event)
{
	struct ctl_sink *s;

	if (!out || !event)
		return cli_EINVAL();

	s = calloc(1, sizeof(*s));
	if (!s)
		return cli_ENOMEM();

	s->event = sd_event_ref(event);
	s->fd = -1;

	*out = s;
	return 0;
}

void ctl_sink_free(struct ctl_sink *s)
{
	if (!s)
		return;

	ctl_sink_close(s);
	free(s->target);
	sd_event_unref(s->event);
	free(s);
}

int ctl_sink_connect(struct ctl_sink *s, const char *target)
{
	struct sockaddr_in addr = { };
	char *t;
	int r;

	if (!s || !target || s->fd >= 0)
		return cli_EINVAL();

	addr.sin_family = AF_INET;
	addr.sin_port = htons(7236);
	r = inet_pton(AF_INET, target, &addr.sin_addr);
	if (r != 1)
		return cli_EINVAL();

	t = strdup(target);
	if (!t)
		return cli_ENOMEM();

	free(s->target);
	s->target = t;

	memcpy(&s->addr, &addr, sizeof(addr));
	s->addr_size = sizeof(addr);

	return sink_connect(s);
}

void ctl_sink_close(struct ctl_sink *s)
{
	if (!s)
		return;

	sink_close(s);
}

bool ctl_sink_is_connecting(struct ctl_sink *s)
{
	return s && s->fd >= 0 && !s->connected;
}

bool ctl_sink_is_connected(struct ctl_sink *s)
{
	return s && s->connected;
}

bool ctl_sink_is_closed(struct ctl_sink *s)
{
	return !s || s->fd < 0;
}
