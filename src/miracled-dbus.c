/*
 * MiracleCast - Wifi-Display/Miracast Implementation
 *
 * Copyright (c) 2013-2014 David Herrmann <dh.herrmann@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#define LOG_SUBSYSTEM "dbus"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include "miracle.h"
#include "miracled.h"
#include "shl_log.h"
#include "shl_util.h"

/*
 * Peer DBus
 */

static int peer_dbus_allow(sd_bus *bus, sd_bus_message *msg,
			   void *data, sd_bus_error *err)
{
	struct peer *p = data;
	const char *pin;
	int r;

	r = sd_bus_message_read(msg, "s", &pin);
	if (r < 0)
		return r;

	r = peer_allow(p, pin);
	if (r < 0)
		return r;

	return sd_bus_reply_method_return(msg, NULL);
}

static int peer_dbus_reject(sd_bus *bus, sd_bus_message *msg,
			    void *data, sd_bus_error *err)
{
	struct peer *p = data;

	peer_reject(p);
	return sd_bus_reply_method_return(msg, NULL);
}

static int peer_dbus_connect(sd_bus *bus, sd_bus_message *msg,
			     void *data, sd_bus_error *err)
{
	struct peer *p = data;
	const char *prov, *pin;
	int r;

	r = sd_bus_message_read(msg, "ss", &prov, &pin);
	if (r < 0)
		return r;

	r = peer_connect(p, prov, pin);
	if (r < 0)
		return r;

	return sd_bus_reply_method_return(msg, NULL);
}

static int peer_dbus_disconnect(sd_bus *bus, sd_bus_message *msg,
				void *data, sd_bus_error *err)
{
	struct peer *p = data;

	peer_disconnect(p);
	return sd_bus_reply_method_return(msg, NULL);
}

static int peer_dbus_get_link(sd_bus *bus,
			      const char *path,
			      const char *interface,
			      const char *property,
			      sd_bus_message *reply,
			      void *data,
			      sd_bus_error *err)
{
	_shl_free_ char *link = NULL;
	struct peer *p = data;
	int r;

	link = shl_strcat("/org/freedesktop/miracle/link/", p->l->name);
	if (!link)
		return log_ENOMEM();

	r = sd_bus_message_append_basic(reply, 'o', link);
	if (r < 0)
		return r;

	return 1;
}

static int peer_dbus_get_name(sd_bus *bus,
			      const char *path,
			      const char *interface,
			      const char *property,
			      sd_bus_message *reply,
			      void *data,
			      sd_bus_error *err)
{
	struct peer *p = data;
	const char *name;
	int r;

	name = peer_get_friendly_name(p);
	r = sd_bus_message_append_basic(reply, 's', name ? name : "<unknown>");
	if (r < 0)
		return r;

	return 1;
}

static int peer_dbus_get_connected(sd_bus *bus,
				   const char *path,
				   const char *interface,
				   const char *property,
				   sd_bus_message *reply,
				   void *data,
				   sd_bus_error *err)
{
	struct peer *p = data;
	int r, val;

	val = peer_is_connected(p);
	r = sd_bus_message_append_basic(reply, 'b', &val);
	if (r < 0)
		return r;

	return 1;
}

static int peer_dbus_get_interface(sd_bus *bus,
				   const char *path,
				   const char *interface,
				   const char *property,
				   sd_bus_message *reply,
				   void *data,
				   sd_bus_error *err)
{
	struct peer *p = data;
	const char *val;
	int r;

	val = peer_get_interface(p);
	r = sd_bus_message_append_basic(reply, 's', val ? : "");
	if (r < 0)
		return r;

	return 1;
}

static int peer_dbus_get_local_address(sd_bus *bus,
				       const char *path,
				       const char *interface,
				       const char *property,
				       sd_bus_message *reply,
				       void *data,
				       sd_bus_error *err)
{
	struct peer *p = data;
	const char *val;
	int r;

	val = peer_get_local_address(p);
	r = sd_bus_message_append_basic(reply, 's', val ? : "");
	if (r < 0)
		return r;

	return 1;
}

