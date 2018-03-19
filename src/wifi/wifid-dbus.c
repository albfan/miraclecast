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

#define LOG_SUBSYSTEM "dbus"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include "shl_log.h"
#include "shl_util.h"
#include "util.h"
#include "wifid.h"

static char *peer_dbus_get_path(struct peer *p)
{
	char buf[128], *node;
	int r;

	sprintf(buf, "%s@%u", p->p2p_mac, p->l->ifindex);

	r = sd_bus_path_encode("/org/freedesktop/miracle/wifi/peer",
			       buf,
			       &node);
	if (r < 0) {
		log_vERR(r);
		return NULL;
	}

	return node;
}

static char *link_dbus_get_path(struct link *l)
{
	char buf[128], *node;
	int r;

	sprintf(buf, "%u", l->ifindex);

	r = sd_bus_path_encode("/org/freedesktop/miracle/wifi/link",
			       buf,
			       &node);
	if (r < 0) {
		log_vERR(r);
		return NULL;
	}

	return node;
}

/*
 * Peer DBus
 */

static int peer_dbus_connect(sd_bus_message *msg,
			     void *data, sd_bus_error *err)
{
	struct peer *p = data;
	const char *prov, *pin;
	int r;

	r = sd_bus_message_read(msg, "ss", &prov, &pin);
	if (r < 0)
		return r;

	if (!*prov || !strcmp(prov, "auto"))
		prov = NULL;
	if (!*pin)
		pin = NULL;

	r = peer_connect(p, prov, pin);
	if (r < 0)
		return r;

	return sd_bus_reply_method_return(msg, NULL);
}

static int peer_dbus_disconnect(sd_bus_message *msg,
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
	_shl_free_ char *node = NULL;
	struct peer *p = data;
	int r;

	node = link_dbus_get_path(p->l);
	if (!node)
		return -ENOMEM;

	r = sd_bus_message_append_basic(reply, 'o', node);
	if (r < 0)
		return r;

	return 1;
}

static int peer_dbus_get_p2p_mac(sd_bus *bus,
				 const char *path,
				 const char *interface,
				 const char *property,
				 sd_bus_message *reply,
				 void *data,
				 sd_bus_error *err)
{
	struct peer *p = data;
	int r;

	r = sd_bus_message_append_basic(reply, 's', p->p2p_mac);
	if (r < 0)
		return r;

	return 1;
}

static int peer_dbus_get_friendly_name(sd_bus *bus,
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
	if (!name)
		name = "<unknown>";

	r = sd_bus_message_append_basic(reply, 's', name);
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
	int r;

	r = sd_bus_message_append(reply, "b", p->connected);
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
	int r;

	r = sd_bus_message_append(reply, "s", peer_get_interface(p));
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
	int r;

	r = sd_bus_message_append(reply, "s", peer_get_local_address(p));
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
	int r;

	r = sd_bus_message_append(reply, "s", peer_get_remote_address(p));
	if (r < 0)
		return r;

	return 1;
}

static int peer_dbus_get_wfd_subelements(sd_bus *bus,
					 const char *path,
					 const char *interface,
					 const char *property,
					 sd_bus_message *reply,
					void *data,
					sd_bus_error *err)
{
	struct peer *p = data;
	int r;

	r = sd_bus_message_append(reply, "s", peer_get_wfd_subelements(p));
	if (r < 0)
		return r;

	return 1;
}

