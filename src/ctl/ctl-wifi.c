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

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>
#include "ctl.h"
#include "shl_dlist.h"
#include "shl_macro.h"
#include "shl_util.h"
#include "util.h"

/*
 * Peers
 */

static void ctl_peer_free(struct ctl_peer *p)
{
	if (!p)
		return;

	if (shl_dlist_linked(&p->list))
		ctl_fn_peer_free(p);

	free(p->wfd_subelements);
	free(p->remote_address);
	free(p->local_address);
	free(p->interface);
	free(p->friendly_name);
	free(p->p2p_mac);

	shl_dlist_unlink(&p->list);
	free(p->label);
	free(p);
}

static void ctl_peer_free_p(struct ctl_peer **p)
{
	ctl_peer_free(*p);
}

#define _ctl_peer_free_ _shl_cleanup_(ctl_peer_free_p)

static int ctl_peer_new(struct ctl_peer **out,
			struct ctl_link *l,
			const char *label)
{
	struct ctl_peer *p;
	int r;

	if (!l || !label)
		return cli_EINVAL();

	p = calloc(1, sizeof(*p));
	if (!p)
		return cli_ENOMEM();

	p->l = l;

	p->label = strdup(label);
	if (!p->label) {
		r = cli_ENOMEM();
		goto error;
	}

	if (out)
		*out = p;

	return 0;

error:
	ctl_peer_free(p);
	return r;
}

static void ctl_peer_link(struct ctl_peer *p)
{
	if (!p || shl_dlist_linked(&p->list))
		return;

	shl_dlist_link_tail(&p->l->peers, &p->list);
	ctl_fn_peer_new(p);
}

static int ctl_peer_parse_properties(struct ctl_peer *p,
				     sd_bus_message *m)
{
	const char *t, *p2p_mac = NULL, *friendly_name = NULL;
	const char *interface = NULL, *local_address = NULL;
	const char *remote_address = NULL, *wfd_subelements = NULL;
	bool connected_set = false;
	char *tmp;
	int connected, r;

	if (!p || !m)
		return cli_EINVAL();

	r = sd_bus_message_enter_container(m, 'a', "{sv}");
	if (r < 0)
		return cli_log_parser(r);

	while ((r = sd_bus_message_enter_container(m,
						   'e',
						   "sv")) > 0) {
		r = sd_bus_message_read(m, "s", &t);
		if (r < 0)
			return cli_log_parser(r);

		if (!strcmp(t, "P2PMac")) {
			r = bus_message_read_basic_variant(m, "s", &p2p_mac);
			if (r < 0)
				return cli_log_parser(r);
		} else if (!strcmp(t, "FriendlyName")) {
			r = bus_message_read_basic_variant(m, "s",
							   &friendly_name);
			if (r < 0)
				return cli_log_parser(r);
		} else if (!strcmp(t, "Connected")) {
			r = bus_message_read_basic_variant(m, "b",
							   &connected);
			if (r < 0)
				return cli_log_parser(r);

			connected_set = true;
		} else if (!strcmp(t, "Interface")) {
			r = bus_message_read_basic_variant(m, "s",
							   &interface);
			if (r < 0)
				return cli_log_parser(r);
		} else if (!strcmp(t, "LocalAddress")) {
			r = bus_message_read_basic_variant(m, "s",
							   &local_address);
			if (r < 0)
				return cli_log_parser(r);
		} else if (!strcmp(t, "RemoteAddress")) {
			r = bus_message_read_basic_variant(m, "s",
							   &remote_address);
			if (r < 0)
				return cli_log_parser(r);
		} else if (!strcmp(t, "WfdSubelements")) {
			r = bus_message_read_basic_variant(m, "s",
							   &wfd_subelements);
			if (r < 0)
				return cli_log_parser(r);
		} else {
			sd_bus_message_skip(m, "v");
		}

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return cli_log_parser(r);
	}
	if (r < 0)
		return cli_log_parser(r);

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return cli_log_parser(r);

	if (p2p_mac) {
		tmp = strdup(p2p_mac);
		if (tmp) {
			free(p->p2p_mac);
			p->p2p_mac = tmp;
		} else {
			cli_vENOMEM();
		}
	}

	if (friendly_name) {
		tmp = strdup(friendly_name);
		if (tmp) {
			free(p->friendly_name);
			p->friendly_name = tmp;
		} else {
			cli_vENOMEM();
		}
	}

	if (interface) {
		tmp = strdup(interface);
		if (tmp) {
			free(p->interface);
			p->interface = tmp;
		} else {
			cli_vENOMEM();
		}
	}

	if (local_address) {
		tmp = strdup(local_address);
		if (tmp) {
			free(p->local_address);
			p->local_address = tmp;
		} else {
			cli_vENOMEM();
		}
	}

	if (remote_address) {
		tmp = strdup(remote_address);
		if (tmp) {
			free(p->remote_address);
			p->remote_address = tmp;
		} else {
			cli_vENOMEM();
		}
	}

	if (wfd_subelements) {
		tmp = strdup(wfd_subelements);
		if (tmp) {
			free(p->wfd_subelements);
			p->wfd_subelements = tmp;
		} else {
			cli_vENOMEM();
		}
	}

	/* do notifications last */
	if (connected_set && p->connected != connected) {
		p->connected = connected;
		if (p->connected)
			ctl_fn_peer_connected(p);
		else
			ctl_fn_peer_disconnected(p);
	}

	return 0;
}

