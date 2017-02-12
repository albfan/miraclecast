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
#define LOG_SUBSYSTEM "wfd-session"

#include <arpa/inet.h>
#include "ctl.h"
#include "rtsp.h"
#include "wfd-dbus.h"

#define wfd_out_session(s)		(assert(wfd_is_out_session(s)), (struct wfd_out_session *) (s))
#define wfd_in_session(s)		(assert(wfd_is_in_session(s)), (struct wfd_in_session *) (s))

struct wfd_session_vtable
{
	int (*start)(struct wfd_session *s);
	void (*end)(struct wfd_session *s);
	void (*distruct)(struct wfd_session *s);
};

struct wfd_out_session
{
	struct wfd_session parent;
	struct wfd_sink *sink;
	int fd;

	bool sink_has_video: 1;
	bool sink_has_audio: 1;
	bool sink_has_pref_disp_mode: 1;
	bool sink_has_3d: 1;
	bool sink_has_uibc: 1;
};

static void wfd_session_set_state(struct wfd_session *s, enum wfd_session_state state);
static int wfd_out_session_start(struct wfd_session *s);
static void wfd_out_session_end(struct wfd_session *s);
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
	struct wfd_out_session *s = calloc(1, sizeof(struct wfd_out_session));
	if(!s) {
		return -ENOMEM;
	}

	wfd_session(s)->dir = WFD_SESSION_DIR_OUT;
	s->fd = -1;
	s->sink = sink;

	*out = wfd_session(s);

	return 0;
}

struct wfd_sink * wfd_out_session_get_sink(struct wfd_session *s)
{
	assert(wfd_is_out_session(s));

	return wfd_out_session(s)->sink;
}

uint64_t wfd_session_get_id(struct wfd_session *s)
{
	return s->id;
}

static void wfd_session_set_state(struct wfd_session *s,
				enum wfd_session_state state)
{
	if(state == s->state) {
		return;
	}

	s->state = state;

	wfd_fn_session_properties_changed(s, "State");
}

int wfd_session_start(struct wfd_session *s, uint64_t id)
{
	int r;

	assert(wfd_is_session(s));
	assert(id);

	if(wfd_session_is_started(s)) {
		return -EINPROGRESS;
	}

	r = (*session_vtables[s->dir].start)(s);
	if(0 <= r) {
		s->id = id;
		wfd_session_set_state(s, WFD_SESSION_STATE_CONNECTING);
	}

	return r;
}

int wfd_session_is_started(struct wfd_session *s)
{
	assert(wfd_is_session(s));

	return 0 != s->id;
}

void wfd_session_end(struct wfd_session *s)
{
	assert(wfd_is_session(s));

	if(!wfd_session_is_started(s)) {
		return;
	}

	log_info("session %lu ended", s->id);

	(*session_vtables[s->dir].end)(s);

	if(s->rtsp) {
		rtsp_unref(s->rtsp);
		s->rtsp = NULL;
	}

	if(s->url) {
		free(s->url);
		s->url = NULL;
	}

	s->hup = false;

	wfd_session_set_state(s, WFD_SESSION_STATE_NULL);

	if(wfd_is_out_session(s)) {
		wfd_fn_out_session_ended(s);
	}
}

void wfd_session_free(struct wfd_session *s)
{
	enum wfd_session_dir dir;

	if(!wfd_is_session(s)) {
		return;
	}

	if(wfd_is_out_session(s)) {
		wfd_out_session(s)->sink = NULL;
	}

	wfd_session_end(s);

	if(session_vtables[dir].distruct) {
		(*session_vtables[dir].distruct)(s);
	}

	free(s);
}

enum wfd_session_dir wfd_session_get_dir(struct wfd_session *s)
{
	return s->dir;
}

