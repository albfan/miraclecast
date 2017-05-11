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
#include "dispd.h"
#include "dispd-arg.h"

#ifndef DISPD_SESSION_H
#define DISPD_SESSION_H

#define dispd_out_session(s)		(assert(dispd_is_out_session(s)), (struct dispd_out_session *) (s))
#define dispd_in_session(s)		(assert(dispd_is_in_session(s)), (struct dispd_in_session *) (s))

struct dispd_session;
struct dispd_sink;
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

enum dispd_stream_id
{
	DISPD_STREAM_ID_PRIMARY,
	DISPD_STREAM_ID_SECONDARY,
};

enum dispd_session_arg_id
{
	DISPD_SESSION_ARG_NEXT_REQUEST,
	DISPD_SESSION_ARG_NEW_STATE,
	DISPD_SESSION_ARG_REQUEST_ARGS,
};

struct rtsp_dispatch_entry
{
	union {
		int (*request)(struct dispd_session *s,
						struct rtsp *bus,
						const struct dispd_arg_list *args,
						struct rtsp_message **out);
		int (*handle_request)(struct dispd_session *s,
						struct rtsp_message *req,
						struct rtsp_message **out_rep);
	};
	int (*handle_reply)(struct dispd_session *s,
					struct rtsp_message *m);
	struct dispd_arg_list rule;
};

struct dispd_session_vtable
{
	int (*initiate_io)(struct dispd_session *s, int *out_fd, uint32_t *out_mask);
	int (*handle_io)(struct dispd_session *s, int error, int *out_fd);
	int (*initiate_request)(struct dispd_session *s);
	int (*resume)(struct dispd_session *);
	int (*pause)(struct dispd_session *);
	int (*teardown)(struct dispd_session *);
	void (*destroy)(struct dispd_session *s);
};

struct dispd_session
{
	int ref;
	enum dispd_session_dir dir;
	enum dispd_session_state state;
	enum rtsp_message_id last_request;
	const struct rtsp_dispatch_entry *rtsp_disp_tbl;

	unsigned int id;
	struct rtsp *rtsp;
	uint64_t req_cookie;
	uint16_t rtp_ports[2];
	struct wfd_video_formats *vformats;
	struct wfd_audio_codecs *acodecs;

	struct {
		enum dispd_stream_id id;
		char *url;
		uint16_t rtp_port;
		uint16_t rtcp_port;
	} stream;

	enum dispd_display_server_type disp_type;
	char *disp_name;
	char *disp_params;
	char *disp_auth;
	struct dispd_rectangle disp_dimen;
	enum dispd_audio_server_type audio_type;
	char *audio_dev_name;

	uid_t client_uid;
	gid_t client_gid;
	gid_t client_pid;
	char *runtime_path;
};

int dispd_session_init(struct dispd_session *s,
				unsigned int id,
				enum dispd_session_dir dir,
				const struct rtsp_dispatch_entry *disp_tbl);
int dispd_session_gen_stream_url(struct dispd_session *s,
				const char *local_addr,
				enum dispd_stream_id id);
int dispd_session_request(struct dispd_session *s,
				enum rtsp_message_id id,
				const struct dispd_arg_list *args);
void dispd_session_end(struct dispd_session *s);
struct dispd_sink * dispd_out_session_get_sink(struct dispd_session *s);
void dispd_session_set_state(struct dispd_session *s,
				enum dispd_session_state state);

#endif /* DISPD_SESSION_H */
