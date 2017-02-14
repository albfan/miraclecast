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

#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include "ctl.h"
#include "util.h"
#include "wfd-dbus.h"

#define wfd_dbus_object_added(o, argv...)					({		\
				const char *ifaces[] = { argv };					\
				_wfd_dbus_object_added(wfd_dbus_get(),				\
								(o),								\
								ifaces,								\
								SHL_ARRAY_LENGTH(ifaces));			\
})
#define wfd_dbus_object_removed(o, argv...)				({			\
				const char *ifaces[] = { argv };					\
				_wfd_dbus_object_removed(wfd_dbus_get(),			\
								(o),								\
								ifaces,								\
								SHL_ARRAY_LENGTH(ifaces));			\
})

struct wfd_dbus
{
	sd_bus *bus;
	sd_event *loop;

	bool exposed : 1;
};

int wfd_dbus_new(struct wfd_dbus **out, sd_event *loop, sd_bus *bus)
{
	struct wfd_dbus *wfd_dbus = calloc(1, sizeof(struct wfd_dbus));
	if(!wfd_dbus) {
		return -ENOMEM;
	}

	wfd_dbus->bus = sd_bus_ref(bus);
	wfd_dbus->loop = sd_event_ref(loop);

	*out = wfd_dbus;

	return 0;
}

void wfd_dbus_free(struct wfd_dbus *wfd_dbus)
{
	if(!wfd_dbus) {
		return;
	}

	if(wfd_dbus->exposed) {
		sd_bus_release_name(wfd_dbus->bus, "org.freedesktop.miracle.wfd");
	}

	if(wfd_dbus->bus) {
		sd_bus_unref(wfd_dbus->bus);
	}

	if(wfd_dbus->loop) {
		sd_event_unref(wfd_dbus->loop);
	}

	free(wfd_dbus);
}

static inline int wfd_dbus_get_sink_path(struct wfd_sink *s, char **out)
{
	return sd_bus_path_encode("/org/freedesktop/miracle/wfd/sink",
					wfd_sink_get_label(s),
					out);
}

static inline int wfd_dbus_get_session_path(struct wfd_session *s, char **out)
{
	char buf[64];
	int r = snprintf(buf, sizeof(buf), "%" PRIu64, wfd_session_get_id(s));
	if(0 > r) {
		return r;
	}

	return sd_bus_path_encode("/org/freedesktop/miracle/wfd/session",
					buf,
					out);
}

static int wfd_dbus_enum(sd_bus *bus,
				const char *path,
				void *userdata,
				char ***out,
				sd_bus_error *out_error)
{
	int r = 0, i = 0;
	char **nodes, *node;
	struct wfd_sink *sink;
	struct wfd_session *session;
	struct ctl_wfd *wfd = ctl_wfd_get();

	if(strcmp("/org/freedesktop/miracle/wfd", path)) {
		return 0;
	}

	if(!wfd->n_sinks) {
		return 0;
	}
   
	nodes = malloc((wfd->n_sinks + wfd->n_sessions + 1) * sizeof(char *));
	if(!nodes) {
		return -ENOMEM;
	}

	ctl_wfd_foreach_sink(sink, wfd) {
		r = wfd_dbus_get_sink_path(sink, &node);
		if(0 > r) {
			goto free_nodes;
		}
		nodes[i ++] = node;
	}

	ctl_wfd_foreach_session(session, wfd) {
		r = wfd_dbus_get_session_path(session, &node);
		if(0 > r) {
			goto free_nodes;
		}
		nodes[i ++] = node;
	}

	nodes[i ++] = NULL;
	*out = nodes;

	return 0;

free_nodes:
	while(i --) {
		free(nodes[i]);
	}
	free(nodes);
	return r;
}

