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
#include <libudev.h>
#include <stdbool.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include "shl_dlist.h"
#include "shl_htable.h"

#ifndef WIFID_H
#define WIFID_H

struct manager;
struct link;
struct peer;
struct supplicant;
struct supplicant_peer;

/* supplicant */

int supplicant_new(struct link *l,
		   struct supplicant **out);
void supplicant_free(struct supplicant *s);

int supplicant_start(struct supplicant *s);
void supplicant_stop(struct supplicant *s);
bool supplicant_is_running(struct supplicant *s);
bool supplicant_is_ready(struct supplicant *s);

int supplicant_set_friendly_name(struct supplicant *s, const char *name);
int supplicant_set_wfd_subelements(struct supplicant *s, const char *val);
int supplicant_p2p_start_scan(struct supplicant *s);
void supplicant_p2p_stop_scan(struct supplicant *s);
bool supplicant_p2p_scanning(struct supplicant *s);

/* supplicant peer */

const char *supplicant_peer_get_friendly_name(struct supplicant_peer *sp);
const char *supplicant_peer_get_interface(struct supplicant_peer *sp);
const char *supplicant_peer_get_local_address(struct supplicant_peer *sp);
const char *supplicant_peer_get_remote_address(struct supplicant_peer *sp);
const char *supplicant_peer_get_wfd_subelements(struct supplicant_peer *sp);
int supplicant_peer_connect(struct supplicant_peer *sp,
			    const char *prov_type,
			    const char *pin);
void supplicant_peer_disconnect(struct supplicant_peer *sp);

/* peer */

struct peer {
	struct link *l;
	char *p2p_mac;
	struct supplicant_peer *sp;

	bool public : 1;
	bool connected : 1;
};

#define peer_from_htable(_p) \
	shl_htable_entry((_p), struct peer, p2p_mac)

int peer_new(struct link *l,
	     const char *p2p_mac,
	     struct peer **out);
void peer_free(struct peer *p);

const char *peer_get_friendly_name(struct peer *p);
const char *peer_get_interface(struct peer *p);
const char *peer_get_local_address(struct peer *p);
const char *peer_get_remote_address(struct peer *p);
const char *peer_get_wfd_subelements(struct peer *p);
int peer_connect(struct peer *p, const char *prov, const char *pin);
void peer_disconnect(struct peer *p);

int peer_allow(struct peer *p);
void peer_reject(struct peer *p);

void peer_supplicant_started(struct peer *p);
void peer_supplicant_stopped(struct peer *p);
void peer_supplicant_friendly_name_changed(struct peer *p);
void peer_supplicant_wfd_subelements_changed(struct peer *p);
void peer_supplicant_provision_discovery(struct peer *p,
					 const char *prov,
					 const char *pin);
void peer_supplicant_go_neg_request(struct peer *p,
				    const char *prov,
				    const char *pin);
void peer_supplicant_formation_failure(struct peer *p, const char *reason);
void peer_supplicant_connected_changed(struct peer *p, bool connected);

_shl_sentinel_
void peer_dbus_properties_changed(struct peer *p, const char *prop, ...);
void peer_dbus_provision_discovery(struct peer *p,
				   const char *prov,
				   const char *pin);
void peer_dbus_go_neg_request(struct peer *p,
			      const char *type,
			      const char *pin);
void peer_dbus_formation_failure(struct peer *p, const char *reason);
void peer_dbus_added(struct peer *p);
void peer_dbus_removed(struct peer *p);

/* link */

struct link {
	struct manager *m;
	unsigned int ifindex;
	struct supplicant *s;

	char *ifname;
	char *friendly_name;
	char *wfd_subelements;
	char *config_methods;

	size_t peer_cnt;
	struct shl_htable peers;

	bool managed : 1;
	bool public : 1;
	bool use_dev : 1;
};

#define link_from_htable(_l) \
	shl_htable_entry((_l), struct link, ifindex)
#define LINK_FIRST_PEER(_l) \
	SHL_HTABLE_FIRST_MACRO(&(_l)->peers, peer_from_htable)
#define LINK_FOREACH_PEER(_i, _l) \
	SHL_HTABLE_FOREACH_MACRO(_i, &(_l)->peers, peer_from_htable)

struct peer *link_find_peer(struct link *l, const char *p2p_mac);
struct peer *link_find_peer_by_label(struct link *l, const char *label);

int link_new(struct manager *m,
	     unsigned int ifindex,
	     const char *ifname,
	     struct link **out);
void link_free(struct link *l);

/* workaround for the 'no ifname' issue */
void link_use_dev(struct link *l);
bool link_is_using_dev(struct link *l);

int link_set_managed(struct link *l, bool set);
bool link_get_managed(struct link *l);
int link_renamed(struct link *l, const char *ifname);

int link_set_config_methods(struct link *l, char *config_methods);
int link_set_friendly_name(struct link *l, const char *name);
const char *link_get_friendly_name(struct link *l);
int link_set_wfd_subelements(struct link *l, const char *val);
const char *link_get_wfd_subelements(struct link *l);
int link_set_p2p_scanning(struct link *l, bool set);
bool link_get_p2p_scanning(struct link *l);

void link_supplicant_started(struct link *l);
void link_supplicant_stopped(struct link *l);
void link_supplicant_p2p_scan_changed(struct link *l, bool new_value);

_shl_sentinel_
void link_dbus_properties_changed(struct link *l, const char *prop, ...);
void link_dbus_added(struct link *l);
void link_dbus_removed(struct link *l);

/* manager */

struct manager {
	sd_event *event;
	sd_bus *bus;
	sd_event_source *sigs[_NSIG];
	struct udev *udev;
	struct udev_monitor *udev_mon;
	sd_event_source *udev_mon_source;

	char *friendly_name;
	char *config_methods;

	size_t link_cnt;
	struct shl_htable links;
};

#define MANAGER_FIRST_LINK(_m) \
	SHL_HTABLE_FIRST_MACRO(&(_m)->links, link_from_htable)
#define MANAGER_FOREACH_LINK(_i, _m) \
	SHL_HTABLE_FOREACH_MACRO(_i, &(_m)->links, link_from_htable)

struct link *manager_find_link(struct manager *m, unsigned int ifindex);
struct link *manager_find_link_by_label(struct manager *m, const char *label);

/* dbus */

int manager_dbus_connect(struct manager *m);
void manager_dbus_disconnect(struct manager *m);

/* cli arguments */

extern unsigned int arg_wpa_loglevel;

#endif /* WIFID_H */
