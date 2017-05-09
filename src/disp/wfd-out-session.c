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
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <stdio.h>
#include <fcntl.h>
#include "wfd-session.h"
#include "shl_log.h"
#include "util.h"
#include "rtsp.h"
#include "ctl.h"
#include "dispd-encoder.h"

#define LOCAL_RTP_PORT		16384
#define LOCAL_RTCP_PORT		16385
#define KEEP_ALIVE_INTERVAL	30

struct wfd_out_session
{
	struct wfd_session parent;
	struct wfd_sink *sink;
	int fd;

	struct dispd_encoder *encoder;

	sd_event_source *keep_alive_timer;
};

static void on_encoder_state_changed(struct dispd_encoder *e,
				enum dispd_encoder_state state,
				void *userdata);
static void wfd_out_session_cancel_sink_alive_checking(struct wfd_session *s);

static const struct rtsp_dispatch_entry out_session_rtsp_disp_tbl[];

int wfd_out_session_new(struct wfd_session **out,
				unsigned int id,
				struct wfd_sink *sink)
{
	_wfd_session_unref_ struct wfd_session *s;
	int r;

	assert_ret(out);
	assert_ret(id);
	assert_ret(sink);

	s = calloc(1, sizeof(struct wfd_out_session));
	if(!s) {
		return log_ENOMEM();
	}

	r = wfd_session_init(s,
					id,
					WFD_SESSION_DIR_OUT,
					out_session_rtsp_disp_tbl);
	if(0 > r) {
		return log_ERR(r);
	}

	wfd_out_session(s)->fd = -1;
	wfd_out_session(s)->sink = sink;

	*out = wfd_session_ref(s);

	return 0;
}

struct wfd_sink * wfd_out_session_get_sink(struct wfd_session *s)
{
	assert(wfd_is_out_session(s));

	return wfd_out_session(s)->sink;
}

int wfd_out_session_handle_io(struct wfd_session *s,
				int error,
				int *out_fd)
{
	socklen_t len;
	struct sockaddr_storage addr;
	_shl_close_ int fd = -1;
	struct wfd_out_session *os = wfd_out_session(s);

	log_debug("accepting incoming RTSP connection\n");

	len = sizeof(addr);
	fd = accept4(os->fd,
					(struct sockaddr *) &addr,
					&len,
					SOCK_NONBLOCK | SOCK_CLOEXEC);
	if(0 > fd) {
		return -errno;
	}

	log_info("RTSP connection established");

	close(os->fd);
	os->fd = -1;
	*out_fd = fd;
	fd = -1;

	return 0;
}

