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
#include <time.h>
#include "ctl.h"
#include "rtsp.h"
#include "wfd-dbus.h"
#include "wfd-session.h"
#include "shl_macro.h"
#include "shl_log.h"

#define rtsp_message_id_is_valid(_id) (				\
		(_id) >= RTSP_M1_REQUEST_SINK_OPTIONS &&	\
		(_id) <= RTSP_M16_KEEPALIVE					\
)
#define wfd_stream_id_is_valid(_id) (		\
		(_id) >= WFD_STREAM_ID_PRIMARY &&	\
		(_id) <= WFD_STREAM_ID_SECONDARY	\
)

static const char *rtsp_message_names[];
extern const struct rtsp_dispatch_entry out_session_rtsp_disp_tbl[];

extern int wfd_out_session_initiate_io(struct wfd_session *s, int *out_fd, uint32_t *out_mask);
extern int wfd_out_session_handle_io(struct wfd_session *s, int error, int *out_fd);
extern int wfd_out_session_initiate_request(struct wfd_session *);
extern int wfd_out_session_resume(struct wfd_session *);
extern int wfd_out_session_pause(struct wfd_session *);
extern int wfd_out_session_teardown(struct wfd_session *);
extern void wfd_out_session_end(struct wfd_session *);
extern void wfd_out_session_destroy(struct wfd_session *);
static const char * rtsp_message_id_to_string(enum rtsp_message_id id);
static int wfd_session_handle_request(struct rtsp *bus,
				struct rtsp_message *m,
				void *userdata);

const struct wfd_session_vtable session_vtbl[] = {
	[WFD_SESSION_DIR_OUT] = {
		.initiate_io		= wfd_out_session_initiate_io,
		.handle_io			= wfd_out_session_handle_io,
		.initiate_request	= wfd_out_session_initiate_request,
		.resume				= wfd_out_session_resume,
		.pause				= wfd_out_session_pause,
		.teardown			= wfd_out_session_teardown,
		.destroy			= wfd_out_session_destroy,
	}
};

static int wfd_session_do_request(struct wfd_session *s,
				enum rtsp_message_id id,
				const struct wfd_arg_list *args,
				struct rtsp_message **out)
{
	assert_ret(s);
	assert_ret(rtsp_message_id_is_valid(id));
	assert_ret(out);

	if(!s->rtsp_disp_tbl[id].request) {
		log_warning("!!! request %d not implemented !!!", id);
		return -ENOTSUP;
	}

	return (*s->rtsp_disp_tbl[id].request)(s, args, out);
}

static int wfd_session_do_handle_request(struct wfd_session *s,
				enum rtsp_message_id id,
				struct rtsp_message *req,
				struct rtsp_message **rep)
{
	assert_ret(s);
	assert_ret(rtsp_message_id_is_valid(id));
	assert_ret(req);
	assert_ret(rep);

	if(!rtsp_message_id_is_valid(id)) {
		return -EINVAL;
	}

	if(!s->rtsp_disp_tbl[id].handle_request) {
		log_warning("!!! request handler not implemented !!!");
		return -ENOTSUP;
	}

	return (*s->rtsp_disp_tbl[id].handle_request)(s,
					req,
					rep);
}

static int wfd_session_do_handle_reply(struct wfd_session *s,
				enum rtsp_message_id id,
				struct rtsp_message *rep)
{
	assert_ret(s);
	assert_ret(rtsp_message_id_is_valid(id));
	assert_ret(rep);

	if(!rtsp_message_id_is_valid(id)) {
		return -EINVAL;
	}
	
	if(!s->rtsp_disp_tbl[id].handle_reply) {
		return 0;
	}

	return (*s->rtsp_disp_tbl[id].handle_reply)(s, rep);
}

unsigned int wfd_session_get_id(struct wfd_session *s)
{
	assert_retv(s, (unsigned int) -1);

	return s->id;
}

enum wfd_session_state wfd_session_get_state(struct wfd_session *s)
{
	assert_retv(s, WFD_SESSION_STATE_NULL);

	return s->state;
}

void wfd_session_set_state(struct wfd_session *s,
				enum wfd_session_state state)
{
	assert_vret(wfd_is_session(s));

	if(state == s->state) {
		return;
	}

	s->state = state;

