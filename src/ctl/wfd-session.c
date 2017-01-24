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
#include "ctl.h"
#include "rtsp.h"

#define wfd_session(s)			((struct wfd_session *) (s))
#define wfd_is_session(s)		(\
				(s) && \
				(WFD_SESSION_DIR_OUT == wfd_session(s)->dir || \
				WFD_SESSION_DIR_IN == wfd_session(s)->dir) \
)
#define wfd_out_session(s)		((struct wfd_out_session *) (s))
#define wfd_is_out_session(s)	(WFD_SESSION_DIR_OUT == wfd_session(s)->dir)
#define wfd_in_session(s)		((struct wfd_in_session *) (s))
#define wfd_is_in_session(s)	(WFD_SESSION_DIR_IN == wfd_session(s)->dir)

struct wfd_session_vtable
{
	int (*start)(struct wfd_session *s);
	int (*end)(struct wfd_session *s);
	void (*distruct)(struct wfd_session *s);
};

struct wfd_out_session
{
	struct wfd_session parent;
	struct wfd_sink *sink;
};

static int wfd_out_session_start(struct wfd_session *s);
static int wfd_out_session_end(struct wfd_session *s);
static void wfd_out_session_distruct(struct wfd_session *s);

static const struct wfd_session_vtable session_vtables[] = {
	[WFD_SESSION_DIR_OUT] = {
		.start		= wfd_out_session_start,
		.end		= wfd_out_session_end,
		.distruct	= wfd_out_session_distruct,
	}
};

int wfd_out_session_new(struct wfd_session **out, struct wfd_sink *sink)
{
	int r;
	struct wfd_out_session *s = calloc(1, sizeof(struct wfd_out_session));
	if(!s) {
		return -ENOMEM;
	}

	wfd_session(s)->dir = WFD_SESSION_DIR_OUT;
	wfd_session(s)->fd = -1;
	wfd_session(s)->hup = true;
	s->sink = sink;

	r = ctl_wfd_add_session(ctl_wfd_get(), wfd_session(s));
	if(0 > r) {
		wfd_session_free(wfd_session(s));
	}
	else {
		*out = wfd_session(s);
	}

	return r;
}

uint64_t wfd_session_get_id(struct wfd_session *s)
{
	return s->id;
}

void wfd_session_set_id(struct wfd_session *s, uint64_t id)
{
	assert(id);

	s->id = id;
}

int wfd_session_start(struct wfd_session *s)
{
	assert(wfd_is_session(s));

	return (*session_vtables[s->dir].start)(s);
}

void wfd_session_free(struct wfd_session *s)
{
	if(!wfd_is_session(s)) {
		return;
	}

	if(s->id) {
		ctl_wfd_remove_session_by_id(ctl_wfd_get(), s->id, NULL);
	}

	if(session_vtables[s->dir].distruct) {
		(*session_vtables[s->dir].distruct)(s);
	}
	free(s->url);
	rtsp_unref(s->rtsp);
	free(s);
}

enum wfd_session_dir wfd_session_get_dir(struct wfd_session *s)
{
	return s->dir;
}

static int wfd_session_gen_url(struct wfd_session *s, const char *addr)
{
	char *url;
	int r = asprintf(&url, "rtsp://%s/wfd1.0", addr);
	if(0 <= r) {
		free(s->url);
		s->url = url;
	}

	return r;
}

int wfd_out_session_handle_message(struct rtsp *rtsp,
				struct rtsp_message *m,
				void *userdata)
{
	return 0;
}

static int wfd_out_session_handle_options_reply(struct rtsp *bus,
				struct rtsp_message *m,
				void *userdata)
{
	int r = 0;
	/*struct wfd_src *s = data;*/
	/*_rtsp_message_unref_ struct rtsp_message *req = NULL;*/

	/*cli_debug("INCOMING (M1): %s\n", rtsp_message_get_raw(m));*/

	/*if(!rtsp_message_is_reply(m, RTSP_CODE_OK, NULL)) {*/
		/*cli_printf("[" CLI_RED "ERROR" CLI_DEFAULT "] Failed to get OPTIONS from sink\n");*/
		/*goto error;*/
	/*}*/

	/*r = rtsp_message_new_request(s->rtsp,*/
					 /*&req,*/
					 /*"GET_PARAMETER",*/
					 /*s->url);*/
	/*if (r < 0) {*/
		/*cli_vERR(r);*/
		/*goto error;*/
	/*}*/

	/*r = rtsp_message_append(req, "{&}",*/
			/*"wfd_video_formats\n"*/
			/*//"wfd_audio_codecs\n"*/
			/*"wfd_client_rtp_ports\n"*/
			/*//"wfd_uibc_capability"*/
	/*);*/
	/*if (r < 0) {*/
		/*cli_vERR(r);*/
		/*goto error;*/
	/*}*/

	/*rtsp_message_seal(req);*/
	/*cli_debug("OUTGOING (M3): %s\n", rtsp_message_get_raw(req));*/

	/*r = rtsp_call_async(s->rtsp, req, src_get_parameter_rep_fn, s, 0, NULL);*/
	/*if (r < 0) {*/
		/*cli_vERR(r);*/
		/*goto error;*/
	/*}*/

	/*return 0;*/

/*error:*/
	/*wfd_src_close(s);*/
	/*wfd_fn_src_disconnected(s);*/

	return r;
}

