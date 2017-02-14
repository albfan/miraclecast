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

#include "rtsp.h"
#include "wfd-dbus.h"
#include "wfd-session.h"
#include "shl_macro.h"

#define wfd_message_id_valide(id) ( \
		(id) >= RTSP_M1_REQUEST_SINK_OPTIONS && \
		(id) <= RTSP_M16_KEEPALIVE \
)

static const char *rtsp_message_names[];
extern const struct wfd_session_vtable session_vtables[];
extern const struct rtsp_dispatch_entry out_session_rtsp_disp_tbl[];

static inline int wfd_session_do_request(struct wfd_session *s,
				enum rtsp_message_id id,
				struct rtsp_message **out)
{
	assert(wfd_message_id_valide(id));
	assert(s->rtsp_disp_tbl[id].request);

	return (*s->rtsp_disp_tbl[id].request)(s, out);
}

static inline int wfd_session_do_handle_request(struct wfd_session *s,
				enum rtsp_message_id id,
				struct rtsp_message *req,
				struct rtsp_message **out_rep)
{
	assert(wfd_message_id_valide(id));
	assert(s->rtsp_disp_tbl[id].handle_request);

	return (*s->rtsp_disp_tbl[id].handle_request)(s, req, out_rep);
}

static inline int wfd_session_do_handle_reply(struct wfd_session *s,
				enum rtsp_message_id id,
				struct rtsp_message *m)
{
	assert(wfd_message_id_valide(id));
	assert(s->rtsp_disp_tbl[id].handle_reply);

	return (*s->rtsp_disp_tbl[id].handle_reply)(s, m);
}

uint64_t wfd_session_get_id(struct wfd_session *s)
{
	return s->id;
}