int ctl_peer_connect(struct ctl_peer *p, const char *prov, const char *pin)
{
	_sd_bus_error_free_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_shl_free_ char *node = NULL;
	int r;

	if (!p)
		return cli_EINVAL();

	r = sd_bus_path_encode("/org/freedesktop/miracle/wifi/peer",
			       p->label,
			       &node);
	if (r < 0)
		return cli_ERR(r);

	r = sd_bus_call_method(p->l->w->bus,
			       "org.freedesktop.miracle.wifi",
			       node,
			       "org.freedesktop.miracle.wifi.Peer",
			       "Connect",
			       &err,
			       NULL,
			       "ss",
			       prov ? : "auto",
			       pin ? : "");
	if (r < 0) {
		cli_error("cannot connect peer %s: %s",
			  p->label, bus_error_message(&err, r));
		return r;
	}

	return 0;
}

int ctl_peer_disconnect(struct ctl_peer *p)
{
	_sd_bus_error_free_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_shl_free_ char *node = NULL;
	int r;

	if (!p)
		return cli_EINVAL();

	r = sd_bus_path_encode("/org/freedesktop/miracle/wifi/peer",
			       p->label,
			       &node);
	if (r < 0)
		return cli_ERR(r);

	r = sd_bus_call_method(p->l->w->bus,
			       "org.freedesktop.miracle.wifi",
			       node,
			       "org.freedesktop.miracle.wifi.Peer",
			       "Disconnect",
			       &err,
			       NULL,
			       NULL);
	if (r < 0) {
		cli_error("cannot disconnect peer %s: %s",
			  p->label, bus_error_message(&err, r));
		return r;
	}

	return 0;
}

/*
 * Links
 */

static struct ctl_peer *ctl_link_find_peer(struct ctl_link *l,
					   const char *label)
{
	struct shl_dlist *i;
	struct ctl_peer *p;

	shl_dlist_for_each(i, &l->peers) {
		p = peer_from_dlist(i);
		if (!strcasecmp(p->label, label))
			return p;
	}

	return NULL;
}

static void ctl_link_free(struct ctl_link *l)
{
	struct ctl_peer *p;

	if (!l)
		return;

	while (!shl_dlist_empty(&l->peers)) {
		p = shl_dlist_last_entry(&l->peers, struct ctl_peer, list);
		ctl_peer_free(p);
	}

	if (shl_dlist_linked(&l->list))
		ctl_fn_link_free(l);

	free(l->wfd_subelements);
	free(l->friendly_name);
	free(l->ifname);

	shl_dlist_unlink(&l->list);
	free(l->label);
	free(l);
}

static void ctl_link_free_p(struct ctl_link **l)
{
	ctl_link_free(*l);
}

#define _ctl_link_free_ _shl_cleanup_(ctl_link_free_p)

static int ctl_link_new(struct ctl_link **out,
			struct ctl_wifi *w,
			const char *label)
{
	struct ctl_link *l;
	int r;

	if (!w || !label)
		return cli_EINVAL();

	l = calloc(1, sizeof(*l));
	if (!l)
		return cli_ENOMEM();

	l->w = w;
	shl_dlist_init(&l->peers);

	l->label = strdup(label);
	if (!l->label) {
		r = cli_ENOMEM();
		goto error;
	}

	if (out)
		*out = l;

	return 0;

error:
	ctl_link_free(l);
	return r;
}