static int peer_dbus_get_remote_address(sd_bus *bus,
					const char *path,
					const char *interface,
					const char *property,
					sd_bus_message *reply,
					void *data,
					sd_bus_error *err)
{
	struct peer *p = data;
	const char *val;
	int r;

	val = peer_get_remote_address(p);
	r = sd_bus_message_append_basic(reply, 's', val ? : "");
	if (r < 0)
		return r;

	return 1;
}

static const sd_bus_vtable peer_dbus_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("Allow",
		      "s",
		      NULL,
		      peer_dbus_allow,
		      0),
	SD_BUS_METHOD("Reject",
		      NULL,
		      NULL,
		      peer_dbus_reject,
		      0),
	SD_BUS_METHOD("Connect",
		      "ss",
		      NULL,
		      peer_dbus_connect,
		      0),
	SD_BUS_METHOD("Disconnect",
		      NULL,
		      NULL,
		      peer_dbus_disconnect,
		      0),
	SD_BUS_PROPERTY("Link",
			"o",
			peer_dbus_get_link,
			0,
			SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Name",
			"s",
			peer_dbus_get_name,
			0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("Connected",
			"b",
			peer_dbus_get_connected,
			0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("Interface",
			"s",
			peer_dbus_get_interface,
			0,
			0),
	SD_BUS_PROPERTY("LocalAddress",
			"s",
			peer_dbus_get_local_address,
			0,
			0),
	SD_BUS_PROPERTY("RemoteAddress",
			"s",
			peer_dbus_get_remote_address,
			0,
			0),
	SD_BUS_SIGNAL("ProvisionRequest", "ss", 0),
	SD_BUS_VTABLE_END
};

static int peer_dbus_find(sd_bus *bus,
			  const char *path,
			  const char *interface,
			  void *data,
			  void **found,
			  sd_bus_error *err)
{
	struct manager *m = data;
	struct peer *p;
	const char *name;

	if (!(name = shl_startswith(path, "/org/freedesktop/miracle/peer/")))
		return 0;

	p = manager_find_peer(m, name);
	if (!p)
		return 0;

	*found = p;
	return 1;
}

void peer_dbus_provision_request(struct peer *p,
				 const char *type,
				 const char *pin)
{
	_shl_free_ char *path = NULL;
	int r;

	if (!type)
		return;
	if (!pin)
		pin = "";

	path = shl_strcat("/org/freedesktop/miracle/peer/", p->name);
	if (!path)
		return log_vENOMEM();

	r = sd_bus_emit_signal(p->l->m->bus,
			       path,
			       "org.freedesktop.miracle.Peer",
			       "ProvisionRequest",
			       "ss", type, pin);
	if (r < 0)
		log_vERR(r);
}

void peer_dbus_properties_changed(struct peer *p, const char *prop, ...)
{
	_shl_free_ char *path = NULL;
	char **strv;
	int r;

	path = shl_strcat("/org/freedesktop/miracle/peer/", p->name);
	if (!path)
		return log_vENOMEM();

	strv = strv_from_stdarg_alloca(prop);
	r = sd_bus_emit_properties_changed_strv(p->l->m->bus,
						path,
						"org.freedesktop.miracle.Peer",
						strv);
	if (r < 0)
		log_vERR(r);
}

void peer_dbus_added(struct peer *p)
{
	_shl_free_ char *path = NULL;
	int r;

	path = shl_strcat("/org/freedesktop/miracle/peer/", p->name);
	if (!path)
		return log_vENOMEM();

	r = sd_bus_emit_interfaces_added(p->l->m->bus,
					 path,
					 /*
					 "org.freedesktop.DBus.Properties",
					 "org.freedesktop.DBus.Introspectable",
					 */
					 "org.freedesktop.miracle.Peer",
					 NULL);
	if (r < 0)
		log_vERR(r);
}

void peer_dbus_removed(struct peer *p)
{
	_shl_free_ char *path = NULL;
	int r;

	path = shl_strcat("/org/freedesktop/miracle/peer/", p->name);
	if (!path)
		return log_vENOMEM();

	r = sd_bus_emit_interfaces_removed(p->l->m->bus,
					   path,
					   /*
					   "org.freedesktop.DBus.Properties",
					   "org.freedesktop.DBus.Introspectable",
					   */
					   "org.freedesktop.miracle.Peer",
					   NULL);
	if (r < 0)
		log_vERR(r);
}

/*
 * Link DBus
 */

static int link_dbus_start_scan(sd_bus *bus, sd_bus_message *msg,
				void *data, sd_bus_error *err)
{
	struct link *l = data;
	int r;

	r = link_start_scan(l);
	if (r < 0)
		return r;

	return sd_bus_reply_method_return(msg, NULL);
}

static int link_dbus_stop_scan(sd_bus *bus, sd_bus_message *msg,
			       void *data, sd_bus_error *err)
{
	struct link *l = data;

	link_stop_scan(l);

	return sd_bus_reply_method_return(msg, NULL);
}

static int link_dbus_get_type(sd_bus *bus,
			      const char *path,
			      const char *interface,
			      const char *property,
			      sd_bus_message *reply,
			      void *data,
			      sd_bus_error *err)
{
	struct link *l = data;
	int r;

	r = sd_bus_message_append_basic(reply, 's',
					link_type_to_str(l->type));
	if (r < 0)
		return r;

	return 1;
}

static int link_dbus_get_interface(sd_bus *bus,
				   const char *path,
				   const char *interface,
				   const char *property,
				   sd_bus_message *reply,
				   void *data,
				   sd_bus_error *err)
{
	struct link *l = data;
	int r;

	r = sd_bus_message_append_basic(reply, 's', l->interface);
	if (r < 0)
		return r;

	return 1;
}

static int link_dbus_get_running(sd_bus *bus,
				 const char *path,
				 const char *interface,
				 const char *property,
				 sd_bus_message *reply,
				 void *data,
				 sd_bus_error *err)
{
	struct link *l = data;
	int r, val;

	val = l->running;
	r = sd_bus_message_append_basic(reply, 'b', &val);
	if (r < 0)
		return r;

	return 1;
}

static int link_dbus_get_name(sd_bus *bus,
			      const char *path,
			      const char *interface,
			      const char *property,
			      sd_bus_message *reply,
			      void *data,
			      sd_bus_error *err)
{
	struct link *l = data;
	int r;

	r = sd_bus_message_append_basic(reply, 's', l->friendly_name);
	if (r < 0)
		return r;

	return 1;
}

static int link_dbus_set_name(sd_bus *bus,
			      const char *path,
			      const char *interface,
			      const char *property,
			      sd_bus_message *value,
			      void *data,
			      sd_bus_error *err)
{
	struct link *l = data;
	int r;
	const char *name;

	r = sd_bus_message_read(value, "s", &name);
	if (r < 0)
		return r;

	return link_set_friendly_name(l, name);
}

static const sd_bus_vtable link_dbus_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("StartScan",
		      NULL,
		      NULL,
		      link_dbus_start_scan,
		      0),
	SD_BUS_METHOD("StopScan",
		      NULL,
		      NULL,
		      link_dbus_stop_scan,
		      0),
	SD_BUS_PROPERTY("Type",
			"s",
			link_dbus_get_type,
			0,
			SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Interface",
			"s",
			link_dbus_get_interface,
			0,
			SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Running",
			"b",
			link_dbus_get_running,
			0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_WRITABLE_PROPERTY("Name",
				 "s",
				 link_dbus_get_name,
				 link_dbus_set_name,
				 0,
				 SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_SIGNAL("ScanStopped", NULL, 0),
	SD_BUS_VTABLE_END
};

static int link_dbus_find(sd_bus *bus,
			  const char *path,
			  const char *interface,
			  void *data,
			  void **found,
			  sd_bus_error *err)
{
	struct manager *m = data;
	struct link *l;
	const char *name;

	if (!(name = shl_startswith(path, "/org/freedesktop/miracle/link/")))
		return 0;

	l = manager_find_link(m, name);
	if (!l)
		return 0;

	*found = l;
	return 1;
}

void link_dbus_properties_changed(struct link *l, const char *prop, ...)
{
	_shl_free_ char *path = NULL;
	char **strv;
	int r;

	path = shl_strcat("/org/freedesktop/miracle/link/", l->name);
	if (!path)
		return log_vENOMEM();

	strv = strv_from_stdarg_alloca(prop);
	r = sd_bus_emit_properties_changed_strv(l->m->bus,
						path,
						"org.freedesktop.miracle.Link",
						strv);
	if (r < 0)
		log_vERR(r);
}

void link_dbus_scan_stopped(struct link *l)
{
	_shl_free_ char *path = NULL;
	int r;

	path = shl_strcat("/org/freedesktop/miracle/link/", l->name);
	if (!path)
		return log_vENOMEM();

	r = sd_bus_emit_signal(l->m->bus,
			       path,
			       "org.freedesktop.miracle.Link",
			       "ScanStopped",
			       NULL);
	if (r < 0)
		log_vERR(r);
}

void link_dbus_added(struct link *l)
{
	_shl_free_ char *path = NULL;
	int r;

	path = shl_strcat("/org/freedesktop/miracle/link/", l->name);
	if (!path)
		return log_vENOMEM();

	r = sd_bus_emit_interfaces_added(l->m->bus,
					 path,
					 /*
					 "org.freedesktop.DBus.Properties",
					 "org.freedesktop.DBus.Introspectable",
					 */
					 "org.freedesktop.miracle.Link",
					 NULL);
	if (r < 0)
		log_vERR(r);
}

void link_dbus_removed(struct link *l)
{
	_shl_free_ char *path = NULL;
	int r;

	path = shl_strcat("/org/freedesktop/miracle/link/", l->name);
	if (!path)
		return log_vENOMEM();

	r = sd_bus_emit_interfaces_removed(l->m->bus,
					   path,
					   /*
					   "org.freedesktop.DBus.Properties",
					   "org.freedesktop.DBus.Introspectable",
					   */
					   "org.freedesktop.miracle.Link",
					   NULL);
	if (r < 0)
		log_vERR(r);
}

/*
 * Manager DBus
 */

static int manager_dbus_add_link(sd_bus *bus, sd_bus_message *msg,
				 void *data, sd_bus_error *err)
{
	struct manager *m = data;
	const char *stype, *interface;
	unsigned int type;
	struct link *l;
	int r;

	r = sd_bus_message_read(msg, "ss", &stype, &interface);
	if (r < 0)
		return r;

	type = link_type_from_str(stype);
	if (type >= LINK_CNT)
		return sd_bus_error_setf(err,
					 SD_BUS_ERROR_INVALID_ARGS,
					 "invalid type");

	r = link_new(m, type, interface, &l);
	if (r == -EALREADY)
		return sd_bus_error_setf(err,
					 SD_BUS_ERROR_INVALID_ARGS,
					 "link already available");
	else if (r < 0)
		return r;

	return sd_bus_reply_method_return(msg, "s", l->name);
}

static int manager_dbus_remove_link(sd_bus *bus, sd_bus_message *msg,
				    void *data, sd_bus_error *err)
{
	_shl_free_ char *link = NULL;
	struct manager *m = data;
	struct link *l;
	const char *name;
	int r;

	r = sd_bus_message_read(msg, "s", &name);
	if (r < 0)
		return r;

	link = bus_label_escape(name);
	if (!link)
		return log_ENOMEM();

	l = manager_find_link(m, link);
	if (!l)
		return sd_bus_error_setf(err,
					 SD_BUS_ERROR_INVALID_ARGS,
					 "link not available");

	link_free(l);

	return sd_bus_reply_method_return(msg, NULL);
}

static const sd_bus_vtable manager_dbus_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("AddLink",
		      "ss",
		      "s",
		      manager_dbus_add_link,
		      0),
	SD_BUS_METHOD("RemoveLink",
		      "s",
		      NULL,
		      manager_dbus_remove_link,
		      0),
	SD_BUS_VTABLE_END
};

static int manager_dbus_enumerate(sd_bus *bus,
				  const char *path,
				  void *data,
				  char ***out,
				  sd_bus_error *err)
{
	struct manager *m = data;
	struct link *l;
	struct peer *p;
	size_t i;
	char **nodes, *node;
	int r;

	nodes = malloc(sizeof(*nodes) * (m->link_cnt + m->peer_cnt + 2));
	if (!nodes)
		return log_ENOMEM();

	i = 0;
	MANAGER_FOREACH_LINK(l, m) {
		if (i >= m->link_cnt + m->peer_cnt) {
			log_warning("overflow: skipping link %s",
				    l->name);
			continue;
		}

		node = shl_strcat("/org/freedesktop/miracle/link/",
				  l->name);
		if (!node) {
			r = log_ENOMEM();
			goto error;
		}

		nodes[i++] = node;
	}

	MANAGER_FOREACH_PEER(p, m) {
		if (i >= m->link_cnt + m->peer_cnt) {
			log_warning("overflow: skipping peer %s",
				    p->name);
			continue;
		}

		node = shl_strcat("/org/freedesktop/miracle/peer/",
				  p->name);
		if (!node) {
			r = log_ENOMEM();
			goto error;
		}

		nodes[i++] = node;
	}

	node = strdup("/org/freedesktop/miracle");
	if (!node) {
		r = log_ENOMEM();
		goto error;
	}

	nodes[i++] = node;
	nodes[i] = NULL;
	*out = nodes;

	return 0;

error:
	while (i--)
		free(nodes[i]);
	free(nodes);
	return r;
}

int manager_dbus_connect(struct manager *m)
{
	int r;

	r = sd_bus_add_object_vtable(m->bus,
				     "/org/freedesktop/miracle",
				     "org.freedesktop.miracle.Manager",
				     manager_dbus_vtable,
				     m);
	if (r < 0)
		goto error;

	r = sd_bus_add_node_enumerator(m->bus,
				       "/org/freedesktop/miracle",
				       manager_dbus_enumerate,
				       m);
	if (r < 0)
		goto error;

	r = sd_bus_add_fallback_vtable(m->bus,
				       "/org/freedesktop/miracle/link",
				       "org.freedesktop.miracle.Link",
				       link_dbus_vtable,
				       link_dbus_find,
				       m);
	if (r < 0)
		goto error;

	r = sd_bus_add_fallback_vtable(m->bus,
				       "/org/freedesktop/miracle/peer",
				       "org.freedesktop.miracle.Peer",
				       peer_dbus_vtable,
				       peer_dbus_find,
				       m);
	if (r < 0)
		goto error;

	r = sd_bus_add_object_manager(m->bus, "/org/freedesktop/miracle");
	if (r < 0)
		goto error;

	r = sd_bus_request_name(m->bus, "org.freedesktop.miracle", 0);
	if (r < 0) {
		log_error("cannot claim org.freedesktop.miracle bus-name: %d",
			  r);
		goto error_silent;
	}

	return 0;

error:
	log_ERR(r);
error_silent:
	manager_dbus_disconnect(m);
	return r;
}

void manager_dbus_disconnect(struct manager *m)
{
	if (!m || !m->bus)
		return;

	sd_bus_release_name(m->bus, "org.freedesktop.miracle");
	sd_bus_remove_object_manager(m->bus, "/org/freedesktop/miracle");
	sd_bus_remove_fallback_vtable(m->bus,
				      "/org/freedesktop/miracle/peer",
				      "org.freedesktop.miracle.Peer",
				      peer_dbus_vtable,
				      peer_dbus_find,
				      m);
	sd_bus_remove_fallback_vtable(m->bus,
				      "/org/freedesktop/miracle/link",
				      "org.freedesktop.miracle.Link",
				      link_dbus_vtable,
				      link_dbus_find,
				      m);
	sd_bus_remove_node_enumerator(m->bus,
				      "/org/freedesktop/miracle",
				      manager_dbus_enumerate,
				      m);
	sd_bus_remove_object_vtable(m->bus,
				    "/org/freedesktop/miracle",
				    "org.freedesktop.miracle.Manager",
				    manager_dbus_vtable,
				    m);
}
