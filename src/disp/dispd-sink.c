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
#include <systemd/sd-event.h>
#include "ctl.h"
#include "dispd.h"
#include "dispd-dbus.h"

static int dispd_sink_set_session(struct dispd_sink *sink,
				struct dispd_session *session)
{
	int r;

	assert_ret(sink);

	if(sink->session == session) {
		return 0;
	}

	if(session) {
		r = dispd_add_session(dispd_get(), session);
		if(0 > r) {
			return r;
		}
	}
	
	if(sink->session) {
		dispd_remove_session_by_id(dispd_get(),
						dispd_session_get_id(sink->session),
						NULL);
		dispd_session_unref(sink->session);
	}

	sink->session = session ? dispd_session_ref(session) : NULL;
	dispd_fn_sink_properties_changed(sink, "Session");

	return 0;
}

int dispd_sink_new(struct dispd_sink **out,
				struct ctl_peer *peer,
				union wfd_sube *sube)
{
	struct dispd_sink *sink;

	assert_ret(out);
	assert_ret(peer);
	assert_ret(sube);
	assert_ret(wfd_sube_device_is_sink(sube));
	
	sink = calloc(1, sizeof(struct dispd_sink));
	if(!sink) {
		return -ENOMEM;
	}

	sink->label = strdup(peer->label);
	if(!sink->label) {
		dispd_sink_free(sink);
		return -ENOMEM;
	}

	sink->peer = peer;
	sink->dev_info = *sube;

	*out = sink;

	return 0;
}

void dispd_sink_free(struct dispd_sink *sink)
{
	if(!sink) {
		return;
	}

	dispd_sink_set_session(sink, NULL);

	if(sink->label) {
		free(sink->label);
	}

	free(sink);
}

const char * dispd_sink_get_label(struct dispd_sink *sink)
{
	assert_retv(sink, NULL);

	return sink->label;
}

const union wfd_sube * dispd_sink_get_dev_info(struct dispd_sink *sink)
{
	assert_retv(sink, NULL);

	return &sink->dev_info;
}

struct ctl_peer * dispd_sink_get_peer(struct dispd_sink *sink)
{
	assert_retv(sink, NULL);

	return sink->peer;
}

int dispd_sink_create_session(struct dispd_sink *sink, struct dispd_session **out)
{
	int r;
	_dispd_session_unref_ struct dispd_session *sess = NULL;

	assert_ret(sink);
	assert_ret(out);

	if(dispd_sink_is_session_started(sink)) {
		return -EALREADY;
	}

	r = dispd_out_session_new(&sess,
					dispd_alloc_session_id(dispd_get()),
					sink);
	if(0 > r) {
		return r;
	}

	r = dispd_sink_set_session(sink, sess);
	if(0 > r) {
		return r;
	}

	*out = dispd_session_ref(sess);

	dispd_fn_sink_properties_changed(sink, "Session");

	return 0;
}

int dispd_fn_out_session_ended(struct dispd_session *s)
{
	assert_ret(dispd_is_out_session(s));

	dispd_sink_set_session(dispd_out_session_get_sink(s), NULL);

	return 0;
}

bool dispd_sink_is_session_started(struct dispd_sink *sink)
{
	assert_retv(sink, false);

	return NULL != sink->session;
}
