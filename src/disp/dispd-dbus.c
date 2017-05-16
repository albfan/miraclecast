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
#include "dispd.h"
#include "util.h"
#include "shl_log.h"
#include "dispd-dbus.h"

#define dispd_dbus_object_added(o, argv...)					({		\
				const char *ifaces[] = { argv };					\
				_dispd_dbus_object_added(dispd_dbus_get(),				\
								(o),								\
								ifaces,								\
								SHL_ARRAY_LENGTH(ifaces));			\
})
#define dispd_dbus_object_removed(o, argv...)				({			\
				const char *ifaces[] = { argv };					\
				_dispd_dbus_object_removed(dispd_dbus_get(),			\
								(o),								\
								ifaces,								\
								SHL_ARRAY_LENGTH(ifaces));			\
})

struct dispd_dbus
{
	sd_bus *bus;
	sd_event *loop;

	bool exposed : 1;
};

int dispd_dbus_new(struct dispd_dbus **out, sd_event *loop, sd_bus *bus)
{
	struct dispd_dbus *dispd_dbus = calloc(1, sizeof(struct dispd_dbus));
	if(!dispd_dbus) {
		return log_ENOMEM();
	}

	dispd_dbus->bus = sd_bus_ref(bus);
	dispd_dbus->loop = sd_event_ref(loop);

	*out = dispd_dbus;

	return 0;
}

void dispd_dbus_free(struct dispd_dbus *dispd_dbus)
{
	if(!dispd_dbus) {
		return;
	}

	if(dispd_dbus->exposed) {
		sd_bus_release_name(dispd_dbus->bus, "org.freedesktop.miracle.wfd");
	}

	if(dispd_dbus->bus) {
		sd_bus_unref(dispd_dbus->bus);
	}

	if(dispd_dbus->loop) {
		sd_event_unref(dispd_dbus->loop);
	}

	free(dispd_dbus);
}

static inline int dispd_dbus_get_sink_path(struct dispd_sink *s, char **out)
{
	int r = sd_bus_path_encode("/org/freedesktop/miracle/wfd/sink",
					dispd_sink_get_label(s),
					out);
	if(0 > r) {
		return log_ERRNO();
	}

	return 0;
}

static inline int dispd_dbus_get_session_path(struct dispd_session *s, char **out)
{
	char buf[64];
	int r = snprintf(buf, sizeof(buf), "%u", dispd_session_get_id(s));
	if(0 > r) {
		return log_ERRNO();
	}

	r = sd_bus_path_encode("/org/freedesktop/miracle/wfd/session",
					buf,
					out);
	if(0 > r) {
		return log_ERRNO();
	}

	return 0;
}