int wfd_out_session_initiate_io(struct wfd_session *s,
				int *out_fd,
				uint32_t *out_mask)
{
	struct wfd_out_session *os = wfd_out_session(s);
	union wfd_sube sube;
	struct sockaddr_in addr = {};
	struct ctl_peer *p = os->sink->peer;
	_shl_close_ int fd = -1;
	int enable;
	int r;

	assert_retv(os->sink->peer->connected, -ENOTCONN);
	assert_retv(os->fd, -EINPROGRESS);

	r = wfd_sube_parse_with_id(WFD_SUBE_ID_DEVICE_INFO,
					p->l->wfd_subelements,
					&sube);
	if(0 > r) {
		log_warning("WfdSubelements property of link must be set before P2P scan");
		return log_ERR(-EINVAL);
	}
	else if(WFD_SUBE_ID_DEVICE_INFO != sube.id) {
		return log_ERR(-EAFNOSUPPORT);
	}

	r = inet_pton(AF_INET, p->local_address, &addr.sin_addr);
	if (0 >= r) {
		return log_ERR(-EAFNOSUPPORT);
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(wfd_sube_device_get_rtsp_port(&sube));

	fd = socket(addr.sin_family,
					SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
					0);
	if (0 > fd) {
		return log_ERRNO();
	}

	enable = true;
	r = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
	if(0 > r) {
		return log_ERRNO();
	}

	r = bind(fd, (struct sockaddr*) &addr, sizeof(addr));
	if (0 > r) {
		return log_ERRNO();
	}

	r = listen(fd, 10);
	if (0 > r) {
		return log_ERRNO();
	}

	r = dispd_encoder_spawn(&os->encoder, s);
	if(0 > r) {
		return log_ERRNO();
	}

	dispd_encoder_set_handler(os->encoder, on_encoder_state_changed, s);
	if(0 > r) {
		return log_ERRNO();
	}

	log_trace("socket listen on %s:%hu",
					p->local_address,
					wfd_sube_device_get_rtsp_port(&sube));

	os->fd = fd;
	*out_fd = fd;
	fd = -1;
	*out_mask = EPOLLIN;

	return 0;
}

int wfd_out_session_resume(struct wfd_session *s)
{
	return wfd_session_request(s,
					RTSP_M5_TRIGGER,
					&(struct wfd_arg_list) wfd_arg_list(wfd_arg_cstr("PLAY")));
}

int wfd_out_session_pause(struct wfd_session *s)
{
	return wfd_session_request(s,
					RTSP_M5_TRIGGER,
					&(struct wfd_arg_list) wfd_arg_list(wfd_arg_cstr("PAUSE")));
}

int wfd_out_session_teardown(struct wfd_session *s)
{
	return wfd_session_request(s,
					RTSP_M5_TRIGGER,
					&(struct wfd_arg_list) wfd_arg_list(wfd_arg_cstr("TEARDOWN")));
}

void wfd_out_session_destroy(struct wfd_session *s)
{
	struct wfd_out_session *os;

	assert_vret(s);

	os = wfd_out_session(s);

	wfd_out_session_cancel_sink_alive_checking(s);

	if(0 <= os->fd) {
		close(os->fd);
		os->fd = -1;
	}

	if(os->encoder) {
		dispd_encoder_stop(os->encoder);
		dispd_encoder_set_handler(os->encoder, NULL, NULL);
		dispd_encoder_unref(os->encoder);
		os->encoder = NULL;
	}

	os->sink = NULL;
}

int wfd_out_session_initiate_request(struct wfd_session *s)
{
	return wfd_session_request(s,
					RTSP_M1_REQUEST_SINK_OPTIONS,
					NULL);
}

static int wfd_out_session_handle_get_parameter_reply(struct wfd_session *s,
				struct rtsp_message *m)
{
	struct wfd_video_formats *vformats;
	struct wfd_audio_codecs *acodecs;
	uint16_t rtp_ports[2];
	_shl_free_ char *t = NULL;
	const char *l;
	int r;

	if(!rtsp_message_read(m, "{<&>}", "wfd_video_formats", &l)) {
		r = wfd_video_formats_from_string(l, &vformats);
		if(0 > r) {
			return log_ERR(r);
		}

		if(s->vformats) {
			free(s->vformats);
		}
		s->vformats = vformats;
	}

	if(!rtsp_message_read(m, "{<&>}", "wfd_audio_codecs", &l)) {
		r = wfd_audio_codecs_from_string(l, &acodecs);
		if(0 > r) {
			return log_ERR(r);
		}
		
		if(s->acodecs) {
			free(s->acodecs);
		}
		s->acodecs = acodecs;
	}

	if(!rtsp_message_read(m, "{<&>}", "wfd_client_rtp_ports", &l)) {
		if(strncmp("RTP/AVP/UDP;unicast", l, 19)) {
			return log_EPROTO();
		}

		r = sscanf(l + 20, "%hd %hd %ms",
						&rtp_ports[0],
						&rtp_ports[1],
						&t);
		if(3 != r) {
			return log_EPROTO();
		}

		if(strncmp("mode=play", t, 9)) {
			return log_EPROTO();
		}

		if(!rtp_ports[0] && !rtp_ports[1]) {
			return log_EPROTO();
		}

		s->rtp_ports[0] = rtp_ports[0];
		s->rtp_ports[1] = rtp_ports[1];
	}

	return 0;
}

static int wfd_out_session_request_get_parameter(struct wfd_session *s,
				struct rtsp *bus,
				const struct wfd_arg_list *args,
				struct rtsp_message **out)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r = rtsp_message_new_request(bus,
					&m,
					"GET_PARAMETER",
					"rtsp://localhost/wfd1.0");
	if (0 > r) {
		return log_ERR(r);
	}

	r = rtsp_message_append(m, "{&}",
			"wfd_video_formats\n"
			"wfd_audio_codecs\n"
			"wfd_client_rtp_ports"
			//"wfd_uibc_capability"
	);
	if (0 > r) {
		return log_ERR(r);
	}

	*out = (rtsp_message_ref(m), m);

	return 0;
}

static bool find_strv(const char *str, char **strv)
{
	while(*strv) {
		if(!strcmp(str, *strv)) {
			return true;
		}
		++strv;
	}

	return false;
}