static void ctl_link_link(struct ctl_link *l)
{
	if (!l || shl_dlist_linked(&l->list))
		return;

	shl_dlist_link_tail(&l->w->links, &l->list);
	ctl_fn_link_new(l);
}

static int ctl_link_parse_properties(struct ctl_link *l,
				     sd_bus_message *m)
{
	const char *t, *interface_name = NULL, *friendly_name = NULL;
	const char *wfd_subelements = NULL;
	unsigned int interface_index = 0;
	bool p2p_scanning_set = false;
	char *tmp;
	int p2p_scanning, r;
	bool managed_set = false;
	int managed;

	if (!l || !m)
		return cli_EINVAL();

	r = sd_bus_message_enter_container(m, 'a', "{sv}");
	if (r < 0)
		return cli_log_parser(r);

	while ((r = sd_bus_message_enter_container(m,
						   'e',
						   "sv")) > 0) {
		r = sd_bus_message_read(m, "s", &t);
		if (r < 0)
			return cli_log_parser(r);

		if (!strcmp(t, "InterfaceIndex")) {
			r = bus_message_read_basic_variant(m, "u",
						&interface_index);
			if (r < 0)
				return cli_log_parser(r);
		} else if (!strcmp(t, "InterfaceName")) {
			r = bus_message_read_basic_variant(m, "s",
						&interface_name);
			if (r < 0)
				return cli_log_parser(r);
		} else if (!strcmp(t, "FriendlyName")) {
			r = bus_message_read_basic_variant(m, "s",
						&friendly_name);
			if (r < 0)
				return cli_log_parser(r);
		} else if (!strcmp(t, "Managed")) {
			r = bus_message_read_basic_variant(m, "b",
						&managed);
			if (r < 0)
				return cli_log_parser(r);

			managed_set = true;
		} else if (!strcmp(t, "P2PScanning")) {
			r = bus_message_read_basic_variant(m, "b",
						&p2p_scanning);
			if (r < 0)
				return cli_log_parser(r);

			p2p_scanning_set = true;
		} else if (!strcmp(t, "WfdSubelements")) {
			r = bus_message_read_basic_variant(m, "s",
						&wfd_subelements);
			if (r < 0)
				return cli_log_parser(r);
		} else {
			sd_bus_message_skip(m, "v");
		}

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return cli_log_parser(r);
	}
	if (r < 0)
		return cli_log_parser(r);

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return cli_log_parser(r);

	if (interface_index)
		l->ifindex = interface_index;

	if (interface_name) {
		tmp = strdup(interface_name);
		if (tmp) {
			free(l->ifname);
			l->ifname = tmp;
		} else {
			cli_vENOMEM();
		}
	}

	if (friendly_name) {
		tmp = strdup(friendly_name);
		if (tmp) {
			free(l->friendly_name);
			l->friendly_name = tmp;
		} else {
			cli_vENOMEM();
		}
	}

	if (managed_set)
		l->managed = managed;

	if (p2p_scanning_set)
		l->p2p_scanning = p2p_scanning;

	if (wfd_subelements) {
		tmp = strdup(wfd_subelements);
		if (tmp) {
			free(l->wfd_subelements);
			l->wfd_subelements = tmp;
		} else {
			cli_vENOMEM();
		}
	}

	return 0;
}

int ctl_link_set_friendly_name(struct ctl_link *l, const char *name)
{
	_sd_bus_message_unref_ sd_bus_message *m = NULL;
	_sd_bus_error_free_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_shl_free_ char *node = NULL;
	int r;

	if (!l)
		return cli_EINVAL();
	if (!strcmp(l->friendly_name, name))
		return 0;

	r = sd_bus_path_encode("/org/freedesktop/miracle/wifi/link",
			       l->label,
			       &node);
	if (r < 0)
		return cli_ERR(r);

	r = sd_bus_message_new_method_call(l->w->bus,
					   &m,
					   "org.freedesktop.miracle.wifi",
					   node,
					   "org.freedesktop.DBus.Properties",
					   "Set");
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_message_append(m, "ss",
				  "org.freedesktop.miracle.wifi.Link",
				  "FriendlyName");
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_message_open_container(m, 'v', "s");
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_message_append(m, "s", name);
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_message_close_container(m);
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_call(l->w->bus, m, 0, &err, NULL);
	if (r < 0) {
		cli_error("cannot change friendly-name on link %s to %s: %s",
			  l->label, name, bus_error_message(&err, r));
		return r;
	}

	return 0;
}

