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

#include <sys/types.h>
#include <sys/wait.h>
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

	if(-1 != s->stream.gst) {
		kill(s->stream.gst, SIGTERM);
	}

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

static int wfd_out_session_handle_get_parameter_reply(struct wfd_session *s,
				struct rtsp_message *m,
				enum wfd_session_state *new_state,
				enum rtsp_message_id *next_request)
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
			return r;
		}

		if(s->vformats) {
			free(s->vformats);
		}
		s->vformats = vformats;
	}

	if(!rtsp_message_read(m, "{<&>}", "wfd_audio_codecs", &l)) {
		r = wfd_audio_codecs_from_string(l, &acodecs);
		if(0 > r) {
			return r;
		}
		
		if(s->acodecs) {
			free(s->acodecs);
		}
		s->acodecs = acodecs;
	}

	if(!rtsp_message_read(m, "{<&>}", "wfd_client_rtp_ports", &l)) {
		if(strncmp("RTP/AVP/UDP;unicast", l, 19)) {
			return -EPROTO;
		}

		r = sscanf(l + 20, "%hd %hd %ms",
						&rtp_ports[0],
						&rtp_ports[1],
						&t);
		if(3 != r) {
			return -EPROTO;
		}

		if(strncmp("mode=play", t, 9)) {
			return -EPROTO;
		}

		if(!rtp_ports[0] && !rtp_ports[1]) {
			return -EPROTO;
		}

		s->rtp_ports[0] = rtp_ports[0];
		s->rtp_ports[1] = rtp_ports[1];
	}

	*next_request = RTSP_M4_SET_PARAMETER;

	return 0;
}

static int wfd_out_session_request_get_parameter(struct wfd_session *s,
				struct rtsp_message **out)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r = rtsp_message_new_request(s->rtsp,
					&m,
					"GET_PARAMETER",
					"rtsp://localhost/wfd1.0");
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

	*out = m;
	m = NULL;

	return 0;

error:
	wfd_session_end(s);

	return r;
}

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
				struct rtsp_message **out_rep,
				enum wfd_session_state *new_state,
				enum rtsp_message_id *next_request)
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

	*next_request = RTSP_M3_GET_PARAMETER;

	return 0;
}

static int wfd_out_session_handle_options_reply(struct wfd_session *s,
					struct rtsp_message *m,
					enum wfd_session_state *new_state,
					enum rtsp_message_id *next_request)
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

static int wfd_out_session_launch_gst(struct wfd_session *s, pid_t *out)
{
	sigset_t sigset;
	char port[10];
	char * args[] = {
		"gst-launch-1.0",
		"-v",
		"ximagesrc",
			"use-damage=false",
			"show-pointer=false",
			"startx=0",
			"starty=0",
			"endx=1279",
			"endy=719",
		"!", "vaapipostproc",
		"!", "video/x-raw,",
			"format=YV12",
		"!", "vaapih264enc",
		/*"!", "video/x-h264,",*/
			/*"stream-format=byte-steram,",*/
			/*"profile=high",*/
		/*"!", "queue",*/
		"!", "mpegtsmux",
		"!", "rtpmp2tpay",
		"!", "udpsink",
			"host=", wfd_out_session_get_sink(s)->peer->remote_address,
			"port=", port,
		NULL
	};

	snprintf(port, sizeof(port), "%hu", s->stream.rtp_port);

	pid_t p = fork();
	if(0 > p) {
		return p;
	}
	else if(0 < p) {
		*out = p;
		return 0;
	}

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGTERM);
	sigprocmask(SIG_UNBLOCK, &sigset, NULL);

	execvpe(args[0],
					args,
					(char *[]) {
						"XDG_RUNTIME_DIR=/run/user/1000",
						"GST_DEBUG=3",
						"DISPLAY=:0",
						"XAUTHORITY=/run/user/1000/gdm/Xauthority",
						NULL
					});

	exit(1);
}

static int wfd_out_sessoin_handle_gst_term(sd_event_source *source,
				const siginfo_t *si,
				void *userdata)
{
	struct wfd_session *s = userdata;

	log_trace("gst-launch(%d) terminated", si->si_pid);

	if(WFD_SESSION_STATE_TEARING_DOWN != wfd_session_get_state(s)) {
		s->stream.gst = -1;
		wfd_session_end(s);
	}

	return 0;
}

static int wfd_out_session_handle_play_request(struct wfd_session *s,
						struct rtsp_message *req,
						struct rtsp_message **out_rep,
						enum wfd_session_state *new_state,
						enum rtsp_message_id *next_request)
{
	_shl_free_ char *v;
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	pid_t gst;
	int r, status;

	r = rtsp_message_new_reply_for(req,
					&m,
					RTSP_CODE_OK,
					NULL);
	if(0 > r) {
		return r;
	}

	r = asprintf(&v, "%d;timeout=30", s->stream.id);
	if(0 > r) {
		return r;
	}
	
	r = rtsp_message_append(m, "<s>", "Session", v);
	if(0 > r) {
		return r;
	}

	r = rtsp_message_append(m, "<s>", "Range", "ntp=now-");
	if(0 > r) {
		return r;
	}

	r = wfd_out_session_launch_gst(s, &gst);
	if(0 > r) {
		return r;
	}

	r = sd_event_add_child(ctl_wfd_get_loop(),
					NULL,
					gst, WEXITED,
					wfd_out_sessoin_handle_gst_term,
					s);
	if(0 > r) {
		kill(gst, SIGKILL);
		waitpid(gst, &status, WNOHANG);
		return r;
	}