static int wfd_out_session_handle_options_request(struct wfd_session *s,
				struct rtsp_message *req,
				struct rtsp_message **out_rep)
{
	const char *require;
	_rtsp_message_unref_ struct rtsp_message *rep = NULL;
	int r;

	r = rtsp_message_read(req, "<s>", "Require", &require);
	if(0 > r) {
		return log_ERR(r);
	}

	if(strcmp("org.wfa.wfd1.0", require)) {
		r = rtsp_message_new_reply_for(req,
						out_rep, 
						RTSP_CODE_OPTION_NOT_SUPPORTED,
						"Invalid specification");
		if(0 > r) {
			return log_ERR(r);
		}

		return 0;
	}

	r = rtsp_message_new_reply_for(req,
					&rep,
					RTSP_CODE_OK,
					NULL);
	if(0 > r) {
		return log_ERR(r);
	}

	r = rtsp_message_append(rep,
					"<&>",
					"Public", "org.wfa.wfd1.0, SETUP, TEARDOWN, PLAY, PAUSE, GET_PARAMETER, SET_PARAMETER");
	if(0 > r) {
		return log_ERR(r);
	}

	*out_rep = (rtsp_message_ref(rep), rep);

	return 0;
}

static int wfd_out_session_handle_options_reply(struct wfd_session *s,
					struct rtsp_message *m)
{
	int r;
	const char *public;
	char *methods[4];

	r = rtsp_message_read(m, "<&>", "Public", &public);
	if(0 > r) {
		return log_ERR(r);
	}

	r = sscanf(public, "%m[^,], %m[^,], %ms", &methods[0], &methods[1], &methods[2]);
	if(3 != r) {
		return log_EPROTO();
	}

	methods[3] = NULL;
	r = find_strv("org.wfa.wfd1.0", methods) &&
					find_strv("SET_PARAMETER", methods) &&
					find_strv("GET_PARAMETER", methods);
	free(methods[2]);
	free(methods[1]);
	free(methods[0]);
	if(!r) {
		return log_EPROTO();
	}

	return 0;
}

static int wfd_out_session_request_options(struct wfd_session *s,
				struct rtsp *bus,
				const struct wfd_arg_list *args,
				struct rtsp_message **out)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r = rtsp_message_new_request(bus,
					&m,
					"OPTIONS", "*");
	if (0 > r) {
		return log_ERR(r);
	}

	r = rtsp_message_append(m,
					"<s>",
					"Require", "org.wfa.wfd1.0");
	if (0 > r) {
		return log_ERR(r);
	}

	*out = (rtsp_message_ref(m), m);

	return 0;
}

inline static char * uint16_to_str(uint16_t i, char *buf, size_t len)
{
	snprintf(buf, len, "%u", i);

	return buf;
}

inline static char * quote_str(const char *s, char *d, size_t len)
{
	snprintf(d, len, "\"%s\"", s);

	return d;
}

static int wfd_out_session_handle_pause_request(struct wfd_session *s,
						struct rtsp_message *req,
						struct rtsp_message **out_rep)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r;

	r = dispd_encoder_pause(wfd_out_session(s)->encoder);
	if(0 > r) {
		return log_ERR(r);
	}

	r = rtsp_message_new_reply_for(req,
					&m,
					RTSP_CODE_OK,
					NULL);
	if(0 > r) {
		return log_ERR(r);
	}

	*out_rep = (rtsp_message_ref(m), m);

	return 0;
}

static int wfd_out_session_handle_teardown_request(struct wfd_session *s,
						struct rtsp_message *req,
						struct rtsp_message **rep)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r;

	r = rtsp_message_new_reply_for(req,
					&m,
					RTSP_CODE_OK,
					NULL);
	if(0 > r) {
		return log_ERR(r);
	}

	*rep = (rtsp_message_ref(m), m);

	return 0;
}

static int wfd_out_session_handle_play_request(struct wfd_session *s,
						struct rtsp_message *req,
						struct rtsp_message **out_rep)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	_shl_free_ char *v = NULL;
	struct dispd_encoder *e;
	uint64_t now;
	int r;

	r = rtsp_message_new_reply_for(req,
					&m,
					RTSP_CODE_OK,
					NULL);
	if(0 > r) {
		return log_ERR(r);
	}

	r = asprintf(&v, "%X", wfd_session_get_id(s));
	if(0 > r) {
		return log_ERR(r);
	}
	r = rtsp_message_append(m, "<&>", "Session", v);
	if(0 > r) {
		return log_ERR(r);
	}

	r = sd_event_now(ctl_wfd_get_loop(), CLOCK_MONOTONIC, &now);
	if(0 > r) {
		return log_ERR(r);
	}

	e = wfd_out_session(s)->encoder;
	if(DISPD_ENCODER_STATE_CONFIGURED <= dispd_encoder_get_state(e)) {
		r = dispd_encoder_start(e);
		if(0 > r) {
			return log_ERR(r);
		}
	}

	*out_rep = (rtsp_message_ref(m), m);

	return 0;
}

