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
#include "wfd-session.h"
#include "shl_log.h"
#include "rtsp.h"

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

static const struct rtsp_dispatch_entry out_session_rtsp_disp_tbl[];

int wfd_out_session_new(struct wfd_session **out, struct wfd_sink *sink)
{
	struct wfd_out_session *s = calloc(1, sizeof(struct wfd_out_session));
	if(!s) {
		return -ENOMEM;
	}

	wfd_session(s)->dir = WFD_SESSION_DIR_OUT;
	wfd_session(s)->rtsp_disp_tbl = out_session_rtsp_disp_tbl;
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

static int wfd_out_session_handle_io(struct wfd_session *s,
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

	//wfd_session_set_state(s, WFD_SESSION_STATE_CAPS_EXCHAING);

	close(os->fd);
	os->fd = -1;
	*out_fd = fd;
	fd = -1;

	return 0;
}

static int wfd_out_session_initiate_io(struct wfd_session *s,
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

	os->fd = fd;
	*out_fd = fd;
	fd = -1;
	*out_mask = EPOLLIN;

	return 0;
}

static void wfd_out_session_end(struct wfd_session *s)
{
	struct wfd_out_session *os = wfd_out_session(s);
	if(0 <= os->fd) {
		close(os->fd);
		os->fd = -1;
	}
}

static void wfd_out_session_distruct(struct wfd_session *s)
{
}

static int wfd_out_session_initiate_request(struct wfd_session *s)
{
	return wfd_session_request(s, RTSP_M1_REQUEST_SINK_OPTIONS);
}

/*static int wfd_out_session_handle_get_parameter_reply(struct rtsp *bus,*/
				/*struct rtsp_message *m,*/
				/*void *userdata)*/
/*{*/
	/*struct wfd_session *s = userdata;*/

	/*log_debug("received GET_PARAMETER reply (M3): %s\n",*/
					/*(char *) rtsp_message_get_raw(m));*/
	/*if(RTSP_CODE_OK != rtsp_message_get_code(m)) {*/
		/*wfd_session_end(wfd_session(userdata));*/
		/*return -EPROTO;*/
	/*}*/

	/*return 0;*/
/*}*/

/*static int wfd_out_session_send_get_parameter(sd_event_source *source,*/
				/*void *userdata)*/
/*{*/
	/*struct wfd_session *s = userdata;*/
	/*_rtsp_message_unref_ struct rtsp_message *m = NULL;*/
	/*int r = rtsp_message_new_request(s->rtsp,*/
					/*&m,*/
					/*"GET_PARAMETER",*/
					/*s->url);*/
	/*if (0 > r) {*/
		/*goto error;*/
	/*}*/

	/*r = rtsp_message_append(m, "{&}",*/
			/*"wfd_video_formats\n"*/
			/*"wfd_audio_codecs\n"*/
			/*"wfd_client_rtp_ports"*/
			/*//"wfd_uibc_capability"*/
	/*);*/
	/*if (0 > r) {*/
		/*goto error;*/
	/*}*/

	/*rtsp_message_seal(m);*/

	/*r = rtsp_call_async(s->rtsp,*/
					/*m,*/
					/*wfd_out_session_handle_get_parameter_reply,*/
					/*s,*/
					/*0,*/
					/*NULL);*/
	/*if (r < 0) {*/
		/*goto error;*/
	/*}*/

	/*log_debug("Sending GET_PARAMETER (M3): %s\n",*/
					/*(char *) rtsp_message_get_raw(m));*/

	/*return 0;*/

/*error:*/
	/*wfd_session_end(s);*/

	/*return r;*/
/*}*/

static bool find_strv(const char *str, char **strv)
{
	while(strv) {
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
		return r;
	}

	if(strcmp("org.wfa.wfd1.0", require)) {
		return rtsp_message_new_reply_for(req,
						out_rep, 
						RTSP_CODE_OPTION_NOT_SUPPORTED,
						"Invalid specification");
	}

	r = rtsp_message_new_reply_for(req,
					&rep,
					RTSP_CODE_OK,
					NULL);
	if(0 > r) {
		return r;
	}

	r = rtsp_message_append(rep,
					"<s>",
					"Public", "org.wfa.wfd1.0, SETUP, TEARDOWN, PLAY, PAUSE, GET_PARAMETER, SET_PARAMETER");
	if(0 > r) {
		return r;
	}

	*out_rep = rep;
	rep = NULL;

	return 0;
}

static int wfd_out_session_check_options_reply(struct wfd_session *s,
				struct rtsp_message *m)
{
	int r;
	const char *public;
	char *methods[4];

	r = rtsp_message_read(m, "<&>", "Public", &public);
	if(0 > r) {
		return r;
	}

	r = sscanf(public, "%m[^,], %m[^,], %ms", &methods[0], &methods[1], &methods[2]);
	if(3 != r) {
		return -EPROTO;
	}

	methods[3] = NULL;
	r = find_strv("org.wfa.wfd1.0", methods) &&
					find_strv("SET_PARAMETER", methods) &&
					find_strv("GET_PARAMETER", methods);
	free(methods[2]);
	free(methods[1]);
	free(methods[0]);
	if(!r) {
		return -EPROTO;
	}

	return 0;
}

static int wfd_out_session_request_options(struct wfd_session *s,
				struct rtsp_message **out)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r = rtsp_message_new_request(s->rtsp,
					&m,
					"OPTIONS", "*");
	if (0 > r) {
		return r;
	}

	r = rtsp_message_append(m,
					"<s>",
					"Require", "org.wfa.wfd1.0");
	if (0 > r) {
		return r;
	}

	*out = m;
	m = NULL;

	return 0;
}

