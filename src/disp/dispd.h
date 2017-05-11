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

#include <systemd/sd-event.h>
#include "shl_htable.h"
#include "ctl.h"
#include "wfd.h"

#ifndef DISP_DISP_H
#define DISP_DISP_H

/* dispd session */
#define dispd_session(s)			((struct dispd_session *) (s))
#define dispd_is_session(s)		(								\
				(s) &&											\
				(DISPD_SESSION_DIR_OUT == dispd_session(s)->dir ||	\
				DISPD_SESSION_DIR_IN == dispd_session(s)->dir)		\
)
#define dispd_session_has_id(s)	(0 < dispd_session_get_id(s))
#define dispd_is_out_session(s)	(DISPD_SESSION_DIR_OUT == dispd_session_get_dir(s))
#define dispd_is_in_session(s)	(DISPD_SESSION_DIR_IN == dispd_session_get_dir(s))
#define _dispd_session_unref_ _shl_cleanup_(dispd_session_unrefp)

struct dispd_sink;
struct dispd_session;
struct rtsp_dispatch_entry;

enum dispd_session_dir
{
	DISPD_SESSION_DIR_OUT,
	DISPD_SESSION_DIR_IN,
};

enum dispd_session_state
{
	DISPD_SESSION_STATE_NULL,
	DISPD_SESSION_STATE_CONNECTING,
	DISPD_SESSION_STATE_CAPS_EXCHANGING,
	DISPD_SESSION_STATE_ESTABLISHED,
	DISPD_SESSION_STATE_SETTING_UP,
	DISPD_SESSION_STATE_PAUSED,
	DISPD_SESSION_STATE_PLAYING,
	DISPD_SESSION_STATE_TEARING_DOWN,
	DISPD_SESSION_STATE_DESTROYED,
};

struct dispd_rectangle
{
	int x;
	int y;
	int width;
	int height;
};

enum dispd_display_server_type
{
	DISPD_DISPLAY_SERVER_TYPE_UNKNOWN = 0,
	DISPD_DISPLAY_SERVER_TYPE_X,
};

enum dispd_audio_server_type
{
	DISPD_AUDIO_SERVER_TYPE_UNKNOWN = 0,
	DISPD_AUDIO_SERVER_TYPE_PULSE_AUDIO,
};

int dispd_out_session_new(struct dispd_session **out,
				unsigned int id,
				struct dispd_sink *sink);
struct dispd_session * _dispd_session_ref(struct dispd_session *s);
#define dispd_session_ref(s) ( \
	log_debug("dispd_session_ref(%p): %d => %d", (s), *(int *) s, 1 + *(int *) s), \
	_dispd_session_ref(s) \
)
void _dispd_session_unref(struct dispd_session *s);
#define dispd_session_unref(s) { \
	log_debug("dispd_session_unref(%p): %d => %d", (s), *(int *) s, *(int *) s - 1); \
	_dispd_session_unref(s); \
}

void dispd_session_unrefp(struct dispd_session **s);
unsigned int * dispd_session_to_htable(struct dispd_session *s);
struct dispd_session * dispd_session_from_htable(unsigned int *e);

int dispd_session_start(struct dispd_session *s);
int dispd_session_resume(struct dispd_session *s);
int dispd_session_pause(struct dispd_session *s);
int dispd_session_teardown(struct dispd_session *s);
int dispd_session_destroy(struct dispd_session *s);

bool dispd_session_is_established(struct dispd_session *s);
unsigned int dispd_session_get_id(struct dispd_session *s);
const char * dispd_session_get_stream_url(struct dispd_session *s);
bool dispd_session_is_state(struct dispd_session *s, enum dispd_session_state state);
enum dispd_session_state dispd_session_get_state(struct dispd_session *s);
enum dispd_session_dir dispd_session_get_dir(struct dispd_session *s);
struct dispd_sink * dispd_out_session_get_sink(struct dispd_session *s);