static void on_encoder_state_changed(struct dispd_encoder *e,
				enum dispd_encoder_state state,
				void *userdata)
{
	int r = 0;
	struct wfd_session *s = userdata;

	switch(state) {
		case DISPD_ENCODER_STATE_SPAWNED:
			if(wfd_session_is_state(s, WFD_SESSION_STATE_SETTING_UP)) {
				r = dispd_encoder_configure(wfd_out_session(s)->encoder, s);
				if(0 > r) {
					log_vERR(r);
				}
			}
			break;
		case DISPD_ENCODER_STATE_CONFIGURED:
			if(wfd_session_is_state(s, WFD_SESSION_STATE_SETTING_UP)) {
				r = dispd_encoder_start(e);
				if(0 > r) {
					log_vERR(r);
				}
			}
			break;
		case DISPD_ENCODER_STATE_READY:
			break;
		case DISPD_ENCODER_STATE_STARTED:
			wfd_session_set_state(s, WFD_SESSION_STATE_PLAYING);
			break;
		case DISPD_ENCODER_STATE_PAUSED:
			wfd_session_set_state(s, WFD_SESSION_STATE_PAUSED);
			break;
		case DISPD_ENCODER_STATE_TERMINATED:
			wfd_session_teardown(s);
			break;
		default:
			break;
	}

	return;
}

static int wfd_out_session_request_keep_alive(struct wfd_session *s,
				struct rtsp *bus,
				const struct wfd_arg_list *args,
				struct rtsp_message **out)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	_shl_free_ char *sess = NULL;
	int r;

	r = rtsp_message_new_request(bus,
					&m,
					"GET_PARAMETER",
					"rtsp://localhost/wfd1.0");
	if(0 > r) {
		return log_ERR(r);
	}

	r = asprintf(&sess, "%X", wfd_session_get_id(s));
	if(0 > r) {
		return log_ERR(r);
	}

	r = rtsp_message_append(m, "<&>", "Session", sess);
	if(0 > r) {
		return log_ERR(r);
	}

	*out = (rtsp_message_ref(m), m);

	return 0;
}

static int wfd_out_session_check_sink_alive(sd_event_source *source,
				uint64_t usec,
				void *userdata)
{
	struct wfd_session *s = userdata;

	wfd_session_request(s, RTSP_M16_KEEPALIVE, NULL);

	return 0;
}

static int wfd_out_session_init_sink_alive_checking(struct wfd_session *s)
{
	struct wfd_out_session *os = wfd_out_session(s);
	uint64_t now;
	int r;

	assert_ret(s);

	if(os->keep_alive_timer) {
		return 0;
	}

	r = sd_event_now(ctl_wfd_get_loop(), CLOCK_MONOTONIC, &now);
	if(0 > r) {
		return log_ERR(r);
	}

	/* WFD Spec, Section 6.5.1
	 * A WFD Source shall send RTSP M16 request messages and the time interval
	 * of two successive
	 * RTSP M16 request messages shall be smaller than the timeout value set by
	 * the RTSP M6
	 * response message minus 5 seconds. */
	r = sd_event_add_time(ctl_wfd_get_loop(),
					&os->keep_alive_timer,
					CLOCK_MONOTONIC,
					now + (KEEP_ALIVE_INTERVAL - 5) * 1000 * 1000,
					0,
					wfd_out_session_check_sink_alive,
					wfd_session_ref(s));
	if(0 > r) {
		return log_ERR(r);
	}

	return 0;
}

static void wfd_out_session_cancel_sink_alive_checking(struct wfd_session *s)
{
	struct wfd_out_session *os = wfd_out_session(s);

	assert_vret(s);

	if(!os->keep_alive_timer) {
		return;
	}

	sd_event_source_set_enabled(os->keep_alive_timer, false);
	sd_event_source_unref(os->keep_alive_timer);
	os->keep_alive_timer = NULL;

	wfd_session_unref(s);
}