int ctl_link_set_wfd_subelements(struct ctl_link *l, const char *val)
{
	_sd_bus_message_unref_ sd_bus_message *m = NULL;
	_sd_bus_error_free_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_shl_free_ char *node = NULL;
	int r;

	if (!l)
		return cli_EINVAL();
	if (!strcmp(l->wfd_subelements, val))
		return 0;

	r = sd_bus_path_encode("/org/freedesktop/miracle/wifi/link",
			       l->label,
			       &node);
	if (r < 0)
		return cli_ERR(r);

	r = sd_bus_message_new_method_call(l->w->bus,
					   &m,
					   "org.freedesktop.miracle.wifi",
					   node,
					   "org.freedesktop.DBus.Properties",
					   "Set");
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_message_append(m, "ss",
				  "org.freedesktop.miracle.wifi.Link",
				  "WfdSubelements");
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_message_open_container(m, 'v', "s");
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_message_append(m, "s", val);
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_message_close_container(m);
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_call(l->w->bus, m, 0, &err, NULL);
	if (r < 0) {
		cli_error("cannot change WfdSubelements on link %s to %s: %s",
			  l->label, val, bus_error_message(&err, r));
		return r;
	}

	return 0;
}

int ctl_link_set_managed(struct ctl_link *l, bool val)
{
	_sd_bus_message_unref_ sd_bus_message *m = NULL;
	_sd_bus_error_free_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_shl_free_ char *node = NULL;
	int r;

	if (!l)
		return cli_EINVAL();
	if (l->managed == val)
		return 0;

	r = sd_bus_path_encode("/org/freedesktop/miracle/wifi/link",
			       l->label,
			       &node);
	if (r < 0)
		return cli_ERR(r);

	r = sd_bus_message_new_method_call(l->w->bus,
					   &m,
					   "org.freedesktop.miracle.wifi",
					   node,
					   "org.freedesktop.DBus.Properties",
					   "Set");
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_message_append(m, "ss",
				  "org.freedesktop.miracle.wifi.Link",
				  "Managed");
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_message_open_container(m, 'v', "b");
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_message_append(m, "b", val);
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_message_close_container(m);
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_call(l->w->bus, m, 0, &err, NULL);
	if (r < 0) {
		cli_error("cannot change managed state on link %s to %d: %s",
			  l->label, val, bus_error_message(&err, r));
		return r;
	}

	l->managed = val;

	return 0;
}

int ctl_link_set_p2p_scanning(struct ctl_link *l, bool val)
{
	_sd_bus_message_unref_ sd_bus_message *m = NULL;
	_sd_bus_error_free_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_shl_free_ char *node = NULL;
	int r;

	if (!l)
		return cli_EINVAL();
	if (l->p2p_scanning == val)
		return 0;

	r = sd_bus_path_encode("/org/freedesktop/miracle/wifi/link",
			       l->label,
			       &node);
	if (r < 0)
		return cli_ERR(r);

	r = sd_bus_message_new_method_call(l->w->bus,
					   &m,
					   "org.freedesktop.miracle.wifi",
					   node,
					   "org.freedesktop.DBus.Properties",
					   "Set");
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_message_append(m, "ss",
				  "org.freedesktop.miracle.wifi.Link",
				  "P2PScanning");
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_message_open_container(m, 'v', "b");
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_message_append(m, "b", val);
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_message_close_container(m);
	if (r < 0)
		return cli_log_create(r);

	r = sd_bus_call(l->w->bus, m, 0, &err, NULL);
	if (r < 0) {
		cli_error("cannot change p2p-scanning state on link %s to %d: %s",
			  l->label, val, bus_error_message(&err, r));
		return r;
	}

	/* Don't set l->p2p_scanning. We get a PropertiesChanged once the value
	 * has really changed. There is no synchronous way to get errors as the
	 * application couldn't deal with them, anyway. See the system-log if
	 * you want detailed information.
	 * However, mark the device as managed scan. This way, the app can stop
	 * scans during shutdown, if required. */

	if (val)
		l->have_p2p_scan = true;

	return 0;
}

