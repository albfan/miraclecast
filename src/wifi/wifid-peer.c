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

#define LOG_SUBSYSTEM "peer"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include "shl_dlist.h"
#include "shl_log.h"
#include "shl_util.h"
#include "util.h"
#include "wifid.h"

/*
 * Peer Handling
 */

int peer_new(struct link *l,
	     const char *p2p_mac,
	     struct peer **out)
{
	char mac[MAC_STRLEN];
	struct peer *p;
	int r;

	if (!l || !p2p_mac)
		return log_EINVAL();

	reformat_mac(mac, p2p_mac);

	if (shl_htable_lookup_str(&l->peers, mac, NULL, NULL))
		return -EALREADY;

	log_debug("new peer: %s @ %s", mac, l->ifname);

	p = calloc(1, sizeof(*p));
	if (!p)
		return log_ENOMEM();

	p->l = l;
	p->p2p_mac = calloc(1, MAC_STRLEN);
	if (!p->p2p_mac) {
		r = log_ENOMEM();
		goto error;
	}
	strncpy(p->p2p_mac, mac, MAC_STRLEN - 1);

	r = shl_htable_insert_str(&l->peers, &p->p2p_mac, NULL);
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	++l->peer_cnt;
	log_info("add peer: %s", p->p2p_mac);

	if (out)
		*out = p;

	return 0;

error:
	peer_free(p);
	return r;
}

void peer_free(struct peer *p)
{
	if (!p)
		return;

	log_debug("free peer: %s @ %s", p->p2p_mac, p->l->ifname);

	if (shl_htable_remove_str(&p->l->peers, p->p2p_mac, NULL, NULL)) {
		log_info("remove peer: %s", p->p2p_mac);
		--p->l->peer_cnt;
	}

	free(p->p2p_mac);
	free(p);
}

const char *peer_get_friendly_name(struct peer *p)
{
	if (!p)
		return NULL;

	return supplicant_peer_get_friendly_name(p->sp);
}

const char *peer_get_interface(struct peer *p)
{
	if (!p || !p->connected)
		return NULL;

	return supplicant_peer_get_interface(p->sp);
}

const char *peer_get_local_address(struct peer *p)
{
	if (!p || !p->connected)
		return NULL;

	return supplicant_peer_get_local_address(p->sp);
}

const char *peer_get_remote_address(struct peer *p)
{
	if (!p || !p->connected)
		return NULL;

	return supplicant_peer_get_remote_address(p->sp);
}

const char *peer_get_wfd_subelements(struct peer *p)
{
	if (!p)
		return NULL;

	return supplicant_peer_get_wfd_subelements(p->sp);
}

int peer_connect(struct peer *p, const char *prov, const char *pin)
{
	if (!p)
		return log_EINVAL();

	return supplicant_peer_connect(p->sp, prov, pin);
}

void peer_disconnect(struct peer *p)
{
	if (!p)
		return log_vEINVAL();

	supplicant_peer_disconnect(p->sp);
}

void peer_supplicant_started(struct peer *p)
{
	if (!p || p->public)
		return;

	log_debug("peer %s @ %s started", p->p2p_mac, p->l->ifname);
	p->public = true;
	peer_dbus_added(p);
}

void peer_supplicant_stopped(struct peer *p)
{
	if (!p || !p->public)
		return;

	log_debug("peer %s @ %s stopped", p->p2p_mac, p->l->ifname);
	peer_dbus_removed(p);
	p->public = false;
}

void peer_supplicant_friendly_name_changed(struct peer *p)
{
	if (!p || !p->public)
		return;

	peer_dbus_properties_changed(p, "FriendlyName", NULL);
}

void peer_supplicant_wfd_subelements_changed(struct peer *p)
{
	if (!p || !p->public)
		return;

	peer_dbus_properties_changed(p, "WfdSubelements", NULL);
}

void peer_supplicant_provision_discovery(struct peer *p,
					 const char *prov,
					 const char *pin)
{
	if (!p || !p->public)
		return;

	peer_dbus_provision_discovery(p, prov, pin);
}

void peer_supplicant_go_neg_request(struct peer *p,
					 const char *prov,
					 const char *pin)
{
	if (!p || !p->public)
		return;

	peer_dbus_go_neg_request(p, prov, pin);
}

void peer_supplicant_formation_failure(struct peer *p,
					 const char *reason)
{
	if (!p || !p->public)
		return;

	peer_dbus_formation_failure(p, reason);
}

void peer_supplicant_connected_changed(struct peer *p, bool connected)
{
	if (!p || p->connected == connected)
		return;

	p->connected = connected;
	peer_dbus_properties_changed(p, "Connected",
					"Interface",
					"LocalAddress",
					"RemoteAddress",
					NULL);
}