enum wfd_session_state wfd_session_get_state(struct wfd_session *s)
{
	return s->state;
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

int wfd_session_is_started(struct wfd_session *s)
{
	assert(wfd_is_session(s));

	return 0 != s->id;
}

void wfd_session_end(struct wfd_session *s)
{
	assert(wfd_is_session(s));

	if(WFD_SESSION_STATE_NULL == s->state) {
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

	wfd_session_set_state(s, WFD_SESSION_STATE_NULL);

	if(wfd_is_out_session(s)) {
		wfd_fn_out_session_ended(s);
	}
}

void wfd_session_free(struct wfd_session *s)
{
	if(wfd_session_is_destructed(s)) {
		return;
	}

	s->destructed = true;

	wfd_session_end(s);

	if(session_vtables[s->dir].distruct) {
		(*session_vtables[s->dir].distruct)(s);
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

uint64_t * wfd_session_to_htable(struct wfd_session *s)
{
	return &s->id;
}

struct wfd_session * wfd_session_from_htable(uint64_t *e)
{
	return shl_htable_entry(e, struct wfd_session, id);
}

static int wfd_session_set_url(struct wfd_session *s, const char *addr)
{
	char *url;
	int r = asprintf(&url, "rtsp://%s/wfd1.0", addr);
	if(0 <= r) {
		free(s->url);
		s->url = url;
	}

	return r;
}

static enum rtsp_message_id wfd_session_message_to_id(struct wfd_session *s,
				struct rtsp_message *m)
{

	const char *method = m ? rtsp_message_get_method(m) : NULL;
	if(!method) {
		return RTSP_M_UNKNOWN;
	}

	if(!strcmp(method, "SET_PARAMETER")) {
		if(0 >= rtsp_message_read(m, "{<>}", "wfd_trigger_method")) {
			return RTSP_M5_TRIGGER;
		}

		if(0 >= rtsp_message_read(m, "{<>}", "wfd_route")) {
			return RTSP_M10_SET_ROUTE;
		}

		if(0 >= rtsp_message_read(m, "{<>}", "wfd_connector_type")) {
			return RTSP_M11_SET_CONNECTOR_TYPE;
		}

		if(0 >= rtsp_message_read(m, "{<>}", "wfd_standby")) {
			return RTSP_M12_SET_STANDBY;
		}

		if(0 >= rtsp_message_read(m, "{<>}", "wfd_idr_request")) {
			return RTSP_M13_REQUEST_IDR;
		}

		if(0 >= rtsp_message_read(m, "{<>}", "wfd_uibc_capability") && s->url) {
			return RTSP_M14_ESTABLISH_UIBC;
		}

		if(0 >= rtsp_message_read(m, "{<>}", "wfd_uibc_capability")) {
			return RTSP_M15_ENABLE_UIBC;
		}

		if(!s->url) {
			return RTSP_M4_SET_PARAMETER;
		}

		return RTSP_M_UNKNOWN;
	}

	if(!strcmp(method, "OPTIONS")) {
		return wfd_is_out_session(s)
						? (RTSP_MESSAGE_REPLY == rtsp_message_get_type(m))
							? RTSP_M1_REQUEST_SINK_OPTIONS
							: RTSP_M2_REQUEST_SRC_OPTIONS
						: (RTSP_MESSAGE_REPLY == rtsp_message_get_type(m))
							? RTSP_M2_REQUEST_SRC_OPTIONS
							: RTSP_M1_REQUEST_SINK_OPTIONS;
	}
	else if(!strcmp(method, "GET_PARAMETER")) {
		if(rtsp_message_get_body_size(m)) {
			return RTSP_M3_GET_PARAMETER;
		}

		return RTSP_M16_KEEPALIVE;
	}
	else if(!strcmp(method, "SETUP")) {
		return RTSP_M6_SETUP;
	}
	else if(!strcmp(method, "PLAY")) {
		return RTSP_M7_PLAY;
	}
	else if(!strcmp(method, "TEARDOWN")) {
		return RTSP_M8_TEARDOWN;
	}
	else if(!strcmp(method, "PAUSE")) {
		return RTSP_M9_PAUSE;
	}

	return RTSP_M_UNKNOWN;
}

static int wfd_session_dispatch_request(struct rtsp *bus,
				struct rtsp_message *m,
				void *userdata)
{
	_rtsp_message_unref_ struct rtsp_message *rep = NULL;
	struct wfd_session *s = userdata;
	enum rtsp_message_id id;
	int r;

	id = wfd_session_message_to_id(s, m);
	if(RTSP_M_UNKNOWN == id) {
		r = -EPROTO;
		goto end;
	}

	log_trace("received %s (M%d) request: %s", rtsp_message_id_to_string(id),
					id,
					(char *) rtsp_message_get_raw(m));

	r = wfd_session_do_handle_request(s, id, m, &rep);
	if(0 > r) {
		goto end;
	}

	r = rtsp_message_set_cookie(rep, id);
	if(0 > r) {
		goto end;
	}

	r = rtsp_message_seal(rep);
	if(0 > r) {
		goto end;
	}

	r = rtsp_send(bus, rep);
	if(0 > r) {
		goto end;
	}

	log_trace("sending %s (M%d) reply: %s", rtsp_message_id_to_string(id),
					id,
					(char *) rtsp_message_get_raw(rep));

end:
	if (0 > r) {
		wfd_session_end(s);
	}

	return r;

}

static int wfd_session_dispatch_reply(struct rtsp *bus,
				struct rtsp_message *m,
				void *userdata)
{
	int r;
	enum rtsp_message_id id;
	struct wfd_session *s = userdata;

	if(!m) {
		goto error;
	}

	if(!rtsp_message_is_reply(m, RTSP_CODE_OK, NULL)) {
		r = -EPROTO;
		goto error;
	}

	id = (enum rtsp_message_id) rtsp_message_get_cookie(m);

	log_trace("received %s (M%d) reply: %s",
					rtsp_message_id_to_string(id),
					id,
					(char *) rtsp_message_get_raw(m));

	r = wfd_session_do_handle_reply(s, id, m);
	if(0 > r) {
		goto error;
	}

	return 0;

error:
	wfd_session_end(s);
	return r;
}

int wfd_session_request(struct wfd_session *s, enum rtsp_message_id id)
{
	int r;
	_rtsp_message_unref_ struct rtsp_message *m = NULL;
	uint64_t cookie = id;

	assert(s);

	r = wfd_session_do_request(s, id, &m);
	if(0 > r) {
		return r;
	}

	r = rtsp_message_seal(m);
	if(0 > r) {
		return r;
	}

	r = rtsp_call_async(s->rtsp,
					m,
					wfd_session_dispatch_reply,
					s,
					0,
					&cookie);
	if(0 > r) {
		return r;
	}

	log_trace("sending %s (M%d) request: %s",
					rtsp_message_id_to_string(id),
					id,
					(char *) rtsp_message_get_raw(m));

	return 0;
}

static int wfd_session_handle_io(sd_event_source *source,
				int fd,
				uint32_t mask,
				void *userdata)
{
	int r, err = 0, conn;
	socklen_t len;
	struct wfd_session *s = userdata;
	_rtsp_unref_ struct rtsp *rtsp = NULL;

	sd_event_source_set_enabled(source, SD_EVENT_OFF);

	if (mask & EPOLLERR) {
		r = getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
		if(0 > r) {
			goto end;
		}
	}

	if (mask & EPOLLIN || mask & EPOLLOUT) {
		r = (*session_vtables[s->dir].handle_io)(s, err, &conn);
		if(0 > r) {
			goto end;
		}

		r = rtsp_open(&rtsp, conn);
		if (0 > r) {
			goto end;
		}

		conn = -1;

		r = rtsp_attach_event(rtsp, ctl_wfd_get_loop(), 0);
		if (0 > r) {
			goto end;
		}

		r = rtsp_add_match(rtsp, wfd_session_dispatch_request, s);
		if (0 > r) {
			goto end;
		}

		s->rtsp = rtsp;
		rtsp = NULL;

		wfd_session_set_state(s, WFD_SESSION_STATE_CAPS_EXCHAING);

		r = (*session_vtables[s->dir].initiate_request)(s);
	}

	if(mask & EPOLLHUP) {
		r = -ESHUTDOWN;
	}

end:
	if (0 > r) {
		wfd_session_end(s);
	}

	return r;
}

int wfd_session_start(struct wfd_session *s, uint64_t id)
{
	int r;
	_shl_close_ int fd = -1;
	uint32_t mask;

	assert(wfd_is_session(s));
	assert(id);

	if(wfd_session_is_started(s)) {
		return -EINPROGRESS;
	}

	r = (*session_vtables[s->dir].initiate_io)(s, &fd, &mask);
	if(0 > r) {
		return r;
	}

	r = sd_event_add_io(ctl_wfd_get_loop(),
				NULL,
				fd,
				mask,
				wfd_session_handle_io,
				s);
	if (r < 0) {
		return r;
	}

	fd = -1;

	s->id = id;
	wfd_session_set_state(s, WFD_SESSION_STATE_CONNECTING);

	return 0;
}

void wfd_session_freep(struct wfd_session **s)
{
	wfd_session_free(*s);
}

const char * rtsp_message_id_to_string(enum rtsp_message_id id)
{
	assert(id >= 0 && 16 >= id);

	return rtsp_message_names[id];
}

static const char *rtsp_message_names[] = {
	"UNKNOWN",
	"OPTIONS(src->sink)",
	"OPTIONS(sink->src)",
	"GET_PARAM",
	"SET_PARAM",
	"SET_PARAM(wfd-trigger-method)",
	"SETUP",
	"PLAY",
	"TEARDOWN",
	"PAUSE",
	"SET_PARAM(wfd-route)",
	"SET_PARAM(wfd-connector-type)",
	"SET_PARAM(wfd-standby)",
	"SET_PARAM(wfd-idr-request)",
	"SET_PARAM(wfd-uibc-cability)",
	"SET_PARAM(wfd-uibc-setting)",
	"GET_PARAM(keepalive)",
};