int _wfd_dbus_object_removed(struct wfd_dbus *wfd_dbus,
				const char *path,
				const char **ifaces,
				size_t n_ifaces)
{
	int i, r;
	_sd_bus_message_unref_ sd_bus_message *m;

	if(!wfd_dbus) {
		return -ECANCELED;
	}
	
	r = sd_bus_message_new_signal(wfd_dbus->bus,
					&m,
					"/org/freedesktop/miracle/wfd",
					"org.freedesktop.DBus.ObjectManager",
					"InterfaceRemoved");
	if(0 > r) {
		return r;
	}

	r = sd_bus_message_append(m, "o", path);
	if(0 > r) {
		return r;
	}

	r = sd_bus_message_open_container(m, 'a', "s");
	if(0 > r) {
		return r;
	}

	for(i = 0; i < n_ifaces; i ++) {
		r = sd_bus_message_append(m, "s", ifaces[i]);
		if(0 > r) {
			return r;
		}
	}

	r = sd_bus_message_close_container(m);
	if(0 > r) {
		return r;
	}

	return sd_bus_send(wfd_dbus->bus, m, NULL);
}

int _wfd_dbus_object_added(struct wfd_dbus *wfd_dbus,
				const char *path,
				const char **ifaces,
				size_t n_ifaces)
{
	int i, r;
	_sd_bus_message_unref_ sd_bus_message *m;

	if(!wfd_dbus) {
		return -ECANCELED;
	}
	
	r = sd_bus_message_new_signal(wfd_dbus->bus,
					&m,
					"/org/freedesktop/miracle/wfd",
					"org.freedesktop.DBus.ObjectManager",
					"InterfaceAdded");
	if(0 > r) {
		return r;
	}

	r = sd_bus_message_append(m, "o", path);
	if(0 > r) {
		return r;
	}

	r = sd_bus_message_open_container(m, 'a', "{sa{sv}}");
	if(0 > r) {
		return r;
	}

	for(i = 0; i < n_ifaces; i ++) {
		r = sd_bus_message_append(m, "{sa{sv}}", ifaces[i], 0);
		if(0 > r) {
			return r;
		}
	}

	r = sd_bus_message_close_container(m);
	if(0 > r) {
		return r;
	}

	return sd_bus_send(wfd_dbus->bus, m, NULL);
}

int wfd_fn_sink_new(struct wfd_sink *s)
{
	_shl_free_ char *path;
	int r = sd_bus_path_encode("/org/freedesktop/miracle/wfd/sink",
					wfd_sink_get_label(s),
					&path);
	if(0 > r) {
		return r;
	}

	return wfd_dbus_object_added(path, "org.freedesktop.miracle.wfd.Sink");
}

int wfd_fn_sink_free(struct wfd_sink *s)
{
	_shl_free_ char *path;
	int r = sd_bus_path_encode("/org/freedesktop/miracle/wfd/sink",
					wfd_sink_get_label(s),
					&path);
	if(0 > r) {
		return r;
	}

	return wfd_dbus_object_added(path, "org.freedesktop.miracle.wfd.Sink");
}

int _wfd_fn_sink_properties_changed(struct wfd_sink *s, char **names)
{
	_shl_free_ char *path;
	int r;
	struct wfd_dbus *wfd_dbus = wfd_dbus_get();

	if(!wfd_dbus) {
		return -ECANCELED;
	}
	
	r = wfd_dbus_get_sink_path(s, &path);
	if(0 > r) {
		return r;
	}

	return sd_bus_emit_properties_changed_strv(wfd_dbus->bus,
					path,
					"org.freedesktop.miracle.wfd.Sink",
					names);
}

static int wfd_dbus_find_sink(sd_bus *bus,
				const char *path,
				const char *interface,
				void *userdata,
				void **ret_found,
				sd_bus_error *ret_error)
{
	_shl_free_ char *node = NULL;
	struct wfd_sink *sink;
	int r = sd_bus_path_decode(path,
					"/org/freedesktop/miracle/wfd/sink",
					&node);
	if(0 >= r || !node) {
		return r;
	}

	r = ctl_wfd_find_sink_by_label(ctl_wfd_get(), node, &sink);
	if(r) {
		*ret_found = sink;
	}

	return r;
}