static int dispd_dbus_enum(sd_bus *bus,
				const char *path,
				void *userdata,
				char ***out,
				sd_bus_error *out_error)
{
	int r = 0, i = 0;
	char **nodes, *node;
	struct dispd_sink *sink;
	struct dispd_session *session;
	struct dispd *dispd = dispd_get();

	if(strcmp("/org/freedesktop/miracle/wfd", path)) {
		return 0;
	}

	if(!dispd->n_sinks) {
		return 0;
	}
   
	nodes = malloc((dispd->n_sinks + dispd->n_sessions + 1) * sizeof(char *));
	if(!nodes) {
		return log_ENOMEM();
	}

	dispd_foreach_sink(sink, dispd) {
		r = dispd_dbus_get_sink_path(sink, &node);
		if(0 > r) {
			goto free_nodes;
		}
		nodes[i ++] = node;
	}

	dispd_foreach_session(session, dispd) {
		r = dispd_dbus_get_session_path(session, &node);
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
	return log_ERRNO();
}

int _dispd_dbus_object_removed(struct dispd_dbus *dispd_dbus,
				const char *path,
				const char **ifaces,
				size_t n_ifaces)
{
	int i, r;
	_sd_bus_message_unref_ sd_bus_message *m = NULL;

	if(!dispd_dbus) {
		return -ECANCELED;
	}
	
	r = sd_bus_message_new_signal(dispd_dbus->bus,
					&m,
					"/org/freedesktop/miracle/wfd",
					"org.freedesktop.DBus.ObjectManager",
					"InterfacesRemoved");
	if(0 > r) {
		return log_ERRNO();
	}

	r = sd_bus_message_append(m, "o", path);
	if(0 > r) {
		return log_ERRNO();
	}

	r = sd_bus_message_open_container(m, 'a', "s");
	if(0 > r) {
		return log_ERRNO();
	}

	for(i = 0; i < n_ifaces; i ++) {
		r = sd_bus_message_append(m, "s", ifaces[i]);
		if(0 > r) {
			return log_ERRNO();
		}
	}

	r = sd_bus_message_close_container(m);
	if(0 > r) {
		return log_ERRNO();
	}

	r = sd_bus_send(dispd_dbus->bus, m, NULL);
	if(0 > r) {
		return log_ERRNO();
	}

	return 0;
}

int _dispd_dbus_object_added(struct dispd_dbus *dispd_dbus,
				const char *path,
				const char **ifaces,
				size_t n_ifaces)
{
	int i, r;
	_sd_bus_message_unref_ sd_bus_message *m = NULL;

	if(!dispd_dbus) {
		return -ECANCELED;
	}
	
	r = sd_bus_message_new_signal(dispd_dbus->bus,
					&m,
					"/org/freedesktop/miracle/wfd",
					"org.freedesktop.DBus.ObjectManager",
					"InterfacesAdded");
	if(0 > r) {
		return log_ERRNO();
	}

	r = sd_bus_message_append(m, "o", path);
	if(0 > r) {
		return log_ERRNO();
	}

	r = sd_bus_message_open_container(m, 'a', "{sa{sv}}");
	if(0 > r) {
		return log_ERRNO();
	}

	for(i = 0; i < n_ifaces; i ++) {
		r = sd_bus_message_append(m, "{sa{sv}}", ifaces[i], 0);
		if(0 > r) {
			return log_ERRNO();
		}
	}

	r = sd_bus_message_close_container(m);
	if(0 > r) {
		return log_ERRNO();
	}

	r = sd_bus_send(dispd_dbus->bus, m, NULL);
	if(0 > r) {
		return log_ERRNO();
	}

	return 0;
}

int dispd_fn_sink_new(struct dispd_sink *s)
{
	_shl_free_ char *path = NULL;
	int r = dispd_dbus_get_sink_path(s, &path);
	if(0 > r) {
		return log_ERRNO();
	}

	r = dispd_dbus_object_added(path, "org.freedesktop.miracle.wfd.Sink");
	if(0 > r) {
		return log_ERRNO();
	}

	return 0;
}

int dispd_fn_sink_free(struct dispd_sink *s)
{
	_shl_free_ char *path = NULL;
	int r = dispd_dbus_get_sink_path(s, &path);
	if(0 > r) {
		return log_ERRNO();
	}

	r = dispd_dbus_object_removed(path, "org.freedesktop.miracle.wfd.Sink");
	if(0 > r) {
		return log_ERRNO();
	}

	return 0;
}

int _dispd_fn_sink_properties_changed(struct dispd_sink *s, char **names)
{
	_shl_free_ char *path = NULL;
	int r;
	struct dispd_dbus *dispd_dbus = dispd_dbus_get();

	if(!dispd_dbus) {
		return log_ERR(-ECANCELED);
	}
	
	r = dispd_dbus_get_sink_path(s, &path);
	if(0 > r) {
		return log_ERR(r);
	}

	r = sd_bus_emit_properties_changed_strv(dispd_dbus->bus,
					path,
					"org.freedesktop.miracle.wfd.Sink",
					names);
	if(0 > r) {
		return log_ERR(r);
	}

	return 0;
}

static int dispd_dbus_find_sink(sd_bus *bus,
				const char *path,
				const char *interface,
				void *userdata,
				void **ret_found,
				sd_bus_error *ret_error)
{
	_shl_free_ char *node = NULL;
	struct dispd_sink *sink;
	int r = sd_bus_path_decode(path,
					"/org/freedesktop/miracle/wfd/sink",
					&node);
	if(0 >= r || !node) {
		return r;
	}

	r = dispd_find_sink_by_label(dispd_get(), node, &sink);
	if(r) {
		*ret_found = sink;
	}

	return r;
}

int dispd_fn_session_new(struct dispd_session *s)
{
	_shl_free_ char *path = NULL;
	int r = dispd_dbus_get_session_path(s, &path);
	if(0 > r) {
		return log_ERRNO();
	}

	r = dispd_dbus_object_added(path, "org.freedesktop.miracle.wfd.Session");
	if(0 > r) {
		return log_ERRNO();
	}

	return 0;
}

int dispd_fn_session_free(struct dispd_session *s)
{
	_shl_free_ char *path = NULL;
	int r = dispd_dbus_get_session_path(s, &path);
	if(0 > r) {
		return log_ERRNO();
	}

	r = dispd_dbus_object_removed(path, "org.freedesktop.miracle.wfd.Session");
	if(0 > r) {
		return log_ERRNO();
	}

	return 0;
}

static int dispd_dbus_find_session(sd_bus *bus,
				const char *path,
				const char *interface,
				void *userdata,
				void **ret_found,
				sd_bus_error *ret_error)
{
	struct dispd_session *s;
	_shl_free_ char *node = NULL;
	int r = sd_bus_path_decode(path,
					"/org/freedesktop/miracle/wfd/session",
					&node);
	if(0 > r) {
		return log_ERRNO();
	}

	r = dispd_find_session_by_id(dispd_get(), 
					strtoull(node, NULL, 10),
					&s);
	if(r) {
		*ret_found = s;
	}

	return r;
}

static int get_user_runtime_path(char **out, sd_bus *bus, uid_t uid)
{
	_sd_bus_message_unref_ sd_bus_message *rep = NULL;
	_sd_bus_error_free_ sd_bus_error error = SD_BUS_ERROR_NULL;
	char p[256];
	char *rp;
	int r;

	assert_ret(out);
	assert_ret(bus);
	assert_ret(0 < uid);

	r = snprintf(p, sizeof(p), "/org/freedesktop/login1/user/_%d", uid);
	if(0 > r) {
		return log_ERRNO();
	}

	r = sd_bus_call_method(bus,
					"org.freedesktop.login1",
					p,
					"org.freedesktop.DBus.Properties",
					"Get",
					&error,
					&rep,
					"ss",
					"org.freedesktop.login1.User",
					"RuntimePath");
	if(0 > r) {
		log_warning("%s: %s", error.name, error.message);
		return r;
	}

	r = sd_bus_message_read(rep, "v", "s", &rp);
	if(0 > r) {
		return log_ERR(r);
	}

	*out = strdup(rp);
	if(!*out) {
		return log_ENOMEM();
	}

	return 0;
}

static int dispd_dbus_sink_start_session(sd_bus_message *m,
				void *userdata,
				sd_bus_error *ret_error)
{
	_dispd_session_unref_ struct dispd_session *sess = NULL;
	_shl_free_ char *path = NULL, *disp_type_name = NULL, *disp_name = NULL;
	_sd_bus_creds_unref_ sd_bus_creds *creds = NULL;
	_shl_free_ char *runtime_path = NULL;
	struct dispd_sink *sink = userdata;
	char *disp_params;
	const char *disp, *disp_auth;
	const char *audio_dev;
	struct dispd_rectangle rect;
	pid_t pid;
	uid_t uid;
	gid_t gid;
	int r;

	r = sd_bus_message_read(m,
					"ssuuuus",
					&disp_auth,
					&disp,
					&rect.x,
					&rect.y,
					&rect.width,
					&rect.height,
					&audio_dev);
	if(0 > r) {
		return log_ERR(r);
	}

	r = sscanf(disp, "%m[^:]://%ms",
					&disp_type_name,
					&disp_name);
	if(r != 2) {
		return log_EINVAL();
	}

	if(strcmp("x", disp_type_name)) {
		return log_EINVAL();
	}

	r = dispd_sink_create_session(sink, &sess);
	if(0 > r) {
		return log_ERR(r);
	}

	dispd_session_set_disp_type(sess, DISPD_DISPLAY_SERVER_TYPE_X);
	if(0 > r) {
		return log_ERR(r);
	}

	disp_params = strchr(disp_name, '?');
	if(disp_params) {
		*disp_params ++ = '\0';
	}

	r = dispd_session_set_disp_name(sess, disp_name);
	if(0 > r) {
		return log_ERR(r);
	}

	r = dispd_session_set_disp_params(sess, disp_params);
	if(0 > r) {
		return log_ERR(r);
	}

	r = dispd_session_set_disp_auth(sess, disp_auth);
	if(0 > r) {
		return log_ERR(r);
	}

	r = dispd_session_set_disp_dimension(sess, &rect);
	if(0 > r) {
		return log_ERR(r);
	}

	r = dispd_session_set_audio_type(sess, DISPD_AUDIO_SERVER_TYPE_PULSE_AUDIO);
	if(0 > r) {
		return log_ERR(r);
	}

	r = dispd_session_set_audio_dev_name(sess, audio_dev);
	if(0 > r) {
		return log_ERR(r);
	}

	r = sd_bus_query_sender_creds(m, SD_BUS_CREDS_PID, &creds);
	if(0 > r) {
		return log_ERR(r);
	}

	r = sd_bus_creds_get_pid(creds, &pid);
	if(0 > r) {
		return log_ERR(r);
	}

	sd_bus_creds_unref(creds);
	creds = NULL;

	r = sd_bus_creds_new_from_pid(&creds,
					pid,
					SD_BUS_CREDS_UID | SD_BUS_CREDS_GID);
	if(0 > r) {
		return log_ERR(r);
	}
	dispd_session_set_client_pid(sess, gid);

	r = sd_bus_creds_get_uid(creds, &uid);
	if(0 > r) {
		return log_ERR(r);
	}
	dispd_session_set_client_uid(sess, uid);

	sd_bus_creds_get_gid(creds, &gid);
	if(0 > r) {
		return log_ERR(r);
	}
	dispd_session_set_client_gid(sess, gid);

	r = get_user_runtime_path(&runtime_path,
					sd_bus_message_get_bus(m),
					uid);
	if(0 > r) {
		return log_ERR(r);
	}

	r = dispd_session_set_runtime_path(sess, runtime_path);
	if(0 > r) {
		return log_ERR(r);
	}

	r = dispd_session_start(sess);
	if(0 > r) {
		return log_ERR(r);
	}

	r = dispd_dbus_get_session_path(sess, &path);
	if(0 > r) {
		return log_ERR(r);
	}

	r = sd_bus_reply_method_return(m, "o", path);
	if(0 > r) {
		return log_ERR(r);
	}

	return 0;
}

static int dispd_dbus_sink_get_session(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *ret_error)
{
	struct dispd_sink *s = userdata;
	_shl_free_ char *session_path = NULL;
	int r;

	if(s->session) {
		r = dispd_dbus_get_session_path(s->session, &session_path);
		if(0 > r) {
			return log_ERRNO();
		}
	}
	else {
		session_path = strdup("/");
		if(!session_path) {
			return log_ENOMEM();
		}
	}

	r = sd_bus_message_append(reply, "o", session_path);
	if(0 > r) {
		return log_ERRNO();
	}

	return 1;
}

static int dispd_dbus_sink_get_peer(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *ret_error)
{
	struct dispd_sink *s = userdata;
	_shl_free_ char *peer_path = NULL;
	int r = sd_bus_path_encode("/org/freedesktop/miracle/wifi/peer",
					s->label,
					&peer_path);
	if(0 > r) {
		return log_ERRNO();
	}

	r = sd_bus_message_append(reply, "o", peer_path);
	if(0 > r) {
		return log_ERRNO();
	}

	return 1;
}

//static int dispd_dbus_sink_has_audio(sd_bus *bus,
//				const char *path,
//				const char *interface,
//				const char *property,
//				sd_bus_message *reply,
//				void *userdata,
//				sd_bus_error *ret_error)
//{
//	return 0;
//}
static int dispd_dbus_session_resume(sd_bus_message *m,
				void *userdata,
				sd_bus_error *ret_error)
{
	struct dispd_session *s = userdata;
	int r;

	if(dispd_session_is_established(s)) {
		r = dispd_session_resume(s);
		if(0 > r) {
			return log_ERRNO();
		}
	}
	else {
		return -ENOTCONN;
	}

	r = sd_bus_reply_method_return(m, NULL);
	if(0 > r) {
		return log_ERRNO();
	}

	return 0;
}

static int dispd_dbus_session_pause(sd_bus_message *m,
				void *userdata,
				sd_bus_error *ret_error)
{
	struct dispd_session *s = userdata;
	int r;

	if(dispd_session_is_established(s)) {
		r = dispd_session_pause(s);
		if(0 > r) {
			return log_ERRNO();
		}
	}
	else {
		return -ENOTCONN;
	}

	r = sd_bus_reply_method_return(m, NULL);
	if(0 > r) {
		return log_ERRNO();
	}

	return 0;
}

static int dispd_dbus_session_teardown(sd_bus_message *m,
				void *userdata,
				sd_bus_error *ret_error)
{
	struct dispd_session *s = userdata;
	int r = 0;

	if(dispd_session_is_established(s)) {
		r = dispd_session_teardown(s);
	}

	if(0 > r) {
		dispd_session_destroy(s);
	}

	r = sd_bus_reply_method_return(m, NULL);
	if(0 > r) {
		return log_ERRNO();
	}

	return 0;
}

static int dispd_dbus_session_get_sink(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *ret_error)
{
	struct dispd_session *s = userdata;
	_shl_free_ char *sink_path = NULL;
	int r;

	if(dispd_session_get_dir(s) != DISPD_SESSION_DIR_OUT) {
		sink_path = strdup("/");
	}
	else {
		dispd_dbus_get_sink_path(dispd_out_session_get_sink(s), &sink_path);
	}
	if(!sink_path) {
		return log_ENOMEM();
	}

	r = sd_bus_message_append(reply, "o", sink_path);
	if(0 > r) {
		return log_ERRNO();
	}

	return 1;
}

static int dispd_dbus_get_session_presentation_url(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *ret_error)
{
	struct dispd_session *s = userdata;
	int r =  sd_bus_message_append(reply,
					"s",
					dispd_session_get_stream_url(s) ? : "");
	if(0 > r) {
		return log_ERRNO();
	}

	return 1;
}

static int dispd_dbus_get_session_state(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *ret_error)
{
	struct dispd_session *s = userdata;
	int r = sd_bus_message_append(reply, "i", dispd_session_get_state(s));
	if(0 > r) {
		return log_ERRNO();
	}

	return 1;
}

int _dispd_fn_session_properties_changed(struct dispd_session *s, char **names)
{
	_shl_free_ char *path = NULL;
	int r;
	struct dispd_dbus *dispd_dbus = dispd_dbus_get();

	if(!dispd_dbus) {
		return -ECANCELED;
	}

	r = dispd_dbus_get_session_path(s, &path);
	if(0 > r) {
		return log_ERRNO();
	}

	r = sd_bus_emit_properties_changed_strv(dispd_dbus_get()->bus,
					path,
					"org.freedesktop.miracle.wfd.Session",
					names);
	if(0 > r) {
		return log_ERRNO();
	}

	return 0;
}

static int dispd_dbus_shutdown(sd_bus_message *m,
				void *userdata,
				sd_bus_error *ret_error)
{
	int r;

	dispd_shutdown(dispd_get());

	r = sd_bus_reply_method_return(m, NULL);
	if(0 > r) {
		return log_ERR(r);
	}

	return 0;
}

static const sd_bus_vtable dispd_dbus_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Shutdown", NULL, NULL, dispd_dbus_shutdown, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END,
};