/*
 * Wifi Management
 */

static int ctl_wifi_parse_link(struct ctl_wifi *w,
			       const char *label,
			       sd_bus_message *m)
{
	_ctl_link_free_ struct ctl_link *l = NULL;
	const char *t;
	int r;

	r = ctl_link_new(&l, w, label);
	if (r < 0)
		return r;

	r = sd_bus_message_enter_container(m, 'a', "{sa{sv}}");
	if (r < 0)
		return cli_log_parser(r);

	while ((r = sd_bus_message_enter_container(m,
						   'e',
						   "sa{sv}")) > 0) {
		r = sd_bus_message_read(m, "s", &t);
		if (r < 0)
			return cli_log_parser(r);

		if (strcmp(t, "org.freedesktop.miracle.wifi.Link")) {
			r = sd_bus_message_skip(m, "a{sv}");
			if (r < 0)
				return cli_log_parser(r);
			r = sd_bus_message_exit_container(m);
			if (r < 0)
				return cli_log_parser(r);
			continue;
		}

		r = ctl_link_parse_properties(l, m);
		if (r < 0)
			return r;

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return cli_log_parser(r);
	}
	if (r < 0)
		return cli_log_parser(r);

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return cli_log_parser(r);

	ctl_link_link(l);
	l = NULL;

	return 0;
}

static int ctl_wifi_parse_peer(struct ctl_wifi *w,
			       const char *label,
			       sd_bus_message *m)
{
	_ctl_peer_free_ struct ctl_peer *p = NULL;
	const char *t;
	struct ctl_link *l;
	int r;

	l = ctl_wifi_find_link_by_peer(w, label);
	if (!l)
		return cli_EINVAL();

	r = ctl_peer_new(&p, l, label);
	if (r < 0)
		return r;

	r = sd_bus_message_enter_container(m, 'a', "{sa{sv}}");
	if (r < 0)
		return cli_log_parser(r);

	while ((r = sd_bus_message_enter_container(m,
						   'e',
						   "sa{sv}")) > 0) {
		r = sd_bus_message_read(m, "s", &t);
		if (r < 0)
			return cli_log_parser(r);

		if (strcmp(t, "org.freedesktop.miracle.wifi.Peer")) {
			r = sd_bus_message_skip(m, "a{sv}");
			if (r < 0)
				return cli_log_parser(r);
			r = sd_bus_message_exit_container(m);
			if (r < 0)
				return cli_log_parser(r);
			continue;
		}

		r = ctl_peer_parse_properties(p, m);
		if (r < 0)
			return r;

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return cli_log_parser(r);
	}
	if (r < 0)
		return cli_log_parser(r);

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return cli_log_parser(r);

	ctl_peer_link(p);
	p = NULL;

	return 0;
}

static int ctl_wifi_parse_object(struct ctl_wifi *w,
				 sd_bus_message *m,
				 bool added)
{
	_shl_free_ char *label = NULL;
	struct ctl_link *l;
	struct ctl_peer *p;
	const char *t;
	int r;

	r = sd_bus_message_read(m, "o", &t);
	if (r < 0)
		return cli_log_parser(r);

	r = sd_bus_path_decode(t,
			       "/org/freedesktop/miracle/wifi/link",
			       &label);
	if (r < 0) {
		return cli_ENOMEM();
	} else if (r > 0) {
		l = ctl_wifi_find_link(w, label);
		if (!l && added) {
			return ctl_wifi_parse_link(w, label, m);
		} else if (l && !added) {
			/* We don't do any dynamic interfaces, so if any of
			 * them is removed, all of them are removed. */
			ctl_link_free(l);
		}
	}

	r = sd_bus_path_decode(t,
			       "/org/freedesktop/miracle/wifi/peer",
			       &label);
	if (r < 0) {
		return cli_ENOMEM();
	} else if (r > 0) {
		p = ctl_wifi_find_peer(w, label);
		if (!p && added) {
			return ctl_wifi_parse_peer(w, label, m);
		} else if (p && !added) {
			/* We don't do any dynamic interfaces, so if any of
			 * them is removed, all of them are removed. */
			ctl_peer_free(p);
		}
	}

	/* skip unhandled payload */
	if (added)
		r = sd_bus_message_skip(m, "a{sa{sv}}");
	else
		r = sd_bus_message_skip(m, "as");
	if (r < 0)
		return cli_log_parser(r);

	return 0;
}