const char * wfd_session_get_url(struct wfd_session *s)
{
	return s->url;
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

static int wfd_out_session_handle_get_parameter_reply(struct rtsp *bus,
				struct rtsp_message *m,
				void *userdata)
{
	log_debug("Received GET_PARAMETER reply (M4): %s\n",
					(char *) rtsp_message_get_raw(m));
	if(RTSP_CODE_OK == rtsp_message_get_code(m)) {
		return 0;
	}

	log_warning("Sink reply GET_PARAMETER (M4) with code: %d",
					rtsp_message_get_code(m));

	wfd_session_end(wfd_session(userdata));

	return -EPROTO;
}

static int wfd_out_session_send_get_parameter(sd_event_source *source,
				void *userdata)
{
	struct wfd_session *s = userdata;
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r = rtsp_message_new_request(s->rtsp,
					&m,
					"GET_PARAMETER",
					s->url);
	if (0 > r) {
		goto error;
	}

	r = rtsp_message_append(m, "{&}",
			"wfd_video_formats\n"
			"wfd_audio_codecs\n"
			"wfd_client_rtp_ports"
			//"wfd_uibc_capability"
	);
	if (0 > r) {
		goto error;
	}

	rtsp_message_seal(m);

	r = rtsp_call_async(s->rtsp,
					m,
					wfd_out_session_handle_get_parameter_reply,
					s,
					0,
					NULL);
	if (r < 0) {
		goto error;
	}

	log_debug("Sending GET_PARAMETER (M3): %s\n",
					(char *) rtsp_message_get_raw(m));

	return 0;

error:
	wfd_session_end(s);

	return r;
}

static int wfd_out_session_handle_options_reply(struct rtsp *bus,
				struct rtsp_message *m,
				void *userdata)
{
	int r = 0, i;
	struct wfd_session *s = userdata;
	char *public, *methods[3] = { NULL };

	log_trace("received OPTIONS reply (M1): %s",
					(char *) rtsp_message_get_raw(m));

	if(!rtsp_message_is_reply(m, RTSP_CODE_OK, NULL)) {
		r = -EPROTO;
		goto error;
	}

	r = rtsp_message_read(m, "<&>", "Public", &public);
	if(0 > r) {
		goto error;
	}

	r = sscanf(public, "%m[^,], %m[^,], %ms", &methods[0], &methods[1], &methods[2]);
	if(3 != r) {
		r = -EINVAL;
		goto error;
	}

	for(i = 0; i < SHL_ARRAY_LENGTH(methods); i ++) {
		if(strcmp("org.wfa.wfd1.0", methods[i]) &&
						strcmp("SET_PARAMETER", methods[i]) &&
						strcmp("GET_PARAMETER", methods[i]))
		{
			r = -EINVAL;
			log_info("Got invalid method from sink: %s", methods[i]);
		}

		free(methods[i]);
	}

	if(0 > r) {
		goto error;
	}

	r = sd_event_add_defer(ctl_wfd_get_loop(),
					NULL,
					wfd_out_session_send_get_parameter,
					s);
	if(0 > r) {
		goto error;
	}

	return 0;

error:
	wfd_session_end(s);

	log_info("error occured while handling reply of OPTIONS");

	return r;
}

static int wfd_out_session_send_options(sd_event_source *source,
				void *userdata)
{
	struct wfd_session *s = userdata;
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r = rtsp_message_new_request(s->rtsp,
					&m,
					"OPTIONS", "*");
	if (0 > r) {
		goto error;
	}

	r = rtsp_message_append(m,
					"<s>",
					"Require", "org.wfa.wfd1.0");
	if (0 > r) {
		goto error;
	}
	
	r = rtsp_message_seal(m);
	if(0 > r) {
		goto error;
	}

	r = rtsp_call_async(s->rtsp,
					m,
					wfd_out_session_handle_options_reply,
					s,
					0,
					NULL);
	if (0 > r) {
		goto error;
	}

	log_debug("sending OPTIONS (M1): %s", (char *) rtsp_message_get_raw(m));

	return 0;

error:
	log_info("failed to send OPTIONS request: %s", strerror(errno));
	wfd_session_end(s);

	return r;
}

static int wfd_out_session_handle_options_request(struct rtsp *bus,
				struct rtsp_message *request,
				struct wfd_session *s)
{
	const char *require;
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r = rtsp_message_read(request, "<s>", "Require", &require);
	if(0 > r) {
		goto error;
	}

	if(strcmp("org.wfa.wfd1.0", require)) {
		r = rtsp_message_new_reply_for(request,
						&m, 
						RTSP_CODE_OPTION_NOT_SUPPORTED,
						"Invalid specification");
	}
	else {
		r = rtsp_message_new_reply_for(request,
						&m,
						RTSP_CODE_OK,
						NULL);
		if(0 > r) {
			goto error;
		}

		r = rtsp_message_append(m,
						"<s>",
						"Public", "org.wfa.wfd1.0, SETUP, TEARDOWN, PLAY, PAUSE, GET_PARAMETER, SET_PARAMETER");
	}

	if(0 > r) {
		goto error;
	}

	r = rtsp_message_seal(m);
	if(0 > r) {
		goto error;
	}

	r = rtsp_send(bus, m);
	if(0 > r) {
		goto error;
	}

	log_debug("sending OPTIONS reply (M2): %s",
					(char *) rtsp_message_get_raw(m));

	return 0;

error:
	s->hup = true;
	return r;
}

static int wfd_out_session_dispatch_request(struct rtsp *bus,
				struct rtsp_message *m,
				void *userdata)
{
	const char *method;
	struct wfd_session *s = userdata;
	int r = 0;

	if (!m) {
		s->hup = true;
		goto end;
	}

	method = rtsp_message_get_method(m);
	if(!method) {
		log_info("unexpected message: %s", (char *) rtsp_message_get_raw(m));
		r = -EINVAL;
		goto end;
	}

	if (!strcmp(method, "OPTIONS")) {
		r = wfd_out_session_handle_options_request(bus, m, s);
	}
	/*else if (!strcmp(method, "GET_PARAMETER")) {*/
		/*wfd_out_session_handle_get_parameter_reply(s, m);*/
	/*}*/
	/*[>else if (!strcmp(method, "SETUP")) {<]*/
		/*[>wfd_out_session_handle_setup_reply(s, m);<]*/
	/*[>}<]*/
	/*[>else if (!strcmp(method, "PLAY")) {<]*/
		/*[>wfd_out_session_handle_play_reply(s, m);<]*/
	/*[>}<]*/
	/*[>else if (!strcmp(method, "PAUSE")) {<]*/
		/*[>wfd_out_session_handle_pause_reply(s, m);<]*/
	/*[>}<]*/
	/*[>else if (!strcmp(method, "TEARDOWN")) {<]*/
		/*[>wfd_out_session_handle_teardown_reply(s, m);<]*/
	/*[>}<]*/

end:
	if (s->hup) {
		wfd_session_end(s);
	}

	return r;

}

static int wfd_out_session_accept_connection(struct wfd_out_session *os)
{
	int r;
	socklen_t len;
	struct sockaddr_storage addr;
	_shl_close_ int fd = -1;
	sd_event *loop = ctl_wfd_get_loop();
	struct wfd_session *s = wfd_session(os);
	_rtsp_unref_ struct rtsp *rtsp = NULL;

	log_debug("accepting incoming RTSP connection\n");

	len = sizeof(addr);
	fd = accept4(os->fd,
					(struct sockaddr *) &addr,
					&len,
					SOCK_NONBLOCK | SOCK_CLOEXEC);
	if(0 > fd) {
		return -ENOTCONN;
	}

	log_info("RTSP connection established");

	r = rtsp_open(&rtsp, fd);
	if (0 > r) {
		goto error;
	}

	fd = -1;

	r = rtsp_attach_event(rtsp, loop, 0);
	if (0 > r) {
		goto error;
	}

	r = rtsp_add_match(rtsp, wfd_out_session_dispatch_request, s);
	if (0 > r) {
		goto error;
	}

	r = sd_event_add_defer(loop, NULL, wfd_out_session_send_options, s);
	if(0 > r) {
		goto error;
	}

	wfd_session_set_state(s, WFD_SESSION_STATE_CAPS_EXCHAING);

	s->rtsp = rtsp;
	rtsp = NULL;

	close(os->fd);
	os->fd = -1;

	return 0;

error:
	s->hup = true;
	return r;
}

static int wfd_out_session_handle_io(sd_event_source *source,
				int fd,
				uint32_t mask,
				void *userdata)
{
	int r, val;
	socklen_t len;
	struct wfd_session *s = userdata;

	sd_event_source_set_enabled(source, SD_EVENT_OFF);

	if (mask & EPOLLERR) {
		r = getsockopt(fd, SOL_SOCKET, SO_ERROR, &val, &len);
		s->hup = true;
		if(0 <= r) {
			r = -val;
			errno = val;
		}
		goto end;
	}

	if (mask & EPOLLIN) {
		r = wfd_out_session_accept_connection(userdata);
	}

end:
	if (s->hup) {
		log_info("failed to accept remote connection: %s", strerror(errno));
		wfd_session_end(s);
	}

	return r;
}

static int wfd_out_session_start(struct wfd_session *s)
{
	struct wfd_out_session *os = wfd_out_session(s);
	union wfd_sube sube;
	struct sockaddr_in addr = {};
	struct ctl_peer *p = os->sink->peer;
	_shl_close_ int fd = -1;
	int enable;
	int r;

	if(!os->sink->peer->connected) {
		log_info("peer not connected yet");
		return -ENOTCONN;
	}

	r = wfd_sube_parse_with_id(WFD_SUBE_ID_DEVICE_INFO,
					p->l->wfd_subelements,
					&sube);
	if(0 > r) {
		log_warning("WfdSubelements property of link must be set before P2P scan");
		return -EINVAL;
	}
	else if(WFD_SUBE_ID_DEVICE_INFO != sube.id) {
		return -EAFNOSUPPORT;
	}

	if(-1 != os->fd) {
		return EINPROGRESS;
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

	r = listen(fd, 10);
	if (0 > r) {
		return r;
	}

	log_trace("socket listening on %s:%hu",
					p->local_address,
					wfd_sube_device_get_rtsp_port(&sube));

	r = sd_event_add_io(ctl_wfd_get_loop(),
				NULL,
				fd,
				EPOLLIN,
				wfd_out_session_handle_io,
				s);
	if (r < 0) {
		return r;
	}

	r = wfd_session_gen_url(s, p->local_address);
	if(0 <= r) {
		os->fd = fd;
		fd = -1;
	}

	return r;
}

static void wfd_out_session_end(struct wfd_session *s)
{
	struct wfd_out_session *os = wfd_out_session(s);
	if(os->fd) {
		close(os->fd);
		os->fd = -1;
	}
}

static void wfd_out_session_distruct(struct wfd_session *s)
{
	struct wfd_out_session *os = wfd_out_session(s);
	if(0 <= os->fd) {
		close(os->fd);
	}
}

