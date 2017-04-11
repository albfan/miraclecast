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

#include "wfd.h"
#include "shl_htable.h"
#include <systemd/sd-event.h>

#ifndef DISP_DISP_H
#define DISP_DISP_H

/* wfd session */
#define wfd_session(s)			((struct wfd_session *) (s))
#define wfd_is_session(s)		(								\
				(s) &&											\
				(WFD_SESSION_DIR_OUT == wfd_session(s)->dir ||	\
				WFD_SESSION_DIR_IN == wfd_session(s)->dir)		\
)
#define wfd_session_has_id(s)	(0 < wfd_session_get_id(s))
#define wfd_is_out_session(s)	(WFD_SESSION_DIR_OUT == wfd_session_get_dir(s))
#define wfd_is_in_session(s)	(WFD_SESSION_DIR_IN == wfd_session_get_dir(s))
#define _wfd_session_unref_ _shl_cleanup_(wfd_session_unrefp)

struct wfd_sink;
struct wfd_session;
struct rtsp_dispatch_entry;

enum wfd_session_dir
{
	WFD_SESSION_DIR_OUT,
	WFD_SESSION_DIR_IN,
};

enum wfd_session_state
{
	WFD_SESSION_STATE_NULL,
	WFD_SESSION_STATE_CONNECTING,
	WFD_SESSION_STATE_CAPS_EXCHANGING,
	WFD_SESSION_STATE_ESTABLISHED,
	WFD_SESSION_STATE_SETING_UP,
	WFD_SESSION_STATE_PAUSED,
	WFD_SESSION_STATE_PLAYING,
	WFD_SESSION_STATE_TEARING_DOWN,
};

int wfd_out_session_new(struct wfd_session **out,
				struct wfd_sink *sink,
				const char *authority,
				const char *display,
				uint32_t x,
				uint32_t y,
				uint32_t width,
				uint32_t height,
				const char *audio_dev);
int wfd_session_start(struct wfd_session *s, uint64_t id);
enum wfd_session_dir wfd_session_get_dir(struct wfd_session *s);
uint64_t wfd_session_get_id(struct wfd_session *s);
const char * wfd_session_get_stream_url(struct wfd_session *s);
enum wfd_session_state wfd_session_get_state(struct wfd_session *s);
int wfd_session_is_established(struct wfd_session *s);
int wfd_session_resume(struct wfd_session *s);
int wfd_session_pause(struct wfd_session *s);
int wfd_session_teardown(struct wfd_session *s);
struct wfd_session * wfd_session_ref(struct wfd_session *s);
void wfd_session_unref(struct wfd_session *s);
uint64_t wfd_session_get_id(struct wfd_session *s);
struct wfd_sink * wfd_out_session_get_sink(struct wfd_session *s);
void wfd_session_unrefp(struct wfd_session **s);
unsigned int * wfd_session_to_htable(struct wfd_session *s);
struct wfd_session * wfd_session_from_htable(unsigned int *e);

/* wfd sink */
#define _wfd_sink_free_ _shl_cleanup_(wfd_sink_freep)
#define wfd_sink_to_htable(s)		(&(s)->label)
#define wfd_sink_from_htable(s)		shl_htable_entry(s, struct wfd_sink, label)

struct wfd_sink
{
	struct ctl_peer *peer;
	union wfd_sube dev_info;
	char *label;
	struct wfd_session *session;

	sd_event_source *session_cleanup_source;
};

int wfd_sink_new(struct wfd_sink **out,
				struct ctl_peer *peer,
				union wfd_sube *sube);

void wfd_sink_free(struct wfd_sink *sink);

const char * wfd_sink_get_label(struct wfd_sink *sink);
const union wfd_sube * wfd_sink_get_dev_info(struct wfd_sink *sink);
int wfd_sink_start_session(struct wfd_sink *sink,
				struct wfd_session **out,
				const char *authority,
				const char *display,
				uint32_t x,
				uint32_t y,
				uint32_t width,
				uint32_t height,
				const char *audio_dev);
void wfd_sink_handle_session_ended(struct wfd_sink *sink);
bool wfd_sink_is_session_started(struct wfd_sink *sink);
static inline void wfd_sink_freep(struct wfd_sink **s)
{
	wfd_sink_free(*s);
	*s = NULL;
}

/* wfd handling */
#define ctl_wfd_foreach_sink(_i, _w) \
				SHL_HTABLE_FOREACH_MACRO(_i, \
								&(_w)->sinks, \
								wfd_sink_from_htable)
#define ctl_wfd_foreach_session(_i, _w) \
				SHL_HTABLE_FOREACH_MACRO(_i, \
								&(_w)->sessions, \
								wfd_session_from_htable)

struct ctl_wfd
{
	sd_event *loop;
	struct ctl_wifi *wifi;
	struct shl_htable sinks;
	size_t n_sinks;
	struct shl_htable sessions;
	size_t n_sessions;
	unsigned int id_pool;

	sd_event_source *signal_sources[4];
};

struct ctl_wfd * ctl_wfd_get();
int ctl_wfd_find_sink_by_label(struct ctl_wfd *wfd,
				const char *label,
				struct wfd_sink **out);
int ctl_wfd_add_session(struct ctl_wfd *wfd, struct wfd_session *s);
int ctl_wfd_find_session_by_id(struct ctl_wfd *wfd,
				unsigned int id,
				struct wfd_session **out);
int ctl_wfd_remove_session_by_id(struct ctl_wfd *wfd,
				unsigned int id,
				struct wfd_session **out);
unsigned int ctl_wfd_alloc_session_id(struct ctl_wfd *wfd);
static inline struct sd_event * ctl_wfd_get_loop()
{
	return ctl_wfd_get()->loop;
}

int wfd_fn_session_new(struct wfd_session *s);
int wfd_fn_session_free(struct wfd_session *s);
int wfd_fn_out_session_ended(struct wfd_session *s);

int wfd_fn_sink_new(struct wfd_sink *s);
int wfd_fn_sink_free(struct wfd_sink *s);

#endif /* DISP_DISP_H */