	s->stream.gst = gst;

	*out_rep = m;
	m = NULL;

	*new_state = WFD_SESSION_STATE_PLAYING;

	return 0;
}

static int wfd_out_session_handle_setup_request(struct wfd_session *s,
						struct rtsp_message *req,
						struct rtsp_message **out_rep,
						enum wfd_session_state *new_state,
						enum rtsp_message_id *next_request)
{
	int r;
	const char *l;
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	_shl_free_ char *sess = NULL, *trans = NULL;

	r = rtsp_message_read(req, "<s>", "Transport", &l);
	if(0 > r) {
		return -EPROTO;
	}

	if(strncmp("RTP/AVP/UDP;unicast;", l, 20)) {
		return -EPROTO;
	}

	l += 20;

	if(strncmp("client_port=", l, 12)) {
		return -EPROTO;
	}

	l += 12;

	errno = 0;
	s->stream.rtp_port = strtoul(l, NULL, 10);
	if(errno) {
		return -errno;
	}

	r = rtsp_message_new_reply_for(req,
					&m,
					RTSP_CODE_OK,
					NULL);
	if(0 > r) {
		return r;
	}

	r = asprintf(&sess, "%" PRIu64 ";timeout=30", s->id);
	if(0 > r) {
		return r;
	}

	r = rtsp_message_append(m, "<&>", "Session", sess);
	if(0 > r) {
		return r;
	}

	r = asprintf(&trans, "RTP/AVP/UDP;unicast;client_port=%hd",
					s->stream.rtp_port);
	if(0 > r) {
		return r;
	}

	r = rtsp_message_append(m, "<&>", "Transport", trans);
	if(0 > r) {
		return r;
	}

	*out_rep = m;
	m = NULL;

	return 0;
}

static int wfd_out_session_request_trigger(struct wfd_session *s,
				struct rtsp_message **out)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r;

	switch(wfd_session_get_state(s)) {
		case WFD_SESSION_STATE_ESTABLISHED:
			r = rtsp_message_new_request(s->rtsp,
							 &m,
							 "SET_PARAMETER",
							 wfd_session_get_stream_url(s));
			if(0 > r) {
				return r;
			}

			r = rtsp_message_append(m, "{<s>}",
							"wfd_trigger_method",
							"SETUP");
			if(0 > r) {
				return r;
			}
			break;
		default:
			break;
	}

	if(m) {
		*out = m;
		m = NULL;
	}

	return 0;
}

static int wfd_out_session_handle_set_parameter_reply(struct wfd_session *s,
				struct rtsp_message *m,
				enum wfd_session_state *new_state,
				enum rtsp_message_id *next_request)
{
	*new_state = WFD_SESSION_STATE_ESTABLISHED;
	*next_request = RTSP_M5_TRIGGER;

	return 0;
}

static int wfd_out_session_request_set_parameter(struct wfd_session *s,
				struct rtsp_message **out)
{
	_rtsp_message_unref_ struct rtsp_message *m;
	_shl_free_ char *body = NULL;
	int r;

	r = wfd_session_gen_stream_url(s,
					wfd_out_session(s)->sink->peer->local_address,
					WFD_STREAM_ID_PRIMARY);
	if(0 > r) {
		return r;
	}

	s->stream.id = WFD_STREAM_ID_PRIMARY;

	r = asprintf(&body,
					"wfd_video_formats: 00 00 02 10 00001401 00000000 00000000 00 0000 0000 00 none none\n"
					//"wfd_audio_codecs: AAC 00000001 00\n"
					"wfd_presentation_URL: %s none\n"
					"wfd_client_rtp_ports: %u %u mode=play",
					//"wfd_uibc_capability: input_category_list=GENERIC\n;generic_cap_list=SingleTouch;hidc_cap_list=none;port=5100\n"
					//"wfd_uibc_setting: disable\n",
					wfd_session_get_stream_url(s),
					s->rtp_ports[0],
					s->rtp_ports[1]);
	if(0 > r) {
		return r;
	}

	r = rtsp_message_new_request(s->rtsp,
					&m,
					"SET_PARAMETER",
					"rtsp://localhost/wfd1.0");
	if (0 > r) {
		return r;
	}

	r = rtsp_message_append(m, "{&}", body);
	if (0 > r) {
		return r;
	}

	*out = m;
	m = NULL;

	return 0;
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
		.handle_reply = wfd_out_session_handle_options_reply
	},
	[RTSP_M2_REQUEST_SRC_OPTIONS]	= {
		.handle_request = wfd_out_session_handle_options_request
	},
	[RTSP_M3_GET_PARAMETER]			= {
		.request = wfd_out_session_request_get_parameter,
		.handle_reply = wfd_out_session_handle_get_parameter_reply
	},
	[RTSP_M4_SET_PARAMETER]			= {
		.request = wfd_out_session_request_set_parameter,
		.handle_reply = wfd_out_session_handle_set_parameter_reply
	},
	[RTSP_M5_TRIGGER]				= {
		.request = wfd_out_session_request_trigger,
	},
	[RTSP_M6_SETUP]					= {
		.handle_request = wfd_out_session_handle_setup_request,
	},
	[RTSP_M7_PLAY]					= {
		.handle_request = wfd_out_session_handle_play_request,
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
