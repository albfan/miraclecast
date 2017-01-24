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
#include <assert.h>
#include "ctl.h"

int wfd_sink_new(struct wfd_sink **out,
				struct ctl_peer *peer,
				union wfd_sube *sube)
{
	struct wfd_sink *sink;
	int r;

	assert(out);
	assert(peer);
	assert(sube && wfd_sube_device_is_sink(sube));
	
	sink = calloc(1, sizeof(struct wfd_sink));
	if(!sink) {
		return -ENOMEM;
	}

	sink->label = strdup(peer->label);
	if(!sink->label) {
		wfd_sink_free(sink);
		return -ENOMEM;
	}

	sink->peer = peer;
	sink->dev_info = *sube;

	*out = sink;

	return 0;
}

void wfd_sink_free(struct wfd_sink *sink)
{
	if(!sink) {
		return;
	}

	if(sink->session) {
		wfd_session_free(sink->session);
	}

	if(sink->label) {
		free(sink->label);
	}

	free(sink);
}

const char * wfd_sink_get_label(struct wfd_sink *sink)
{
	return sink->label;
}

const union wfd_sube * wfd_sink_get_dev_info(struct wfd_sink *sink)
{
	return &sink->dev_info;
}

struct ctl_peer * wfd_sink_get_peer(struct wfd_sink *sink)
{
	return sink->peer;
}

int wfd_sink_start_session(struct wfd_sink *sink, struct wfd_session **out)
{
	int r;
	struct wfd_session *session = NULL;

	assert(sink);
	assert(out);

	if(wfd_sink_is_session_started(sink)) {
		return -EALREADY;
	}

	r = wfd_out_session_new(&session, sink);
	if(0 > r) {
		return r;
	}

	r = wfd_session_start(session);
	if(0 > r) {
		goto free_session;
	}

	sink->session = session;
	*out = session;

	goto end;

free_session:
	wfd_session_free(session);
end:
	return r;
}

bool wfd_sink_is_session_started(struct wfd_sink *sink)
{
	return NULL != sink->session;
}