	wfd_fn_session_properties_changed(s, "State");
}

bool wfd_session_is_established(struct wfd_session *s)
{
	assert_retv(wfd_is_session(s), false);

	return WFD_SESSION_STATE_ESTABLISHED <= s->state;
}

int wfd_session_resume(struct wfd_session *s)
{
	assert_ret(wfd_is_session(s));

	if(WFD_SESSION_STATE_PLAYING == s->state) {
		return 0;
	}
	else if(WFD_SESSION_STATE_PAUSED != s->state) {
		return -EINVAL;
	}

	if(!session_vtbl[s->dir].resume) {
		return 0;
	}

	return session_vtbl[s->dir].resume(s);;
}

int wfd_session_pause(struct wfd_session *s)
{
	assert_ret(wfd_is_session(s));

	if(WFD_SESSION_STATE_PAUSED == s->state) {
		return 0;
	}
	else if(WFD_SESSION_STATE_PLAYING != s->state) {
		return -EINVAL;
	}

	if(!session_vtbl[s->dir].pause) {
		return 0;
	}

	return session_vtbl[s->dir].pause(s);;
}

int wfd_session_teardown(struct wfd_session *s)
{
	assert_ret(wfd_is_session(s));

	if(wfd_session_is_established(s)) {
		if(!session_vtbl[s->dir].teardown) {
			return 0;
		}

		return session_vtbl[s->dir].teardown(s);;
	}
	else {
		/* notify and detach from sink */
		wfd_session_terminate(s);
		wfd_fn_out_session_ended(s);
	}

	return 0;
}

int wfd_session_terminate(struct wfd_session *s)
{
	assert_ret(wfd_is_session(s));

	if(session_vtbl[s->dir].destroy) {
		(*session_vtbl[s->dir].destroy)(s);
	}

	if(s->rtsp) {
		rtsp_remove_match(s->rtsp, wfd_session_handle_request, s);
		rtsp_detach_event(s->rtsp);
		rtsp_unref(s->rtsp);
		s->rtsp = NULL;
	}

	if(s->vformats) {
		wfd_video_formats_free(s->vformats);
		s->vformats = NULL;
	}

	if(s->acodecs) {
		wfd_audio_codecs_free(s->acodecs);
		s->acodecs = NULL;
	}

	if(s->stream.url) {
		free(s->stream.url);
		s->stream.url = NULL;
	}

	if(s->disp_auth) {
		free(s->disp_auth);
		s->disp_auth = NULL;
	}

	if(s->disp_name) {
		free(s->disp_name);
		s->disp_name = NULL;
	}

	if(s->audio_dev_name) {
		free(s->audio_dev_name);
		s->audio_dev_name = NULL;
	}

	s->rtp_ports[0] = 0;
	s->rtp_ports[1] = 0;
	s->last_request = RTSP_M_UNKNOWN;

	s->state = WFD_SESSION_STATE_TERMINATING;

	return 0;
}

struct wfd_session * wfd_session_ref(struct wfd_session *s)
{
	if(s) {
		++ s->ref;
	}

	return s;
}

void wfd_session_unref(struct wfd_session *s)
{
	if(!s) {
		return;
	}

	assert_vret(1 <= s->ref);

	-- s->ref;
	if(s->ref) {
		return;
	}

	wfd_session_terminate(s);

	free(s);
}

enum wfd_session_dir wfd_session_get_dir(struct wfd_session *s)
{
	assert_retv(s, WFD_SESSION_DIR_OUT);

	return s->dir;
}

unsigned int * wfd_session_to_htable(struct wfd_session *s)
{
	assert_retv(s, NULL);

	return &s->id;
}

struct wfd_session * wfd_session_from_htable(unsigned int *e)
{
	assert_retv(e, NULL);

	return shl_htable_entry(e, struct wfd_session, id);
}

const char * wfd_session_get_stream_url(struct wfd_session *s)
{
	assert_retv(wfd_is_session(s), NULL);

	return s->stream.url;
}

int wfd_session_gen_stream_url(struct wfd_session *s,
				const char *local_addr,
				enum wfd_stream_id id)
{
	char *url;
	int r;

	assert_ret(wfd_is_session(s));
	assert_ret(local_addr);
	assert_ret(wfd_stream_id_is_valid(id));