/*static int wfd_out_session_handle_options_request(struct rtsp *bus,*/
				/*struct rtsp_message *request,*/
				/*struct wfd_session *s)*/
/*{*/
/*}*/

static int wfd_out_session_handle_play_reply(struct rtsp *bus,
				struct rtsp_message *m,
				void *userdata)
{
	/*struct wfd_session *s = userdata;*/
	/*log_trace("received SETUP (M5) reply: %s",*/
					/*(char *) rtsp_message_get_raw(m));*/

	/*wfd_out_session_send_play(s);*/

	return 0;
}

static int wfd_out_session_send_play(struct wfd_session *s)
{
	return 0;
}

static int wfd_out_session_handle_setup_reply(struct rtsp *bus,
				struct rtsp_message *m,
				void *userdata)
{
	/*struct wfd_session *s = userdata;*/
	/*log_trace("received SETUP (M5) reply: %s",*/
					/*(char *) rtsp_message_get_raw(m));*/

	/*wfd_out_session_send_play(s);*/

	return 0;
}

static int wfd_out_session_handle_set_parameter_reply(struct rtsp *bus,
				struct rtsp_message *m,
				void *userdata)
{
	/*struct wfd_session *s = userdata;*/
	/*log_trace("received SET_PARAMETER (M4) reply: %s",*/
					/*(char *) rtsp_message_get_raw(m));*/

	/*wfd_out_session_send_setup(s);*/

	return 0;
}

static int wfd_out_session_send_set_parameter_request(struct wfd_session *s)
{
	/*_rtsp_message_unref_ struct rtsp_message *req;*/
	int r;
	/*const static char tmp[] =*/
			/*"wfd_video_formats: 38 00 02 10 00000080 00000000 00000000 00 0000 0000 11 none none\n"*/
			/*//"wfd_audio_codecs: AAC 00000001 00\n"*/
			/*//"wfd_uibc_capability: input_category_list=GENERIC\n;generic_cap_list=SingleTouch;hidc_cap_list=none;port=5100\n"*/
			/*//"wfd_uibc_setting: disable\n"*/
			/*"wfd_presentation_URL: %s/streamid=0 none\n"*/
			/*"wfd_client_rtp_ports: %s %d %d mode=play";*/

	/*r = rtsp_message_new_request(s->rtsp,*/
					 /*&req,*/
					 /*"SET_PARAMETER",*/
					 /*s->url);*/
	/*if (r < 0) {*/
		/*cli_vERR(r);*/
		/*goto error;*/
	/*}*/

	/*snprintf(buf, sizeof(buf), tmp, s->url, s->sink.rtp_ports.profile,*/
			/*s->sink.rtp_ports.port0, s->sink.rtp_ports.port1);*/

	/*r = rtsp_message_append(req, "{&}", buf);*/
	/*if (r < 0) {*/
		/*cli_vERR(r);*/
		/*goto error;*/
	/*}*/

	/*rtsp_message_seal(req);*/
	/*cli_debug("OUTGOING (M4): %s\n", rtsp_message_get_raw(req));*/

	/*r = rtsp_call_async(s->rtsp, req, src_set_parameter_rep_fn, s, 0, NULL);*/
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

const struct wfd_session_vtable session_vtables[] = {
	[WFD_SESSION_DIR_OUT] = {
		.initiate_io		= wfd_out_session_initiate_io,
		.handle_io			= wfd_out_session_handle_io,
		.initiate_request	= wfd_out_session_initiate_request,
		.end				= wfd_out_session_end,
		.distruct			= wfd_out_session_distruct,
	}
};

static const struct rtsp_dispatch_entry out_session_rtsp_disp_tbl[] = {
	[RTSP_M1_REQUEST_SINK_OPTIONS]	= {
		.request = wfd_out_session_request_options,
		.handle_reply = wfd_out_session_check_options_reply
	},
	[RTSP_M2_REQUEST_SRC_OPTIONS]	= {
		.handle_request = wfd_out_session_handle_options_request
	},
	[RTSP_M3_GET_PARAMETER]			= {
	},
	[RTSP_M4_SET_PARAMETER]			= {
	},
	[RTSP_M5_TRIGGER]				= {
	},
	[RTSP_M6_SETUP]					= {
	},
	[RTSP_M7_PLAY]					= {
	},
	[RTSP_M8_TEARDOWN]				= {
	},
	[RTSP_M9_PAUSE]					= {
	},
	[RTSP_M10_SET_ROUTE]			= {
	},
	[RTSP_M11_SET_CONNECTOR_TYPE]	= {
	},
	[RTSP_M12_SET_STANDBY]			= {
	},
	[RTSP_M13_REQUEST_IDR]			= {
	},
	[RTSP_M14_ESTABLISH_UIBC]		= {
	},
	[RTSP_M15_ENABLE_UIBC]			= {
	},
	[RTSP_M16_KEEPALIVE]			= {
	},
};