static int wfd_out_session_handle_setup_request(struct wfd_session *s,
				struct rtsp_message *req,
				struct rtsp_message **out_rep)
{
	int r;
	char *l;
	struct wfd_out_session *os = wfd_out_session(s);
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	_shl_free_ char *sess = NULL, *trans = NULL;

	r = rtsp_message_read(req, "<s>", "Transport", &l);
	if(0 > r) {
		return log_EPROTO();
	}

	if(strncmp("RTP/AVP/UDP;unicast;", l, 20)) {
		return log_EPROTO();
	}

	l += 20;

	if(strncmp("client_port=", l, 12)) {
		return log_EPROTO();
	}

	l += 12;

	errno = 0;
	s->stream.rtp_port = strtoul(l, &l, 10);
	if(errno) {
		return log_EPROTO();
	}

	if('-' == *l) {
		errno = 0;
		s->stream.rtcp_port = strtoul(l + 1, NULL, 10);
		if(errno) {
			return log_EPROTO();
		}
	}
	else {
		s->stream.rtcp_port = 0;
	}

	r = rtsp_message_new_reply_for(req,
					&m,
					RTSP_CODE_OK,
					NULL);
	if(0 > r) {
		return log_ERRNO();
	}

	r = asprintf(&sess, "%X;timeout=%d",
					wfd_session_get_id(s),
					KEEP_ALIVE_INTERVAL);
	if(0 > r) {
		return log_ERRNO();
	}

	r = rtsp_message_append(m, "<&>", "Session", sess);
	if(0 > r) {
		return log_ERRNO();
	}

	r = asprintf(&trans, "RTP/AVP/UDP;unicast;client_port=%hu%s;server_port=%u-%u",
					s->stream.rtp_port,
					l,
					LOCAL_RTP_PORT,
					LOCAL_RTCP_PORT);
	if(0 > r) {
		return log_ERRNO();
	}

	r = rtsp_message_append(m, "<&>", "Transport", trans);
	if(0 > r) {
		return log_ERRNO();
	}

	if(DISPD_ENCODER_STATE_SPAWNED == dispd_encoder_get_state(os->encoder)) {
		dispd_encoder_configure(os->encoder, s);
	}

	wfd_out_session_init_sink_alive_checking(s);

	*out_rep = (rtsp_message_ref(m), m);

	return 0;
}

static int wfd_out_session_handle_idr_request(struct wfd_session *s,
				struct rtsp_message *req,
				struct rtsp_message **out_rep)
{
	int r = rtsp_message_new_reply_for(req,
					out_rep,
					RTSP_CODE_OK,
					NULL);
	if(0 > r) {
		return log_ERR(r);
	}

	return 0;
}

static int wfd_out_session_request_trigger(struct wfd_session *s,
				struct rtsp *bus,
				const struct wfd_arg_list *args,
				struct rtsp_message **out)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r;
	const char *method;

	assert(args);

	wfd_arg_list_get(args, 0, &method);

	assert(method);

	r = rtsp_message_new_request(bus,
					 &m,
					 "SET_PARAMETER",
					 wfd_session_get_stream_url(s));
	if(0 > r) {
		return log_ERRNO();
	}

	r = rtsp_message_append(m, "{<s>}",
					"wfd_trigger_method",
					method);
	if(0 > r) {
		return log_ERRNO();
	}

	*out = m;
	m = NULL;

	return 0;
}

static int wfd_out_session_request_not_implement(struct wfd_session *s,
						struct rtsp_message *req,
						struct rtsp_message **out_rep)
{
	return rtsp_message_new_reply_for(req,
					out_rep,
					RTSP_CODE_NOT_IMPLEMENTED,
					NULL);
}

static int wfd_out_session_request_set_parameter(struct wfd_session *s,
				struct rtsp *bus,
				const struct wfd_arg_list *args,
				struct rtsp_message **out)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	_shl_free_ char *body = NULL;
	int r;

	r = wfd_session_gen_stream_url(s,
					wfd_out_session(s)->sink->peer->local_address,
					WFD_STREAM_ID_PRIMARY);
	if(0 > r) {
		return log_ERRNO();
	}

	s->stream.id = WFD_STREAM_ID_PRIMARY;

	r = asprintf(&body,
					"wfd_video_formats: 00 00 02 10 %08X %08X %08X 00 0000 0000 00 none none\n"
					"wfd_audio_codecs: AAC 00000001 00\n"
					"wfd_presentation_URL: %s none\n"
					"wfd_client_rtp_ports: RTP/AVP/UDP;unicast %u %u mode=play",
					//"wfd_uibc_capability: input_category_list=GENERIC\n;generic_cap_list=SingleTouch;hidc_cap_list=none;port=5100\n"
					//"wfd_uibc_setting: disable\n",
					0x80,
					0,
					0,
					wfd_session_get_stream_url(s),
					s->rtp_ports[0],
					s->rtp_ports[1]);
	if(0 > r) {
		return log_ERRNO();
	}

	r = rtsp_message_new_request(bus,
					&m,
					"SET_PARAMETER",
					"rtsp://localhost/wfd1.0");
	if (0 > r) {
		return log_ERRNO();
	}

	r = rtsp_message_append(m, "{&}", body);
	if (0 > r) {
		return log_ERRNO();
	}

	*out = m;
	m = NULL;

	return 0;
}