static const sd_bus_vtable dispd_dbus_sink_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("StartSession", "ssuuuus", "o", dispd_dbus_sink_start_session, SD_BUS_VTABLE_UNPRIVILEGED),
	/*SD_BUS_PROPERTY("AudioFormats", "a{sv}", dispd_dbus_sink_get_audio_formats, 0, SD_BUS_VTABLE_PROPERTY_CONST),*/
	/*SD_BUS_PROPERTY("VideoFormats", "a{sv}", dispd_dbus_sink_get_video_formats, 0, SD_BUS_VTABLE_PROPERTY_CONST),*/
	/*SD_BUS_PROPERTY("HasAudio", "b", dispd_dbus_sink_has_audio, 0, SD_BUS_VTABLE_PROPERTY_CONST),*/
	/*SD_BUS_PROPERTY("HasVideo", "b", dispd_dbus_sink_has_video, 0, SD_BUS_VTABLE_PROPERTY_CONST),*/
	SD_BUS_PROPERTY("Session", "o", dispd_dbus_sink_get_session, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("Peer", "o", dispd_dbus_sink_get_peer, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_VTABLE_END,
};

static const sd_bus_vtable dispd_dbus_session_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Resume", NULL, NULL, dispd_dbus_session_resume, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("Pause", NULL, NULL, dispd_dbus_session_pause, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("Teardown", NULL, NULL, dispd_dbus_session_teardown, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_PROPERTY("Sink", "o", dispd_dbus_session_get_sink, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Url", "s", dispd_dbus_get_session_presentation_url, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("State", "i", dispd_dbus_get_session_state, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_VTABLE_END,
};

int dispd_dbus_expose(struct dispd_dbus *dispd_dbus)
{
	int r = sd_bus_add_object_vtable(dispd_dbus->bus,
					NULL,
					"/org/freedesktop/miracle/wfd",
					"org.freedesktop.miracle.wfd",
					dispd_dbus_vtable,
					dispd_dbus);
	if(0 > r) {
		return r;
	}

	r = sd_bus_add_fallback_vtable(dispd_dbus->bus,
					NULL,
					"/org/freedesktop/miracle/wfd/sink",
					"org.freedesktop.miracle.wfd.Sink",
					dispd_dbus_sink_vtable,
					dispd_dbus_find_sink,
					dispd_dbus);
	if(0 > r) {
		return r;
	}

	r = sd_bus_add_fallback_vtable(dispd_dbus->bus,
					NULL,
					"/org/freedesktop/miracle/wfd/session",
					"org.freedesktop.miracle.wfd.Session",
					dispd_dbus_session_vtable,
					dispd_dbus_find_session,
					dispd_dbus);
	if(0 > r) {
		return r;
	}

	r = sd_bus_add_node_enumerator(dispd_dbus->bus,
					NULL,
					"/org/freedesktop/miracle/wfd",
					dispd_dbus_enum,
					dispd_dbus);
	if(0 > r) {
		return r;
	}

	r = sd_bus_add_object_manager(dispd_dbus->bus, NULL, "/org/freedesktop/miracle/wfd");
	if(0 > r) {
		return r;
	}

	r = sd_bus_request_name(dispd_dbus->bus, "org.freedesktop.miracle.wfd", 0);
	if(0 < r) {
		dispd_dbus->exposed = true;
	}

	return r;
}

