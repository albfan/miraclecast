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
#include <unistd.h>
#include "disp.h"
#include "wfd-arg.h"

#ifndef CTL_WFD_SESSION_H
#define CTL_WFD_SESSION_H

#define wfd_out_session(s)		(assert(wfd_is_out_session(s)), (struct wfd_out_session *) (s))
#define wfd_in_session(s)		(assert(wfd_is_in_session(s)), (struct wfd_in_session *) (s))

struct wfd_session;
struct wfd_sink;
struct rtsp;
struct rtsp_message;

enum rtsp_message_id
{
	RTSP_M_UNKNOWN,
	RTSP_M1_REQUEST_SINK_OPTIONS,
	RTSP_M2_REQUEST_SRC_OPTIONS,
	RTSP_M3_GET_PARAMETER,
	RTSP_M4_SET_PARAMETER,
	RTSP_M5_TRIGGER,
	RTSP_M6_SETUP,
	RTSP_M7_PLAY,
	RTSP_M8_TEARDOWN,
	RTSP_M9_PAUSE,
	RTSP_M10_SET_ROUTE,
	RTSP_M11_SET_CONNECTOR_TYPE,
	RTSP_M12_SET_STANDBY,
	RTSP_M13_REQUEST_IDR,
	RTSP_M14_ESTABLISH_UIBC,
	RTSP_M15_ENABLE_UIBC,
	RTSP_M16_KEEPALIVE,
};

enum wfd_stream_id
{
	WFD_STREAM_ID_PRIMARY,
	WFD_STREAM_ID_SECONDARY,
};

enum wfd_session_arg_id
{
	WFD_SESSION_ARG_NEXT_REQUEST,
	WFD_SESSION_ARG_NEW_STATE,
	WFD_SESSION_ARG_REQUEST_ARGS,
};

struct rtsp_dispatch_entry
{
	union {
		int (*request)(struct wfd_session *s,
						const struct wfd_arg_list *args,
						struct rtsp_message **out);
		int (*handle_request)(struct wfd_session *s,
						struct rtsp_message *req,
						struct rtsp_message **out_rep);
	};
	int (*handle_reply)(struct wfd_session *s,
					struct rtsp_message *m);
	struct wfd_arg_list rule;
};

struct wfd_session_vtable
{
	int (*initiate_io)(struct wfd_session *s, int *out_fd, uint32_t *out_mask);
	int (*handle_io)(struct wfd_session *s, int error, int *out_fd);
	int (*initiate_request)(struct wfd_session *s);
	int (*resume)(struct wfd_session *);
	int (*pause)(struct wfd_session *);
	int (*teardown)(struct wfd_session *);
	void (*destroy)(struct wfd_session *s);
};

struct wfd_session
{
	int ref_count;
	enum wfd_session_dir dir;
	enum wfd_session_state state;
	enum rtsp_message_id last_request;
	const struct rtsp_dispatch_entry *rtsp_disp_tbl;

	unsigned int id;
	struct rtsp *rtsp;
	uint16_t rtp_ports[2];
	struct wfd_video_formats *vformats;
	struct wfd_audio_codecs *acodecs;

	struct {
		enum wfd_stream_id id;
		char *url;
		uint16_t rtp_port;
		uint16_t rtcp_port;
	} stream;

	enum wfd_display_server_type disp_type;
	char *disp_name;
	char *disp_params;
	char *disp_auth;
	struct wfd_rectangle disp_dimen;

	enum wfd_audio_server_type audio_type;
	char *audio_dev_name;
};

int wfd_session_init(struct wfd_session *s,
				unsigned int id,
				enum wfd_session_dir dir,
				const struct rtsp_dispatch_entry *disp_tbl);
int wfd_session_gen_stream_url(struct wfd_session *s,
				const char *local_addr,
				enum wfd_stream_id id);
int wfd_session_request(struct wfd_session *s,
				enum rtsp_message_id id,
				const struct wfd_arg_list *args);
void wfd_session_end(struct wfd_session *s);
struct wfd_sink * wfd_out_session_get_sink(struct wfd_session *s);
void wfd_session_set_state(struct wfd_session *s,
				enum wfd_session_state state);

#endif /* CTL_WFD_SESSION_H */