int wfd_fn_session_new(struct wfd_session *s)
{
	_shl_free_ char *path;
	int r = wfd_dbus_get_session_path(s, &path);
	if(0 > r) {
		return r;
	}

	return wfd_dbus_object_added(path, "org.freedesktop.miracle.wfd.Session");
}

int wfd_fn_session_free(struct wfd_session *s)
{
	_shl_free_ char *path;
	int r = wfd_dbus_get_session_path(s, &path);
	if(0 > r) {
		return r;
	}

	return wfd_dbus_object_removed(path,
					"org.freedesktop.miracle.wfd.Session");
}

static int wfd_dbus_find_session(sd_bus *bus,
				const char *path,
				const char *interface,
				void *userdata,
				void **ret_found,
				sd_bus_error *ret_error)
{
	struct wfd_session *s;
	_shl_free_ char *node = NULL;
	int r = sd_bus_path_decode(path,
					"/org/freedesktop/miracle/wfd/session",
					&node);
	if(0 > r) {
		return r;
	}

	r = ctl_wfd_find_session_by_id(ctl_wfd_get(), 
					strtoull(node, NULL, 10),
					&s);
	if(r) {
		*ret_found = s;
	}

	return r;
}

static int wfd_dbus_sink_start_session(sd_bus_message *m,
				void *userdata,
				sd_bus_error *ret_error)
{
	struct wfd_sink *sink = userdata;
	_wfd_session_free_ struct wfd_session *session = NULL;
	_shl_free_ char *path = NULL;
	int r = wfd_sink_start_session(sink, &session);
	if(0 > r) {
		return r;
	}

	r = wfd_dbus_get_session_path(session, &path);
	if(0 > r) {
		return r;
	}

	session = NULL;

	return sd_bus_reply_method_return(m, "o", path);
}

//static int wfd_dbus_sink_get_audio_formats(sd_bus *bus,
//				const char *path,
//				const char *interface,
//				const char *property,
//				sd_bus_message *reply,
//				void *userdata,
//				sd_bus_error *ret_error)
//{
//	return 0;
//}
//
//static int wfd_dbus_sink_get_video_formats(sd_bus *bus,
//				const char *path,
//				const char *interface,
//				const char *property,
//				sd_bus_message *reply,
//				void *userdata,
//				sd_bus_error *ret_error)
//{
//	return 0;
//}
//
//static int wfd_dbus_sink_has_video(sd_bus *bus,
//				const char *path,
//				const char *interface,
//				const char *property,
//				sd_bus_message *reply,
//				void *userdata,
//				sd_bus_error *ret_error)
//{
//	return 0;
//}

static int wfd_dbus_sink_get_peer(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *ret_error)
{
	struct wfd_sink *s = userdata;
	_shl_free_ char *peer_path;
	int r = sd_bus_path_encode("/org/freedesktop/miracle/wifi/peer",
					s->label,
					&peer_path);
	if(0 > r) {
		return r;
	}

	return sd_bus_message_append(reply, "o", peer_path);
}

//static int wfd_dbus_sink_has_audio(sd_bus *bus,
//				const char *path,
//				const char *interface,
//				const char *property,
//				sd_bus_message *reply,
//				void *userdata,
//				sd_bus_error *ret_error)
//{
//	return 0;
//}

static int wfd_dbus_session_end(sd_bus_message *m,
				void *userdata,
				sd_bus_error *ret_error)
{
	wfd_session_end(userdata);

	return sd_bus_reply_method_return(m, NULL);
}

static int wfd_dbus_session_get_sink(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *ret_error)
{
	struct wfd_session *s = userdata;
	_shl_free_ char *sink_path;
	int r;

	if(wfd_session_get_id(s) != WFD_SESSION_DIR_OUT) {
		return 0;
	}

	r = wfd_dbus_get_sink_path(wfd_out_session_get_sink(s),
					&sink_path);
	if(0 > r) {
		return r;
	}

	return sd_bus_message_append(reply, "o", sink_path);
}