enum dispd_display_server_type dispd_session_get_disp_type(struct dispd_session *s);
int dispd_session_set_disp_type(struct dispd_session *s, enum dispd_display_server_type);
const char * dispd_session_get_disp_name(struct dispd_session *s);
int dispd_session_set_disp_name(struct dispd_session *s, const char *disp_name);
const char * dispd_session_get_disp_params(struct dispd_session *s);
int dispd_session_set_disp_params(struct dispd_session *s, const char *disp_params);
const char * dispd_session_get_disp_auth(struct dispd_session *s);
int dispd_session_set_disp_auth(struct dispd_session *s, const char *disp_auth);
const struct dispd_rectangle * dispd_session_get_disp_dimension(struct dispd_session *s);
int dispd_session_set_disp_dimension(struct dispd_session *s, const struct dispd_rectangle *rect);
enum dispd_audio_server_type dispd_session_get_audio_type(struct dispd_session *s);
int dispd_session_set_audio_type(struct dispd_session *s, enum dispd_audio_server_type audio_type);
const char * dispd_session_get_audio_dev_name(struct dispd_session *s);
int dispd_session_set_audio_dev_name(struct dispd_session *s, const char *audio_dev_name);
const char * dispd_session_get_runtime_path(struct dispd_session *s);
int dispd_session_set_runtime_path(struct dispd_session *s,
				const char *runtime_path);
uid_t dispd_session_get_client_uid(struct dispd_session *s);
int dispd_session_set_client_uid(struct dispd_session *s, uid_t uid);
uid_t dispd_session_get_client_gid(struct dispd_session *s);
int dispd_session_set_client_gid(struct dispd_session *s, uid_t gid);
pid_t dispd_session_get_client_pid(struct dispd_session *s);
int dispd_session_set_client_pid(struct dispd_session *s, pid_t pid);

/* dispd sink */
#define _dispd_sink_free_ _shl_cleanup_(dispd_sink_freep)
#define dispd_sink_to_htable(s)		(&(s)->label)
#define dispd_sink_from_htable(s)		shl_htable_entry(s, struct dispd_sink, label)

struct dispd_sink
{
	struct ctl_peer *peer;
	union wfd_sube dev_info;
	char *label;
	struct dispd_session *session;

	sd_event_source *session_cleanup_source;
};

int dispd_sink_new(struct dispd_sink **out,
				struct ctl_peer *peer,
				union wfd_sube *sube);
void dispd_sink_free(struct dispd_sink *sink);
static inline void dispd_sink_freep(struct dispd_sink **s)
{
	dispd_sink_free(*s);
	*s = NULL;
}

int dispd_sink_create_session(struct dispd_sink *sink, struct dispd_session **out);

const char * dispd_sink_get_label(struct dispd_sink *sink);
const union wfd_sube * dispd_sink_get_dev_info(struct dispd_sink *sink);
bool dispd_sink_is_session_started(struct dispd_sink *sink);

void dispd_sink_handle_session_ended(struct dispd_sink *sink);

/* wfd handling */
#define dispd_foreach_sink(_i, _w) \
				SHL_HTABLE_FOREACH_MACRO(_i, \
								&(_w)->sinks, \
								dispd_sink_from_htable)
#define dispd_foreach_session(_i, _w) \
				SHL_HTABLE_FOREACH_MACRO(_i, \
								&(_w)->sessions, \
								dispd_session_from_htable)

struct dispd
{
	sd_event *loop;
	struct ctl_wifi *wifi;
	struct shl_htable sinks;
	size_t n_sinks;
	struct shl_htable sessions;
	size_t n_sessions;
	unsigned int id_pool;
};

struct dispd * dispd_get();
void dispd_shutdown(struct dispd *wfd);

static inline struct sd_event * dispd_get_loop()
{
	return dispd_get()->loop;
}

int dispd_find_sink_by_label(struct dispd *wfd,
				const char *label,
				struct dispd_sink **out);
int dispd_add_session(struct dispd *wfd, struct dispd_session *s);
int dispd_find_session_by_id(struct dispd *wfd,
				unsigned int id,
				struct dispd_session **out);
int dispd_remove_session_by_id(struct dispd *wfd,
				unsigned int id,
				struct dispd_session **out);
unsigned int dispd_alloc_session_id(struct dispd *wfd);

int dispd_fn_session_new(struct dispd_session *s);
int dispd_fn_session_free(struct dispd_session *s);
int dispd_fn_out_session_ended(struct dispd_session *s);

int dispd_fn_sink_new(struct dispd_sink *s);
int dispd_fn_sink_free(struct dispd_sink *s);

#endif /* DISP_DISP_H */