static int ctl_wifi_object_fn(sd_bus_message *m,
			      void *data,
			      sd_bus_error *err)
{
	struct ctl_wifi *w = data;
	bool added;

	added = !strcmp(sd_bus_message_get_member(m), "InterfacesAdded");

	return ctl_wifi_parse_object(w, m, added);
}

static int ctl_wifi_properties_fn(sd_bus_message *m,
				  void *data,
				  sd_bus_error *err)
{
	_shl_free_ char *label = NULL;
	struct ctl_wifi *w = data;
	struct ctl_link *l;
	struct ctl_peer *p;
	const char *t;
	int r;

	if (!sd_bus_message_is_signal(m,
				      "org.freedesktop.DBus.Properties",
				      "PropertiesChanged"))
		return 0;

	t = sd_bus_message_get_path(m);
	if (!t)
		return cli_EINVAL();

	r = sd_bus_path_decode(t,
			       "/org/freedesktop/miracle/wifi/link",
			       &label);
	if (r < 0) {
		return cli_ENOMEM();
	} else if (r > 0) {
		l = ctl_wifi_find_link(w, label);
		if (!l)
			return 0;

		r = sd_bus_message_read(m, "s", &t);
		if (r < 0)
			return cli_log_parser(r);

		if (strcmp(t, "org.freedesktop.miracle.wifi.Link"))
			return 0;

		return ctl_link_parse_properties(l, m);
	}

	r = sd_bus_path_decode(t,
			       "/org/freedesktop/miracle/wifi/peer",
			       &label);
	if (r < 0) {
		return cli_ENOMEM();
	} else if (r > 0) {
		p = ctl_wifi_find_peer(w, label);
		if (!p)
			return 0;

		r = sd_bus_message_read(m, "s", &t);
		if (r < 0)
			return cli_log_parser(r);

		if (strcmp(t, "org.freedesktop.miracle.wifi.Peer"))
			return 0;

		return ctl_peer_parse_properties(p, m);
	}

	return 0;
}

static int ctl_wifi_peer_fn(sd_bus_message *m,
			    void *data,
			    sd_bus_error *err)
{
	_shl_free_ char *label = NULL;
	struct ctl_wifi *w = data;
	struct ctl_peer *p;
	const char *t;
	int r;

	t = sd_bus_message_get_path(m);
	if (!t)
		return cli_EINVAL();

	r = sd_bus_path_decode(t,
			       "/org/freedesktop/miracle/wifi/peer",
			       &label);
	if (r < 0) {
		return cli_ERR(r);
	} else if (r == 0) {
		return 0;
	} else if (r > 0) {
		p = ctl_wifi_find_peer(w, label);
		if (!p)
			return 0;
	}

	if (sd_bus_message_is_signal(m,
				     "org.freedesktop.miracle.wifi.Peer",
				     "ProvisionDiscovery")) {
		/* provision discovery */
		const char *prov, *pin;

		r = sd_bus_message_read(m, "ss", &prov, &pin);
		if (r < 0)
			return cli_log_parser(r);

		ctl_fn_peer_provision_discovery(p, prov, pin);
	} else if (sd_bus_message_is_signal(m,
				     "org.freedesktop.miracle.wifi.Peer",
				     "GoNegRequest")) {
		/* connection request */
		const char *prov, *pin;

		r = sd_bus_message_read(m, "ss", &prov, &pin);
		if (r < 0)
			return cli_log_parser(r);

		ctl_fn_peer_go_neg_request(p, prov, pin);
	} else if (sd_bus_message_is_signal(m,
					    "org.freedesktop.miracle.wifi.Peer",
					    "FormationFailure")) {
		/* group formation failure */
		const char *reason;

		r = sd_bus_message_read(m, "s", &reason);
		if (r < 0)
			return cli_log_parser(r);

		ctl_fn_peer_formation_failure(p, reason);
	}

	return 0;
}

