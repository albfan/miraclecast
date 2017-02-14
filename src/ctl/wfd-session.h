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
#ifndef MIRACLE_OUT_SESSION_H
#define MIRACLE_OUT_SESSION_H

#include "ctl.h"

#define wfd_out_session(s)		(assert(wfd_is_out_session(s)), (struct wfd_out_session *) (s))
#define wfd_in_session(s)		(assert(wfd_is_in_session(s)), (struct wfd_in_session *) (s))
#define wfd_session_is_destructed(s)	(!(s) || (s)->destructed)

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

struct rtsp_dispatch_entry
{
	union {
		int (*request)(struct wfd_session *s, struct rtsp_message **out);
		int (*handle_request)(struct wfd_session *s,
						struct rtsp_message *req,
						struct rtsp_message **out_rep);
	};
	int (*handle_reply)(struct wfd_session *s, struct rtsp_message *m);
	enum rtsp_message_id next_request;
};

struct wfd_session_vtable
{
	int (*initiate_io)(struct wfd_session *s, int *out_fd, uint32_t *out_mask);
	int (*handle_io)(struct wfd_session *s, int error, int *out_fd);
	int (*initiate_request)(struct wfd_session *s);
	void (*end)(struct wfd_session *s);
	void (*distruct)(struct wfd_session *s);
};

struct wfd_session
{
	enum wfd_session_dir dir;
	enum wfd_session_state state;
	uint64_t id;
	char *url;
	struct rtsp *rtsp;
	const struct rtsp_dispatch_entry *rtsp_disp_tbl;

	bool destructed: 1;
};

const char * rtsp_message_id_to_string(enum rtsp_message_id id);
struct wfd_sink * wfd_out_session_get_sink(struct wfd_session *s);
int wfd_session_request(struct wfd_session *s, enum rtsp_message_id id);

#endif /* MIRACLE_OUT_SESSION_H */
