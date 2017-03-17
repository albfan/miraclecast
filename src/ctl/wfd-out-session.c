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
#include <unistd.h>
#include <time.h>
#include <gst/gst.h>
#include <gst/base/base.h>
#include "wfd-session.h"
#include "shl_log.h"
#include "rtsp.h"

#define LOCAL_RTP_PORT		16384
#define LOCAL_RTCP_PORT		16385

enum wfd_display_type
{
	WFD_DISPLAY_TYPE_UNKNOWN,
	WFD_DISPLAY_TYPE_X,
};

struct wfd_out_session
{
	struct wfd_session parent;
	struct wfd_sink *sink;
	int fd;
	sd_event_source *gst_launch_source;
	sd_event_source *gst_term_source;

	enum wfd_display_type display_type;
	char *authority;
	char *display_name;
	const char *display_param_name;
	const char *display_param_value;
	uint16_t x;
	uint16_t y;
	uint16_t width;
	uint16_t height;
	enum wfd_resolution_standard std;
	uint32_t mask;
	char *audio_dev;

	GstElement *pipeline;
	GstBus *bus;
};

static const struct rtsp_dispatch_entry out_session_rtsp_disp_tbl[];

int wfd_out_session_new(struct wfd_session **out,
				struct wfd_sink *sink,
				const char *authority,
				const char *display,
				uint16_t x,
				uint16_t y,
				uint16_t width,
				uint16_t height,
				const char *audio_dev)
{
	_shl_free_ char *display_schema = NULL;
	_shl_free_ char *display_name = NULL;
	char *display_param;
	_wfd_session_free_ struct wfd_session *s;
	struct wfd_out_session *os;
	enum wfd_display_type display_type;
	enum wfd_resolution_standard std;
	uint32_t mask;
	int r;

	r = sscanf(display, "%m[^:]://%ms",
					&display_schema,
					&display_name);
	if(r != 2) {
		return -EINVAL;
	}

	if(!strcmp("x", display_schema)) {
		display_type = WFD_DISPLAY_TYPE_X;
	}
	else {
		return -EINVAL;
	}

	display_param = strchr(display_name, '?');
	if(display_param) {
		*display_param ++ = '\0';
	}

	if(!width || !height) {
		return -EINVAL;
	}

	r = vfd_get_mask_from_resolution(width, height, &std, &mask);
	if(0 > r) {
		return -EINVAL;
	}

	s = calloc(1, sizeof(struct wfd_out_session));
	if(!s) {
		return -ENOMEM;
	}

	os = wfd_out_session(s);
	os->authority = strdup(authority);
	if(!os->authority) {
		free(s);
		return -ENOMEM;
	}

	os->audio_dev = strdup(audio_dev);
	if(!os->audio_dev) {
		free(s);
		return -ENOMEM;
	}

	s->dir = WFD_SESSION_DIR_OUT;
	s->rtsp_disp_tbl = out_session_rtsp_disp_tbl;
	os->fd = -1;
	os->sink = sink;
	os->display_type = display_type;
	os->display_name = display_name;
	os->x = x;
	os->y = y;
	os->width = width;
	os->height = height;
	os->mask = mask;
	os->std = std;

	if(display_param) {
		os->display_param_name = display_param;
		display_param = strchr(display_param, '=');
		if(!display_param) {
			return -EINVAL;
		}
		*display_param ++ = '\0';
		os->display_param_value = display_param;
	}

	*out = wfd_session(s);
	s = NULL;
	display_name = NULL;

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
	struct wfd_out_session *os = wfd_out_session(s);
	if(0 <= os->fd) {
		close(os->fd);
		os->fd = -1;
	}

	if(os->gst_launch_source) {
		sd_event_source_unref(os->gst_launch_source);
		os->gst_launch_source = NULL;
	}

	if(os->audio_dev) {
		free(os->audio_dev);
		os->audio_dev = NULL;
	}

	if(os->display_name) {
		free(os->display_name);
		os->display_name = NULL;
	}

	if(os->authority) {
		free(os->authority);
		os->authority = NULL;
	}

	if(os->bus) {
		gst_bus_remove_watch(os->bus);
		g_object_unref(os->bus);
		os->bus = NULL;
	}

	if(os->pipeline) {
		gst_element_set_state(os->pipeline, GST_STATE_NULL);
		g_object_unref(os->pipeline);
		os->pipeline = NULL;
	}
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

	return 0;
}