static int ctl_wifi_init(struct ctl_wifi *w)
{
	int r;

	r = sd_bus_add_match(w->bus, NULL,
			     "type='signal',"
			     "sender='org.freedesktop.miracle.wifi',"
			     "interface='org.freedesktop.DBus.ObjectManager'",
			     ctl_wifi_object_fn,
			     w);
	if (r < 0)
		return r;

	r = sd_bus_add_match(w->bus, NULL,
			     "type='signal',"
			     "sender='org.freedesktop.miracle.wifi',"
			     "interface='org.freedesktop.DBus.Properties'",
			     ctl_wifi_properties_fn,
			     w);
	if (r < 0)
		return r;

	r = sd_bus_add_match(w->bus, NULL,
			     "type='signal',"
			     "sender='org.freedesktop.miracle.wifi',"
			     "interface='org.freedesktop.miracle.wifi.Peer'",
			     ctl_wifi_peer_fn,
			     w);
	if (r < 0)
		return r;

	return 0;
}

static void ctl_wifi_destroy(struct ctl_wifi *w)
{

}

int ctl_wifi_new(struct ctl_wifi **out, sd_bus *bus)
{
	struct ctl_wifi *w;
	int r;

	if (!out || !bus)
		return cli_EINVAL();

	w = calloc(1, sizeof(*w));
	if (!w)
		return cli_ENOMEM();

	w->bus = sd_bus_ref(bus);
	shl_dlist_init(&w->links);

	r = ctl_wifi_init(w);
	if (r < 0) {
		cli_error("cannot initialize wifi-dbus objects");
		ctl_wifi_free(w);
		return r;
	}

	*out = w;
	return 0;
}

void ctl_wifi_free(struct ctl_wifi *w)
{
	struct ctl_link *l;

	if (!w)
		return;

	while (!shl_dlist_empty(&w->links)) {
		l = shl_dlist_last_entry(&w->links, struct ctl_link, list);
		ctl_link_free(l);
	}

	ctl_wifi_destroy(w);
	sd_bus_unref(w->bus);
	free(w);
}

int ctl_wifi_fetch(struct ctl_wifi *w)
{
	_sd_bus_message_unref_ sd_bus_message *m = NULL;
	_sd_bus_error_free_ sd_bus_error err = SD_BUS_ERROR_NULL;
	int r;

	if (!w)
		return cli_EINVAL();

	r = sd_bus_call_method(w->bus,
			       "org.freedesktop.miracle.wifi",
			       "/org/freedesktop/miracle/wifi",
			       "org.freedesktop.DBus.ObjectManager",
			       "GetManagedObjects",
			       &err,
			       &m,
			       NULL);
	if (r < 0) {
		cli_error("cannot retrieve objects: %s",
			  bus_error_message(&err, r));
		return r;
	}

	r = sd_bus_message_enter_container(m, 'a', "{oa{sa{sv}}}");
	if (r < 0)
		return cli_log_parser(r);

	while ((r = sd_bus_message_enter_container(m,
						   'e',
						   "oa{sa{sv}}")) > 0) {
		r = ctl_wifi_parse_object(w, m, true);
		if (r < 0)
			return r;

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return cli_log_parser(r);
	}
	if (r < 0)
		return cli_log_parser(r);

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return cli_log_parser(r);

	return 0;
}

struct ctl_link *ctl_wifi_find_link(struct ctl_wifi *w,
				    const char *label)
{
	struct shl_dlist *i;
	struct ctl_link *l;

	if (!w || shl_isempty(label))
		return NULL;

	shl_dlist_for_each(i, &w->links) {
		l = link_from_dlist(i);
		if (!strcasecmp(l->label, label))
			return l;
	}

	return NULL;
}

struct ctl_link *ctl_wifi_search_link(struct ctl_wifi *w,
				      const char *label)
{
	struct shl_dlist *i;
	struct ctl_link *l;

	if (!w || shl_isempty(label))
		return NULL;

	l = ctl_wifi_find_link(w, label);
	if (l)
		return l;

	/* try matching on interface */
	shl_dlist_for_each(i, &w->links) {
		l = link_from_dlist(i);
		if (l->ifname && !strcasecmp(l->ifname, label))
			return l;
	}

	/* try matching on friendly-name */
	shl_dlist_for_each(i, &w->links) {
		l = link_from_dlist(i);
		if (l->friendly_name && !strcasecmp(l->friendly_name, label))
			return l;
	}

	return NULL;
}