static const struct rtsp_dispatch_entry out_session_rtsp_disp_tbl[] = {
	[RTSP_M1_REQUEST_SINK_OPTIONS] = {
		.request = wfd_out_session_request_options,
		.handle_reply = wfd_out_session_handle_options_reply
	},
	[RTSP_M2_REQUEST_SRC_OPTIONS] = {
		.handle_request = wfd_out_session_handle_options_request,
		.rule = wfd_arg_list(
			wfd_arg_dict(
				wfd_arg_u(WFD_SESSION_ARG_NEXT_REQUEST),
				wfd_arg_u(RTSP_M3_GET_PARAMETER)
			),
		)
	},
	[RTSP_M3_GET_PARAMETER] = {
		.request = wfd_out_session_request_get_parameter,
		.handle_reply = wfd_out_session_handle_get_parameter_reply,
		.rule = wfd_arg_list(
			wfd_arg_dict(
					wfd_arg_u(WFD_SESSION_ARG_NEXT_REQUEST),
					wfd_arg_u(RTSP_M4_SET_PARAMETER)
			),
		)
	},
	[RTSP_M4_SET_PARAMETER] = {
		.request = wfd_out_session_request_set_parameter,
		.rule = wfd_arg_list(
			wfd_arg_dict(
					wfd_arg_u(WFD_SESSION_ARG_NEXT_REQUEST),
					wfd_arg_u(RTSP_M5_TRIGGER)
			),
			wfd_arg_dict(
					wfd_arg_u(WFD_SESSION_ARG_NEW_STATE),
					wfd_arg_u(WFD_SESSION_STATE_ESTABLISHED)
			),
			wfd_arg_dict(
					wfd_arg_u(WFD_SESSION_ARG_REQUEST_ARGS),
					wfd_arg_arg_list(wfd_arg_cstr("SETUP"))
			),
		)
	},
	[RTSP_M5_TRIGGER]				= {
		.request = wfd_out_session_request_trigger,
	},
	[RTSP_M6_SETUP]					= {
		.handle_request = wfd_out_session_handle_setup_request,
		.rule = wfd_arg_list(
			wfd_arg_dict(
					wfd_arg_u(WFD_SESSION_ARG_NEW_STATE),
					wfd_arg_u(WFD_SESSION_STATE_SETTING_UP)
			),
		)
	},
	[RTSP_M7_PLAY]					= {
		.handle_request = wfd_out_session_handle_play_request,
	},
	[RTSP_M8_TEARDOWN]				= {
		.handle_request = wfd_out_session_handle_teardown_request,
		.rule = wfd_arg_list(
			wfd_arg_dict(
					wfd_arg_u(WFD_SESSION_ARG_NEW_STATE),
					wfd_arg_u(WFD_SESSION_STATE_TEARING_DOWN)
			),
		)
	},
	[RTSP_M9_PAUSE]					= {
		.handle_request = wfd_out_session_handle_pause_request,
	},
	[RTSP_M10_SET_ROUTE]			= {
		.handle_request = wfd_out_session_request_not_implement
	},
	[RTSP_M11_SET_CONNECTOR_TYPE]	= {
		.handle_request = wfd_out_session_request_not_implement
	},
	[RTSP_M12_SET_STANDBY]			= {
		.handle_request = wfd_out_session_request_not_implement
	},
	[RTSP_M13_REQUEST_IDR]			= {
		.handle_request = wfd_out_session_handle_idr_request,
	},
	[RTSP_M14_ESTABLISH_UIBC]		= {
	},
	[RTSP_M15_ENABLE_UIBC]			= {
		.handle_request = wfd_out_session_request_not_implement
	},
	[RTSP_M16_KEEPALIVE]			= {
		.request = wfd_out_session_request_keep_alive,
	},
};