static int wfd_out_session_request_get_parameter(struct wfd_session *s,
				const struct wfd_arg_list *args,
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

static int wfd_out_session_handle_options_reply(struct wfd_session *s,
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
				const struct wfd_arg_list *args,
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

static gboolean wfd_out_session_handle_gst_message(GstBus *bus,
				GstMessage *m,
				gpointer userdata)
{
	struct wfd_session *s = userdata;
	struct wfd_out_session *os = userdata;
	GstState old_state, new_state;

	switch(GST_MESSAGE_TYPE(m)) {
		case GST_MESSAGE_STATE_CHANGED:
			if(os->pipeline != (void *) GST_MESSAGE_SRC(m)) {
				break;
			}

			gst_message_parse_state_changed(m, &old_state, &new_state, NULL);
			if(GST_STATE_PLAYING == new_state) {
				log_info("stream is playing");
				wfd_session_set_state(s, WFD_SESSION_STATE_PLAYING);
			}
			else if(GST_STATE_PLAYING == old_state &&
							GST_STATE_PAUSED == new_state) {
				log_info("stream is paused");
				wfd_session_set_state(s, WFD_SESSION_STATE_PAUSED);
			}
			break;
		case GST_MESSAGE_EOS:
		case GST_MESSAGE_ERROR:
			log_warning("%s encounter unexpected error or EOS",
							GST_MESSAGE_SRC_NAME(m));
			wfd_session_teardown(s);
			break;
		default:
			break;
	}

	return TRUE;
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

static int wfd_out_session_create_pipeline(struct wfd_session *s)
{
	char rrtp_port[16], rrtcp_port[16], lrtcp_port[16];
	char audio_dev[256];
	char vsrc_param1[16] = "", vsrc_param2[16] = "";
	char vsrc_param3[16] = "", vsrc_param4[16] = "";
	struct wfd_out_session *os = wfd_out_session(s);
	GstElement *pipeline;
	GstElement *vsrc;
	GstBus *bus;
	GError *error = NULL;
	const char **tmp;
	int r;
	const char *pipeline_desc[128] = {
		"ximagesrc",
			"name=vsrc",
			"use-damage=false",
			"show-pointer=false",
			vsrc_param1,
			vsrc_param2,
			vsrc_param3,
			vsrc_param4,
		"!", "video/x-raw,",
			"framerate=30/1",
		"!", "vaapipostproc",
			"scale-method=2",	/* high quality scaling mode */
			"format=3",			/* yv12" */
		"!", "vaapih264enc",
			"rate-control=1",
			"num-slices=1",		/* in WFD spec, one slice per frame */
			"max-bframes=0",	/* in H264 CHP, no bframe supporting */
			"cabac=true",		/* in H264 CHP, CABAC entropy codeing is supported, but need more processing to decode */
			"dct8x8=true",		/* in H264 CHP, DTC is supported */
			"cpb-length=50",	/* shortent buffer in order to decrease latency */
			"keyframe-period=30",
			/*  "bitrate=62500", */	/* the max bitrate of H264 level 4.2, crashing my dongle, let codec decide */
		"!", "queue",
			"max-size-buffers=0",
			"max-size-bytes=0",
		"!", "mpegtsmux",
			"name=muxer",
		"!", "rtpmp2tpay",
		"!", ".send_rtp_sink_0", "rtpbin",
			"name=session",
			"do-retransmission=true",
			"do-sync-event=true",
			"do-lost=true",
			"ntp-time-source=3",
			"buffer-mode=0",
			"latency=20",
			"max-misorder-time=30",
		"!", "application/x-rtp",
		"!", "udpsink",
			"sync=false",
			"async=false",
			"host=", wfd_out_session_get_sink(s)->peer->remote_address,
			"port=", uint16_to_str(s->stream.rtp_port, rrtp_port,sizeof(rrtp_port)),
		"udpsrc",
			"address=", wfd_out_session_get_sink(s)->peer->local_address,
			"port=", uint16_to_str(LOCAL_RTCP_PORT, lrtcp_port,sizeof(lrtcp_port)),
			"reuse=true",
		"!", "session.recv_rtcp_sink_0",
		NULL
	};

	if(s->stream.rtcp_port) {
		for(tmp = pipeline_desc; *tmp; ++tmp);
		*tmp ++ = "session.send_rtcp_src_0";
		*tmp ++ = "!";
		*tmp ++ = "udpsink";
		*tmp ++ = "host=";
		*tmp ++ = wfd_out_session_get_sink(s)->peer->remote_address;
		*tmp ++ = "port=";
		*tmp ++ = uint16_to_str(s->stream.rtcp_port, rrtcp_port,sizeof(rrtcp_port));
		*tmp ++ = "sync=false";
		*tmp ++ = "async=false";
		*tmp ++ = NULL;
	}

	/*if(*os->audio_dev) {*/
		/*for(tmp = pipeline_desc; *tmp; ++tmp);*/
		/**tmp ++ = "pulsesrc";*/
		/**tmp ++ = "do-timestamp=true";*/
		/**tmp ++ = "client-name=miraclecast";*/
		/**tmp ++ = "device=";*/
		/**tmp ++ = quote_str(os->audio_dev, audio_dev, sizeof(audio_dev));*/
		/**tmp ++ = "!";*/
		/**tmp ++ = "audioconvert";*/
		/**tmp ++ = "!";*/
		/**tmp ++ = "audio/x-raw,";*/
		/**tmp ++ = "rate=48000,";*/
		/**tmp ++ = "channels=2";*/
		/**tmp ++ = "!";*/
		/**tmp ++ = "avenc_aac";*/
		/**tmp ++ = "!";*/
		/**tmp ++ = "queue";*/
		/*[>*tmp ++ = "max-size-buffers=0";<]*/
		/*[>*tmp ++ = "max-size-bytes=0";<]*/
		/**tmp ++ = "!";*/
		/**tmp ++ = "muxer.";*/
		/**tmp ++ = NULL;*/
	/*}*/

	/* bad pratice, but since we are in the same process,
	   I think this is the only way to do it */
	if(WFD_DISPLAY_TYPE_X == os->display_type) {
		r = setenv("XAUTHORITY", os->authority, 1);
		if(0 > r) {
			return r;
		}

		r = setenv("DISPLAY", os->display_name, 1);
		if(0 > r) {
			return r;
		}

		if(!os->display_param_name) {
			snprintf(vsrc_param1, sizeof(vsrc_param1), "startx=%hu", os->x);
			snprintf(vsrc_param2, sizeof(vsrc_param2), "starty=%hu", os->y);
			snprintf(vsrc_param3, sizeof(vsrc_param3), "endx=%d", os->width - 1);
			snprintf(vsrc_param4, sizeof(vsrc_param4), "endy=%d", os->height - 1);
		}
		else if(!strcmp("xid", os->display_param_name) ||
						!strcmp("xname", os->display_param_name)) {
			snprintf(vsrc_param1, sizeof(vsrc_param1),
					"%s=\"%s\"",
					os->display_param_name,
					os->display_param_value);
		}
	}

	pipeline = gst_parse_launchv(pipeline_desc, &error);
	if(!pipeline) {
		if(error) {
			log_error("failed to create pipeline: %s", error->message);
			g_error_free(error);
		}
		return -1;
	}

	vsrc = gst_bin_get_by_name(GST_BIN(pipeline), "vsrc");
	gst_base_src_set_live(GST_BASE_SRC(vsrc), true);
	g_object_unref(vsrc);
	vsrc = NULL;

	r = gst_element_set_state(pipeline, GST_STATE_PAUSED);
	if(GST_STATE_CHANGE_FAILURE == r) {
		g_object_unref(pipeline);
		return -1;
	}

	bus = gst_element_get_bus(pipeline);
	gst_bus_add_watch(bus, wfd_out_session_handle_gst_message, s);

	os->pipeline = pipeline;
	os->bus = bus;

	return 0;
}

static int wfd_out_session_handle_pause_request(struct wfd_session *s,
						struct rtsp_message *req,
						struct rtsp_message **out_rep)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r;

	r = gst_element_set_state(wfd_out_session(s)->pipeline, GST_STATE_READY);
	if(GST_STATE_CHANGE_FAILURE == r) {
		return -1;
	}

	r = rtsp_message_new_reply_for(req,
					&m,
					RTSP_CODE_OK,
					NULL);
	if(0 > r) {
		return r;
	}

	*out_rep = m;
	m = NULL;

	return 0;
}

static int wfd_out_session_handle_teardown_request(struct wfd_session *s,
						struct rtsp_message *req,
						struct rtsp_message **out_rep)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r;

	gst_element_set_state(wfd_out_session(s)->pipeline, GST_STATE_NULL);

	r = rtsp_message_new_reply_for(req,
					&m,
					RTSP_CODE_OK,
					NULL);
	if(0 > r) {
		return r;
	}

	*out_rep = m;
	m = NULL;

	return 0;
}

static int wfd_out_session_post_handle_play(sd_event_source *source,
				uint64_t t,
				void *userdata)
{
	struct wfd_session *s = userdata;
	GstStateChangeReturn r;

	sd_event_source_unref(source);
	wfd_out_session(s)->gst_launch_source = NULL;

	r = gst_element_set_state(wfd_out_session(s)->pipeline,
					GST_STATE_PLAYING);
	if(GST_STATE_CHANGE_FAILURE == r) {
		wfd_session_teardown(s);
		return -1;
	}

	return 0;
}

static int wfd_out_session_handle_play_request(struct wfd_session *s,
						struct rtsp_message *req,
						struct rtsp_message **out_rep)
{
	_shl_free_ char *v = NULL;
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	uint64_t now;
	int r;

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

	r = sd_event_now(ctl_wfd_get_loop(), CLOCK_MONOTONIC, &now);
	if(0 > r) {
		return r;
	}

	r = sd_event_add_time(ctl_wfd_get_loop(),
					&wfd_out_session(s)->gst_launch_source,
					CLOCK_MONOTONIC,
					100 * 1000 + now, 0,
					wfd_out_session_post_handle_play,
					s);
	if(0 <= r) {
		*out_rep = m;
		m = NULL;
	}

	return r;
}

static int wfd_out_session_handle_setup_request(struct wfd_session *s,
						struct rtsp_message *req,
						struct rtsp_message **out_rep)
{
	int r;
	char *l;
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
	s->stream.rtp_port = strtoul(l, &l, 10);
	if(errno) {
		return -errno;
	}

	if('-' == *l) {
		errno = 0;
		s->stream.rtcp_port = strtoul(l + 1, NULL, 10);
		if(errno) {
			return -errno;
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
		return r;
	}

	r = asprintf(&sess, "%X;timeout=30", s->id);
	if(0 > r) {
		return r;
	}

	r = rtsp_message_append(m, "<&>", "Session", sess);
	if(0 > r) {
		return r;
	}

	r = asprintf(&trans, "RTP/AVP/UDP;unicast;client_port=%hu%s;server_port=%u-%u",
					s->stream.rtp_port,
					l,
					LOCAL_RTP_PORT,
					LOCAL_RTCP_PORT);
	if(0 > r) {
		return r;
	}

	r = rtsp_message_append(m, "<&>", "Transport", trans);
	if(0 > r) {
		return r;
	}

	r = wfd_out_session_create_pipeline(s);
	if(0 > r) {
		return r;
	}

	*out_rep = m;
	m = NULL;

	return 0;
}

static int wfd_out_session_handle_idr_request(struct wfd_session *s,
				struct rtsp_message *req,
				struct rtsp_message **out_rep)
{
	return rtsp_message_new_reply_for(req,
					out_rep,
					RTSP_CODE_OK,
					NULL);
}

static int wfd_out_session_request_trigger(struct wfd_session *s,
				const struct wfd_arg_list *args,
				struct rtsp_message **out)
{
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	int r;
	const char *method;

	assert(args);

	wfd_arg_list_get(args, 0, &method);

	assert(method);

	r = rtsp_message_new_request(s->rtsp,
					 &m,
					 "SET_PARAMETER",
					 wfd_session_get_stream_url(s));
	if(0 > r) {
		return r;
	}

	r = rtsp_message_append(m, "{<s>}",
					"wfd_trigger_method",
					method);
	if(0 > r) {
		return r;
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
				const struct wfd_arg_list *args,
				struct rtsp_message **out)
{
	struct wfd_out_session *os = wfd_out_session(s);
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
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
					"wfd_video_formats: 00 00 02 10 %08X %08X %08X 00 0000 0000 00 none none\n"
					"wfd_audio_codecs: AAC 00000001 00\n"
					"wfd_presentation_URL: %s none\n"
					"wfd_client_rtp_ports: %u %u mode=play",
					//"wfd_uibc_capability: input_category_list=GENERIC\n;generic_cap_list=SingleTouch;hidc_cap_list=none;port=5100\n"
					//"wfd_uibc_setting: disable\n",
					WFD_RESOLUTION_STANDARD_CEA == os->std ? os->mask : 0,
					WFD_RESOLUTION_STANDARD_VESA == os->std ? os->mask: 0,
					WFD_RESOLUTION_STANDARD_HH == os->std ? os->mask : 0,
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
	},
};