static int wfd_out_session_send_options(struct wfd_session *s)
{
	_rtsp_message_unref_ struct rtsp_message *req = NULL;
	int r = rtsp_message_new_request(s->rtsp,
					&req,
					"OPTIONS",
					"*");
	if (0 > r) {
		return r;
	}

	r = rtsp_message_append(req, "<s>",
					"Require",
					"org.wfa.wfd1.0");
	if (0 > r) {
		return r;
	}

	r = rtsp_message_seal(req);
	if(0 > r) {
		return r;
	}

	r = rtsp_call_async(s->rtsp,
					req,
					wfd_out_session_handle_options_reply,
					s,
					0,
					NULL);
	if (0 > r) {
		return r;
	}

	log_debug("Sending RTSP M1 message: %s",
					(char *) rtsp_message_get_raw(req));

	return 0;
}

static int wfd_out_session_handle_incoming_conn(struct wfd_out_session *os)
{
	int r, val;
	struct sockaddr_storage addr;
	socklen_t len;
	_shl_close_ int fd = -1;
	sd_event *loop;
	struct wfd_session *s = wfd_session(os);

	log_debug("got connection request\n");

	len = sizeof(addr);
	fd = accept4(s->fd, (struct sockaddr *) &addr, &len, SOCK_CLOEXEC);
	close(s->fd);
	s->fd = -1;
	if(0 > fd) {
		return -ENOTCONN;
	}

	r = getsockopt(fd, SOL_SOCKET, SO_ERROR, &val, &len);
	if (r < 0) {
		s->hup = true;
		return r;
	}
	else if (val) {
		s->hup = true;
		errno = val;
		return r;
	}

	cli_debug("connection established");

	r = rtsp_open(&s->rtsp, s->fd);
	if (0 > r) {
		goto error;
	}

	r = sd_event_default(&loop);
	if(0 > r) {
		goto unref_rtsp;
	}

	r = rtsp_attach_event(s->rtsp, loop, 0);
	sd_event_unref(loop);
	if (0 > r) {
		goto unref_rtsp;
	}

	r = rtsp_add_match(s->rtsp, wfd_out_session_handle_message, s);
	if (0 > r) {
		goto unref_rtsp;
	}

	r = wfd_out_session_send_options(s);
	if(0 > r) {
		goto unref_rtsp;
	}

	s->fd = fd;
	fd = -1;
	s->connected = true;
	//wfd_fn_src_connected(s);

	return 0;

unref_rtsp:
	rtsp_unref(s->rtsp);
error:
	s->hup = true;
	return r;
}

static int wfd_out_session_handle_io(sd_event_source *source,
				int fd,
				uint32_t mask,
				void *userdata)
{
	sd_event_source_set_enabled(source, SD_EVENT_OFF);
	sd_event_source_unref(source);

	if (mask & EPOLLIN) {
		return wfd_out_session_handle_incoming_conn(userdata);
	}

	/*if (mask & EPOLLERR) {*/
		/*cli_notice("ERR on socket");*/
		/*s->hup = true;*/
	/*}*/

	/*if (s->hup) {*/
		/*wfd_src_close(s);*/
		/*wfd_fn_src_disconnected(s);*/
	/*}*/

	return 0;
}

static int wfd_out_session_start(struct wfd_session *s)
{
	struct wfd_out_session *os = wfd_out_session(s);
	union wfd_sube sube;
	struct sockaddr_in addr = {};
	struct ctl_peer *p = os->sink->peer;
	int enable;
	_shl_close_ int fd = -1;
	sd_event *loop;
	int r;

	if(!os->sink->peer->connected) {
		log_info("peer not connected yet");
		return -ENOTCONN;
	}

	r = wfd_sube_parse(p->l->wfd_subelements, &sube);
	if(0 > r) {
		log_warning("WfdSubelements property of link must be set before P2P scan");
		return -EINVAL;
	}
	else if(WFD_SUBE_ID_DEVICE_INFO != sube.id) {
		return -EAFNOSUPPORT;
	}

	r = inet_pton(AF_INET, p->local_address, &addr.sin_addr);
	if (!r) {
		return -EAFNOSUPPORT;
	}
	addr.sin_family = AF_INET;
	addr.sin_port = htons(wfd_sube_device_get_rtsp_port(&sube));

	fd = socket(addr.sin_family,
					SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
					0);
	if (0 > fd) {
		return fd;
	}

	enable = true;
	r = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
	if(0 > r) {
		return r;
	}

	r = bind(fd, (struct sockaddr*) &addr, sizeof(addr));
	if (0 > r) {
		return r;
	}

	r = listen(fd, 1);
	if (0 > r) {
		return r;
	}

	log_trace("socket listening on %s:%hu",
					p->local_address,
					wfd_sube_device_get_rtsp_port(&sube));

	r = sd_event_default(&loop);
	if(0 > r) {
		return r;
	}

	r = sd_event_add_io(loop,
				NULL,
				fd,
				EPOLLERR | EPOLLIN | EPOLLET,
				wfd_out_session_handle_io,
				s);
	if (r < 0) {
		return r;
	}

	r = wfd_session_gen_url(s, p->local_address);
	if(0 <= r) {
		s->fd = fd;
		fd = -1;
	}

	return r;
}

static int wfd_out_session_end(struct wfd_session *s)
{
	return 0;
}

static void wfd_out_session_distruct(struct wfd_session *s)
{

}