	r = asprintf(&url, "rtsp://%s/wfd1.0/streamid=%d", local_addr, id);
	if(0 > r) {
		return log_ERRNO();
	}

	free(s->stream.url);
	s->stream.url = url;

	return 0;
}

static enum rtsp_message_id wfd_session_message_to_id(struct wfd_session *s,
				struct rtsp_message *m)
{

	const char *method;

	assert_retv(wfd_is_session(s), RTSP_M_UNKNOWN);
   
	method = m ? rtsp_message_get_method(m) : NULL;
	if(!method) {
		return RTSP_M_UNKNOWN;
	}

	if(!strcmp(method, "SET_PARAMETER")) {
		if(!rtsp_message_read(m, "{<>}", "wfd_trigger_method")) {
			return RTSP_M5_TRIGGER;
		}

		if(!rtsp_message_read(m, "{<>}", "wfd_route")) {
			return RTSP_M10_SET_ROUTE;
		}

		if(!rtsp_message_read(m, "{<>}", "wfd_connector_type")) {
			return RTSP_M11_SET_CONNECTOR_TYPE;
		}

		if(!rtsp_message_read(m, "{<>}", "wfd_uibc_setting")) {
			return RTSP_M15_ENABLE_UIBC;
		}

		if(!strncmp("wfd_standby", rtsp_message_get_body(m), 11)) {
			return RTSP_M12_SET_STANDBY;
		}

		if(!strncmp("wfd_idr_request", rtsp_message_get_body(m), 15)) {
			return RTSP_M13_REQUEST_IDR;
		}

		if(WFD_SESSION_STATE_CAPS_EXCHANGING == s->state) {
			return RTSP_M4_SET_PARAMETER;
		}

		if(!rtsp_message_read(m, "{<>}", "wfd_uibc_capability")) {
			return RTSP_M14_ESTABLISH_UIBC;
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

static int wfd_session_post_handle_request_n_reply(struct wfd_session *s,
                enum rtsp_message_id ror)
{
    const struct wfd_arg_list *args;
	enum rtsp_message_id next_request = RTSP_M_UNKNOWN;
	enum wfd_session_arg_id arg_id;
	enum wfd_session_state new_state = WFD_SESSION_STATE_NULL;
	const struct wfd_arg_list *req_args = NULL;
	int i;

	assert_ret(s);
	assert_ret(RTSP_M_UNKNOWN != ror);

	args = &s->rtsp_disp_tbl[ror].rule;
	if(!args->len) {
		return 0;
	}

	for(i = 0; i < args->len; i ++) {
		wfd_arg_list_get_dictk(args, i, &arg_id);
		switch(arg_id) {
			case WFD_SESSION_ARG_NEXT_REQUEST:
				wfd_arg_list_get_dictv(args, i, &next_request);
				break;
			case WFD_SESSION_ARG_NEW_STATE:
				wfd_arg_list_get_dictv(args, i, &new_state);
				wfd_session_set_state(s, new_state);
				break;
			case WFD_SESSION_ARG_REQUEST_ARGS:
				wfd_arg_list_get_dictv(args, i, &req_args);
			default:
				break;
		}
	}

	if(RTSP_M_UNKNOWN != next_request) {
		return wfd_session_request(s, next_request, req_args);
	}

	if(WFD_SESSION_STATE_TEARING_DOWN == new_state) {
		wfd_fn_out_session_ended(s);
	}
	
	return 0;
}

static int wfd_session_handle_request(struct rtsp *bus,
				struct rtsp_message *m,
				void *userdata)
{
	_rtsp_message_unref_ struct rtsp_message *rep = NULL;
	struct wfd_session *s = userdata;
	enum rtsp_message_id id;
	char date[64];
	uint64_t usec;
	time_t sec;
	int r;

	id = wfd_session_message_to_id(s, m);
	if(RTSP_M_UNKNOWN == id) {
		if(m) {
			log_debug("unable to map request to id: %s",
							(char *) rtsp_message_get_raw(m));
		}
		r = -EPROTO;
		goto error;
	}

	log_trace("received %s (M%d) request: %s", rtsp_message_id_to_string(id),
					id,
					(char *) rtsp_message_get_raw(m));

	r = wfd_session_do_handle_request(s,
					id,
					m,
					&rep);
	if(0 > r) {
		goto error;
	}

	r = sd_event_now(ctl_wfd_get_loop(), CLOCK_REALTIME, &usec);
	if(0 > r) {
		goto error;
	}

	sec = usec / 1000 / 1000;
	strftime(date, sizeof(date),
					"%a, %d %b %Y %T %z",
					gmtime(&sec));

	r = rtsp_message_append(rep, "<&>", "Date", date);
	if(0 > r) {
		goto error;
	}

	r = rtsp_message_seal(rep);
	if(0 > r) {
		goto error;
	}

	r = rtsp_send(bus, rep);
	if(0 > r) {
		goto error;
	}

	log_trace("sending %s (M%d) reply: %s", rtsp_message_id_to_string(id),
					id,
					(char *) rtsp_message_get_raw(rep));

	r = wfd_session_post_handle_request_n_reply(s, id);
	if(0 > r) {
		goto error;
	}

	return 0;

error:
	wfd_session_terminate(s);

	return log_ERR(r);
}

static int wfd_session_handle_reply(struct rtsp *bus,
				struct rtsp_message *m,
				void *userdata)
{
	int r;
	enum rtsp_message_id id;
	struct wfd_session *s = userdata;

	if(!m) {
		r = -EPIPE;
		goto error;
	}

	if(!rtsp_message_is_reply(m, RTSP_CODE_OK, NULL)) {
		r = -EPROTO;
		goto error;
	}

	id = s->last_request;

	log_trace("received %s (M%d) reply: %s",
					rtsp_message_id_to_string(id),
					id,
					(char *) rtsp_message_get_raw(m));

	r = wfd_session_do_handle_reply(s, id, m);
	if(0 > r) {
		goto error;
	}

	r = wfd_session_post_handle_request_n_reply(s, id);
	if(0 > r) {
		goto error;
	}

	return 0;

error:
	wfd_session_terminate(s);

	return log_ERR(r);
}

int wfd_session_init(struct wfd_session *s,
				unsigned int id,
				enum wfd_session_dir dir,
				const struct rtsp_dispatch_entry *disp_tbl)
{
	s->ref = 1;
	s->id = id;
	s->dir = dir;
	s->rtsp_disp_tbl = disp_tbl;

	return 0;
}

int wfd_session_request(struct wfd_session *s,
				enum rtsp_message_id id,
				const struct wfd_arg_list *args)
{
	int r;
	_rtsp_message_unref_ struct rtsp_message *m = NULL;

	assert_ret(s);

	r = wfd_session_do_request(s, id, args, &m);
	if(0 > r) {
		goto error;
	}

	r = rtsp_message_seal(m);
	if(0 > r) {
		goto error;
	}

	r = rtsp_call_async(s->rtsp,
					m,
					wfd_session_handle_reply,
					s,
					0,
					NULL);
	if(0 > r) {
		goto error;
	}

	s->last_request = id;

	log_trace("sending %s (M%d) request: %s",
					rtsp_message_id_to_string(id),
					id,
					(char *) rtsp_message_get_raw(m));

	return 0;

error:
	log_warning("error while requesting: %s", strerror(-r));
	return r;
}

static int wfd_session_handle_io(sd_event_source *source,
				int fd,
				uint32_t mask,
				void *userdata)
{
	int r = 0, err = 0, conn;
	socklen_t len;
	struct wfd_session *s = userdata;
	_rtsp_unref_ struct rtsp *rtsp = NULL;

	sd_event_source_set_enabled(source, SD_EVENT_OFF);
	sd_event_source_unref(source);

	if (mask & EPOLLERR) {
		r = getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len);
		if(0 > r) {
			return log_ERRNO();
		}
	}

	if (mask & EPOLLIN || mask & EPOLLOUT) {
		r = (*session_vtbl[s->dir].handle_io)(s, err, &conn);
		if(0 > r) {
			return log_ERRNO();
		}

		r = rtsp_open(&rtsp, conn);
		if (0 > r) {
			return log_ERRNO();
		}

		conn = -1;

		r = rtsp_attach_event(rtsp, ctl_wfd_get_loop(), 0);
		if (0 > r) {
			return log_ERRNO();
		}

		r = rtsp_add_match(rtsp, wfd_session_handle_request, s);
		if (0 > r) {
			return log_ERRNO();
		}

		s->rtsp = rtsp;
		rtsp = NULL;

		wfd_session_set_state(s, WFD_SESSION_STATE_CAPS_EXCHANGING);

		r = (*session_vtbl[s->dir].initiate_request)(s);
	}

	if(mask & EPOLLHUP) {
		wfd_session_teardown(s);
	}

	return 0;
}

int wfd_session_start(struct wfd_session *s)
{
	int r;
	_shl_close_ int fd = -1;
	uint32_t mask;

	assert_ret(wfd_is_session(s));

	if(WFD_SESSION_STATE_NULL != s->state) {
		return -EINPROGRESS;
	}

	r = (*session_vtbl[s->dir].initiate_io)(s, &fd, &mask);
	if(0 > r) {
		return log_ERRNO();
	}

	r = sd_event_add_io(ctl_wfd_get_loop(),
				NULL,
				fd,
				mask,
				wfd_session_handle_io,
				s);
	if (r < 0) {
		return log_ERRNO();
	}

	fd = -1;

	wfd_session_set_state(s, WFD_SESSION_STATE_CONNECTING);

	return 0;
}

enum wfd_display_server_type wfd_session_get_disp_type(struct wfd_session *s)
{
	return s->disp_type;
}

int wfd_session_set_disp_type(struct wfd_session *s, enum wfd_display_server_type disp_type)
{
	s->disp_type = disp_type;

	return 0;
}

const char * wfd_session_get_disp_name(struct wfd_session *s)
{
	return s->disp_name;
}

int wfd_session_set_disp_name(struct wfd_session *s, const char *disp_name)
{
	char *name = disp_name ? strdup(disp_name) : NULL;
	if(!name) {
		return -ENOMEM;
	}

	if(s->disp_name) {
		free(s->disp_name);
	}

	s->disp_name = name;

	return 0;
}

const char * wfd_session_get_disp_params(struct wfd_session *s)
{
	return s->disp_params;
}

int wfd_session_set_disp_params(struct wfd_session *s, const char *disp_params)
{
	char *params = disp_params ? strdup(disp_params) : NULL;
	if(disp_params && !params) {
		return -ENOMEM;
	}

	if(s->disp_params) {
		free(s->disp_params);
	}

	s->disp_params = params;

	return 0;
}

const char * wfd_session_get_disp_auth(struct wfd_session *s)
{
	return s->disp_auth;
}

int wfd_session_set_disp_auth(struct wfd_session *s, const char *disp_auth)
{
	char *auth = disp_auth ? strdup(disp_auth) : NULL;
	if(!auth) {
		return -ENOMEM;
	}

	if(s->disp_auth) {
		free(s->disp_auth);
	}

	s->disp_auth = auth;

	return 0;
}

const struct wfd_rectangle * wfd_session_get_disp_dimension(struct wfd_session *s)
{
	return &s->disp_dimen;
}

int wfd_session_set_disp_dimension(struct wfd_session *s, const struct wfd_rectangle *rect)
{
	assert_ret(rect);

	if(rect) {
		s->disp_dimen = *rect;
	}

	return 0;
}

enum wfd_audio_server_type wfd_session_get_audio_type(struct wfd_session *s)
{
	return s->audio_type;
}

int wfd_session_set_audio_type(struct wfd_session *s, enum wfd_audio_server_type audio_type)
{
	s->audio_type = audio_type;

	return 0;
}

const char * wfd_session_get_audio_dev_name(struct wfd_session *s)
{
	return s->audio_dev_name;
}

int wfd_session_set_audio_dev_name(struct wfd_session *s, char *audio_dev_name)
{
	char *name = audio_dev_name ? strdup(audio_dev_name) : NULL;
	if(!name) {
		return -ENOMEM;
	}

	if(s->audio_dev_name) {
		free(s->audio_dev_name);
	}

	s->audio_dev_name = name;

	return 0;
}

void wfd_session_unrefp(struct wfd_session **s)
{
	if(s) {
		wfd_session_unref(*s);
	}
}

static const char * rtsp_message_id_to_string(enum rtsp_message_id id)
{
	if(rtsp_message_id_is_valid(id)) {
		return rtsp_message_names[id];
	}

	return rtsp_message_names[0];
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