struct ctl_link *ctl_wifi_find_link_by_peer(struct ctl_wifi *w,
					    const char *label)
{
	const char *sep;

	if (!w || shl_isempty(label))
		return NULL;

	sep = strchr(label, '@');
	if (!sep)
		return NULL;

	return ctl_wifi_find_link(w, sep + 1);
}

struct ctl_link *ctl_wifi_search_link_by_peer(struct ctl_wifi *w,
					      const char *label)
{
	const char *sep;

	if (!w || shl_isempty(label))
		return NULL;

	sep = strchr(label, '@');
	if (!sep)
		return NULL;

	return ctl_wifi_search_link(w, sep + 1);
}

struct ctl_peer *ctl_wifi_find_peer(struct ctl_wifi *w,
				    const char *label)
{
	struct ctl_link *l;

	if (!w || shl_isempty(label))
		return NULL;

	/* first try exact match */
	l = ctl_wifi_find_link_by_peer(w, label);
	if (!l)
		return NULL;

	return ctl_link_find_peer(l, label);
}

struct ctl_peer *ctl_wifi_search_peer(struct ctl_wifi *w,
				      const char *real_label)
{
	_shl_free_ char *label = NULL;
	struct shl_dlist *i, *j;
	struct ctl_link *l;
	struct ctl_peer *p;
	const char *next;
	char *sep;
	unsigned int cnt, idx;

	if (!w || shl_isempty(real_label))
		return NULL;

	label = strdup(real_label);
	if (!label) {
		cli_vENOMEM();
		return NULL;
	}

	p = ctl_wifi_find_peer(w, label);
	if (p)
		return p;

	l = ctl_wifi_search_link_by_peer(w, label);
	if (l) {
		sep = strchr(label, '@');
		if (sep)
			*sep = 0;

		shl_dlist_for_each(j, &l->peers) {
			p = shl_dlist_entry(j, struct ctl_peer, list);
			next = shl_startswith(p->label, label);
			if (next && *next == '@')
				return p;
		}

		shl_dlist_for_each(j, &l->peers) {
			p = shl_dlist_entry(j, struct ctl_peer, list);
			if (p->friendly_name &&
			    !strcasecmp(p->friendly_name, label))
				return p;
		}

		shl_dlist_for_each(j, &l->peers) {
			p = shl_dlist_entry(j, struct ctl_peer, list);
			if (p->interface &&
			    !strcasecmp(p->interface, label))
				return p;
		}

		if (shl_atoi_u(label, 10, &next, &idx) >= 0 && !*next) {
			cnt = 0;
			shl_dlist_for_each(j, &l->peers) {
				p = shl_dlist_entry(j, struct ctl_peer, list);
				if (cnt++ == idx)
					return p;
			}
		}

		if (sep)
			*sep = '@';
	}

	shl_dlist_for_each(i, &w->links) {
		l = shl_dlist_entry(i, struct ctl_link, list);
		shl_dlist_for_each(j, &l->peers) {
			p = shl_dlist_entry(j, struct ctl_peer, list);
			next = shl_startswith(p->label, label);
			if (next && *next == '@')
				return p;
		}
	}

	shl_dlist_for_each(i, &w->links) {
		l = shl_dlist_entry(i, struct ctl_link, list);
		shl_dlist_for_each(j, &l->peers) {
			p = shl_dlist_entry(j, struct ctl_peer, list);
			if (p->friendly_name &&
			    !strcasecmp(p->friendly_name, label))
				return p;
		}
	}

	shl_dlist_for_each(i, &w->links) {
		l = shl_dlist_entry(i, struct ctl_link, list);
		shl_dlist_for_each(j, &l->peers) {
			p = shl_dlist_entry(j, struct ctl_peer, list);
			if (p->interface &&
			    !strcasecmp(p->interface, label))
				return p;
		}
	}

	if (shl_atoi_u(label, 10, &next, &idx) < 0 || *next)
		return NULL;

	cnt = 0;
	shl_dlist_for_each(i, &w->links) {
		l = shl_dlist_entry(i, struct ctl_link, list);
		shl_dlist_for_each(j, &l->peers) {
			p = shl_dlist_entry(j, struct ctl_peer, list);
			if (cnt++ == idx)
				return p;
		}
	}

	return NULL;
}