static const sd_bus_vtable peer_dbus_vtable[] = {
	SD_BUS_VTABLE_START(0),
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
	SD_BUS_PROPERTY("P2PMac",
			"s",
			peer_dbus_get_p2p_mac,
			0,
			SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("FriendlyName",
			"s",
			peer_dbus_get_friendly_name,
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
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("LocalAddress",
			"s",
			peer_dbus_get_local_address,
			0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("RemoteAddress",
			"s",
			peer_dbus_get_remote_address,
			0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("WfdSubelements",
			"s",
			peer_dbus_get_wfd_subelements,
			0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_SIGNAL("ProvisionDiscovery", "ss", 0),
	SD_BUS_SIGNAL("GoNegRequest", "ss", 0),
	SD_BUS_SIGNAL("FormationFailure", "s", 0),
	SD_BUS_VTABLE_END
};

static int peer_dbus_find(sd_bus *bus,
			  const char *path,
			  const char *interface,
			  void *data,
			  void **found,
			  sd_bus_error *err)
{
	_shl_free_ char *label = NULL;
	struct manager *m = data;
	struct link *l;
	struct peer *p;
	char *sep;
	int r;

	r = sd_bus_path_decode(path,
			       "/org/freedesktop/miracle/wifi/peer",
			       &label);
	if (r <= 0)
		return r;

	sep = strchr(label, '@');
	if (sep) {
		*sep = 0;
		l = manager_find_link_by_label(m, sep + 1);
		if (!l || !l->public)
			return 0;

		p = link_find_peer_by_label(l, label);
		if (!p || !p->public)
			return 0;
	} else {
		p = NULL;

		MANAGER_FOREACH_LINK(l, m) {
			if (!l->public)
				continue;

			p = link_find_peer_by_label(l, label);
			if (p)
				break;
		}

		if (!p || !p->public)
			return 0;
	}

	*found = p;
	return 1;
}

void peer_dbus_properties_changed(struct peer *p, const char *prop, ...)
{
	_shl_free_ char *node = NULL;
	char **strv;
	int r;

	if (!p->public)
		return;

	node = peer_dbus_get_path(p);
	if (!node)
		return;

	strv = strv_from_stdarg_alloca(prop);
	r = sd_bus_emit_properties_changed_strv(p->l->m->bus,
						node,
						"org.freedesktop.miracle.wifi.Peer",
						strv);
	if (r < 0)
		log_vERR(r);
}

void peer_dbus_provision_discovery(struct peer *p,
				   const char *type,
				   const char *pin)
{
	_shl_free_ char *node = NULL;
	int r;

	if (!type)
		return;
	if (!pin)
		pin = "";

	node = peer_dbus_get_path(p);
	if (!node)
		return;

	r = sd_bus_emit_signal(p->l->m->bus,
			       node,
			       "org.freedesktop.miracle.wifi.Peer",
			       "ProvisionDiscovery",
			       "ss", type, pin);
	if (r < 0)
		log_vERR(r);
}

void peer_dbus_go_neg_request(struct peer *p,
				   const char *type,
				   const char *pin)
{
	_shl_free_ char *node = NULL;
	int r;

	if (!type)
		return;
	if (!pin)
		pin = "";

	node = peer_dbus_get_path(p);
	if (!node)
		return;

	r = sd_bus_emit_signal(p->l->m->bus,
			       node,
			       "org.freedesktop.miracle.wifi.Peer",
			       "GoNegRequest",
			       "ss", type, pin);
	if (r < 0)
		log_vERR(r);
}

void peer_dbus_formation_failure(struct peer *p, const char *reason)
{
	_shl_free_ char *node = NULL;
	int r;

	node = peer_dbus_get_path(p);
	if (!node)
		return;

	r = sd_bus_emit_signal(p->l->m->bus,
			       node,
			       "org.freedesktop.miracle.wifi.Peer",
			       "FormationFailure",
			       "s", reason);
	if (r < 0)
		log_vERR(r);
}

void peer_dbus_added(struct peer *p)
{
	_shl_free_ char *node = NULL;
	int r;

	node = peer_dbus_get_path(p);
	if (!node)
		return;

	r = sd_bus_emit_interfaces_added(p->l->m->bus,
					 node,
					 "org.freedesktop.miracle.wifi.Peer",
					 NULL);
	if (r < 0)
		log_vERR(r);
}

void peer_dbus_removed(struct peer *p)
{
	_shl_free_ char *node = NULL;
	int r;

	node = peer_dbus_get_path(p);
	if (!node)
		return;

	r = sd_bus_emit_interfaces_removed(p->l->m->bus,
					   node,
					   /*
					   "org.freedesktop.DBus.Properties",
					   "org.freedesktop.DBus.Introspectable",
					   */
					   "org.freedesktop.miracle.wifi.Peer",
					   NULL);
	if (r < 0)
		log_vERR(r);
}

/*
 * Link DBus
 */

static int link_dbus_get_interface_index(sd_bus *bus,
					 const char *path,
					 const char *interface,
					 const char *property,
					 sd_bus_message *reply,
					 void *data,
					 sd_bus_error *err)
{
	struct link *l = data;
	int r;

	r = sd_bus_message_append_basic(reply, 'u', &l->ifindex);
	if (r < 0)
		return r;

	return 1;
}

static int link_dbus_get_interface_name(sd_bus *bus,
					const char *path,
					const char *interface,
					const char *property,
					sd_bus_message *reply,
					void *data,
					sd_bus_error *err)
{
	struct link *l = data;
	int r;

	r = sd_bus_message_append_basic(reply, 's', l->ifname);
	if (r < 0)
		return r;

	return 1;
}

static int link_dbus_get_friendly_name(sd_bus *bus,
				       const char *path,
				       const char *interface,
				       const char *property,
				       sd_bus_message *reply,
				       void *data,
				       sd_bus_error *err)
{
	struct link *l = data;
	int r;

	r = sd_bus_message_append(reply, "s", link_get_friendly_name(l));
	if (r < 0)
		return r;

	return 1;
}

static int link_dbus_set_friendly_name(sd_bus *bus,
				       const char *path,
				       const char *interface,
				       const char *property,
				       sd_bus_message *value,
				       void *data,
				       sd_bus_error *err)
{
	struct link *l = data;
	const char *name;
	int r;

	r = sd_bus_message_read(value, "s", &name);
	if (r < 0)
		return r;

	if (!name || !*name)
		return -EINVAL;

	return link_set_friendly_name(l, name);
}

static int link_dbus_get_managed(sd_bus *bus,
				      const char *path,
				      const char *interface,
				      const char *property,
				      sd_bus_message *reply,
				      void *data,
				      sd_bus_error *err)
{
	struct link *l = data;
	int r;

	r = sd_bus_message_append(reply, "b", link_get_managed(l));
	if (r < 0)
		return r;

	return 1;
}

static int link_dbus_set_managed(sd_bus *bus,
				      const char *path,
				      const char *interface,
				      const char *property,
				      sd_bus_message *value,
				      void *data,
				      sd_bus_error *err)
{
	struct link *l = data;
	int val, r;

	r = sd_bus_message_read(value, "b", &val);
	if (r < 0)
		return r;

	return link_set_managed(l, val);
}

static int link_dbus_get_p2p_scanning(sd_bus *bus,
				      const char *path,
				      const char *interface,
				      const char *property,
				      sd_bus_message *reply,
				      void *data,
				      sd_bus_error *err)
{
	struct link *l = data;
	int r;

	r = sd_bus_message_append(reply, "b", link_get_p2p_scanning(l));
	if (r < 0)
		return r;

	return 1;
}

static int link_dbus_set_p2p_scanning(sd_bus *bus,
				      const char *path,
				      const char *interface,
				      const char *property,
				      sd_bus_message *value,
				      void *data,
				      sd_bus_error *err)
{
	struct link *l = data;
	int val, r;

	r = sd_bus_message_read(value, "b", &val);
	if (r < 0)
		return r;

	return link_set_p2p_scanning(l, val);
}

static int link_dbus_get_wfd_subelements(sd_bus *bus,
					 const char *path,
					 const char *interface,
					 const char *property,
					 sd_bus_message *reply,
					 void *data,
					 sd_bus_error *err)
{
	struct link *l = data;
	int r;

	r = sd_bus_message_append(reply, "s", link_get_wfd_subelements(l));
	if (r < 0)
		return r;

	return 1;
}

static int link_dbus_set_wfd_subelements(sd_bus *bus,
					 const char *path,
					 const char *interface,
					 const char *property,
					 sd_bus_message *value,
					 void *data,
					 sd_bus_error *err)
{
	struct link *l = data;
	const char *val;
	int r;

	r = sd_bus_message_read(value, "s", &val);
	if (r < 0)
		return r;

	return link_set_wfd_subelements(l, val);
}

static const sd_bus_vtable link_dbus_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("InterfaceIndex",
			"u",
			link_dbus_get_interface_index,
			0,
			SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("InterfaceName",
			"s",
			link_dbus_get_interface_name,
			0,
			SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_WRITABLE_PROPERTY("FriendlyName",
				 "s",
				 link_dbus_get_friendly_name,
				 link_dbus_set_friendly_name,
				 0,
				 SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_WRITABLE_PROPERTY("Managed",
				 "b",
				 link_dbus_get_managed,
				 link_dbus_set_managed,
				 0,
				 SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_WRITABLE_PROPERTY("P2PScanning",
				 "b",
				 link_dbus_get_p2p_scanning,
				 link_dbus_set_p2p_scanning,
				 0,
				 SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_WRITABLE_PROPERTY("WfdSubelements",
				 "s",
				 link_dbus_get_wfd_subelements,
				 link_dbus_set_wfd_subelements,
				 0,
				 SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_VTABLE_END
};

static int link_dbus_find(sd_bus *bus,
			  const char *path,
			  const char *interface,
			  void *data,
			  void **found,
			  sd_bus_error *err)
{
	_shl_free_ char *label = NULL;
	struct manager *m = data;
	struct link *l;
	int r;

	r = sd_bus_path_decode(path,
			       "/org/freedesktop/miracle/wifi/link",
			       &label);
	if (r <= 0)
		return r;

	l = manager_find_link_by_label(m, label);
	if (!l || !l->public)
		return 0;

	*found = l;
	return 1;
}

void link_dbus_properties_changed(struct link *l, const char *prop, ...)
{
	_shl_free_ char *node = NULL;
	char **strv;
	int r;

	if (!l->public)
		return;

	node = link_dbus_get_path(l);
	if (!node)
		return;

	strv = strv_from_stdarg_alloca(prop);
	r = sd_bus_emit_properties_changed_strv(l->m->bus,
						node,
						"org.freedesktop.miracle.wifi.Link",
						strv);
	if (r < 0)
		log_vERR(r);
}

void link_dbus_added(struct link *l)
{
	_shl_free_ char *node = NULL;
	int r;

	node = link_dbus_get_path(l);
	if (!node)
		return;

	r = sd_bus_emit_interfaces_added(l->m->bus,
					 node,
					 /*
					 "org.freedesktop.DBus.Properties",
					 "org.freedesktop.DBus.Introspectable",
					 */
					 "org.freedesktop.miracle.wifi.Link",
					 NULL);
	if (r < 0)
		log_vERR(r);
}

void link_dbus_removed(struct link *l)
{
	_shl_free_ char *node = NULL;
	int r;

	node = link_dbus_get_path(l);
	if (!node)
		return;

	r = sd_bus_emit_interfaces_removed(l->m->bus,
					   node,
					   /*
					   "org.freedesktop.DBus.Properties",
					   "org.freedesktop.DBus.Introspectable",
					   */
					   "org.freedesktop.miracle.wifi.Link",
					   NULL);
	if (r < 0)
		log_vERR(r);
}

/*
 * Manager DBus
 */

static const sd_bus_vtable manager_dbus_vtable[] = {
	SD_BUS_VTABLE_START(0),
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
	size_t i, peer_cnt;
	char **nodes, *node;
	int r;

	peer_cnt = 0;
	MANAGER_FOREACH_LINK(l, m)
		if (l->public)
			peer_cnt += l->peer_cnt;

	nodes = malloc(sizeof(*nodes) * (m->link_cnt + peer_cnt + 2));
	if (!nodes)
		return log_ENOMEM();

	i = 0;
	MANAGER_FOREACH_LINK(l, m) {
		if (i >= m->link_cnt + peer_cnt) {
			log_warning("overflow: skipping link %s",
				    l->ifname);
			continue;
		}

		if (!l->public)
			continue;

		node = link_dbus_get_path(l);
		if (!node)
			goto error;

		nodes[i++] = node;

		LINK_FOREACH_PEER(p, l) {
			if (i >= m->link_cnt + peer_cnt) {
				log_warning("overflow: skipping peer %s",
					    p->p2p_mac);
				continue;
			}

			if (!p->public)
				continue;

			node = peer_dbus_get_path(p);
			if (!node)
				goto error;

			nodes[i++] = node;
		}
	}

	node = strdup("/org/freedesktop/miracle/wifi");
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

	r = sd_bus_add_object_vtable(m->bus, NULL,
				     "/org/freedesktop/miracle/wifi",
				     "org.freedesktop.miracle.wifi.Manager",
				     manager_dbus_vtable,
				     m);
	if (r < 0)
		goto error;

	r = sd_bus_add_node_enumerator(m->bus, NULL,
				       "/org/freedesktop/miracle/wifi",
				       manager_dbus_enumerate,
				       m);
	if (r < 0)
		goto error;

	r = sd_bus_add_fallback_vtable(m->bus, NULL,
				       "/org/freedesktop/miracle/wifi/link",
				       "org.freedesktop.miracle.wifi.Link",
				       link_dbus_vtable,
				       link_dbus_find,
				       m);
	if (r < 0)
		goto error;

	r = sd_bus_add_fallback_vtable(m->bus, NULL,
				       "/org/freedesktop/miracle/wifi/peer",
				       "org.freedesktop.miracle.wifi.Peer",
				       peer_dbus_vtable,
				       peer_dbus_find,
				       m);
	if (r < 0)
		goto error;

	r = sd_bus_add_object_manager(m->bus, NULL, "/org/freedesktop/miracle/wifi");
	if (r < 0)
		goto error;

	r = sd_bus_request_name(m->bus, "org.freedesktop.miracle.wifi", 0);
	if (r < 0) {
		log_error("cannot claim org.freedesktop.miracle.wifi bus-name: %d",
			  r);
		goto error_silent;
	}

	return 0;

error:
	log_vERR(r);
error_silent:
	manager_dbus_disconnect(m);
	return r;
}

void manager_dbus_disconnect(struct manager *m)
{
	if (!m || !m->bus)
		return;

	sd_bus_release_name(m->bus, "org.freedesktop.miracle.wifi");
}