static int wfd_dbus_session_get_url(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *ret_error)
{
	struct wfd_session *s = userdata;
	return sd_bus_message_append(reply, "s", wfd_session_get_url(s));
}

static int wfd_dbus_session_get_state(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *ret_error)
{
	struct wfd_session *s = userdata;
	return sd_bus_message_append(reply, "i", wfd_session_get_state(s));
}

int _wfd_fn_session_properties_changed(struct wfd_session *s, char **names)
{
	_shl_free_ char *path;
	int r;
	struct wfd_dbus *wfd_dbus = wfd_dbus_get();

	if(!wfd_dbus) {
		return -ECANCELED;
	}

	r = wfd_dbus_get_session_path(s, &path);
	if(0 > r) {
		return r;
	}

	return sd_bus_emit_properties_changed_strv(wfd_dbus_get()->bus,
					path,
					"org.freedesktop.miracle.wfd.Session",
					names);
}

static const sd_bus_vtable wfd_dbus_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_VTABLE_END,
};

static const sd_bus_vtable wfd_dbus_sink_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("StartSession", NULL, "o", wfd_dbus_sink_start_session, 0),
	/*SD_BUS_PROPERTY("AudioFormats", "a{sv}", wfd_dbus_sink_get_audio_formats, 0, SD_BUS_VTABLE_PROPERTY_CONST),*/
	/*SD_BUS_PROPERTY("VideoFormats", "a{sv}", wfd_dbus_sink_get_video_formats, 0, SD_BUS_VTABLE_PROPERTY_CONST),*/
	/*SD_BUS_PROPERTY("HasAudio", "b", wfd_dbus_sink_has_audio, 0, SD_BUS_VTABLE_PROPERTY_CONST),*/
	/*SD_BUS_PROPERTY("HasVideo", "b", wfd_dbus_sink_has_video, 0, SD_BUS_VTABLE_PROPERTY_CONST),*/
	SD_BUS_PROPERTY("Peer", "o", wfd_dbus_sink_get_peer, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_VTABLE_END,
};

static const sd_bus_vtable wfd_dbus_session_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("End", NULL, NULL, wfd_dbus_session_end, 0),
	SD_BUS_PROPERTY("Sink", "o", wfd_dbus_session_get_sink, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Url", "o", wfd_dbus_session_get_url, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("State", "i", wfd_dbus_session_get_state, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_VTABLE_END,
};

int wfd_dbus_expose(struct wfd_dbus *wfd_dbus)
{
	int r = sd_bus_add_object_vtable(wfd_dbus->bus,
					NULL,
					"/org/freedesktop/miracle/wfd",
					"org.freedesktop.miracle.wfd.Display",
					wfd_dbus_vtable,
					wfd_dbus);
	if(0 > r) {
		return r;
	}

	r = sd_bus_add_fallback_vtable(wfd_dbus->bus,
					NULL,
					"/org/freedesktop/miracle/wfd/sink",
					"org.freedesktop.miracle.wfd.Sink",
					wfd_dbus_sink_vtable,
					wfd_dbus_find_sink,
					wfd_dbus);
	if(0 > r) {
		return r;
	}

	r = sd_bus_add_fallback_vtable(wfd_dbus->bus,
					NULL,
					"/org/freedesktop/miracle/wfd/session",
					"org.freedesktop.miracle.wfd.Session",
					wfd_dbus_session_vtable,
					wfd_dbus_find_session,
					wfd_dbus);
	if(0 > r) {
		return r;
	}

	r = sd_bus_add_node_enumerator(wfd_dbus->bus,
					NULL,
					"/org/freedesktop/miracle/wfd",
					wfd_dbus_enum,
					wfd_dbus);
	if(0 > r) {
		return r;
	}

	r = sd_bus_add_object_manager(wfd_dbus->bus, NULL, "/org/freedesktop/miracle/wfd");
	if(0 > r) {
		return r;
	}

	r = sd_bus_request_name(wfd_dbus->bus, "org.freedesktop.miracle.wfd", 0);
	if(0 < r) {
		wfd_dbus->exposed = true;
	}

	return r;
}

