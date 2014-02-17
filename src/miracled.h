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

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include "miracle.h"
#include "shl_dlist.h"
#include "shl_htable.h"

#ifndef MIRACLED_H
#define MIRACLED_H

struct manager;
struct link;
struct peer;
struct wifi;
struct wifi_dev;
struct wifi_event;

/* peer */

struct peer {
	struct shl_dlist list;
	struct link *l;
	unsigned int id;
	char *name;

	struct wifi_dev *d;
};

#define peer_from_htable(_p) \
	shl_htable_offsetof((_p), struct peer, name)
#define peer_from_list(_p) \
	shl_dlist_entry((_p), struct peer, list)

int peer_make_name(unsigned int id, char **out);

int peer_new_wifi(struct link *l, struct wifi_dev *d, struct peer **out);
void peer_free(struct peer *p);

const char *peer_get_friendly_name(struct peer *p);
bool peer_is_connected(struct peer *p);
const char *peer_get_interface(struct peer *p);
const char *peer_get_local_address(struct peer *p);
const char *peer_get_remote_address(struct peer *p);

void peer_process_wifi(struct peer *p, struct wifi_event *ev);

int peer_allow(struct peer *p, const char *pin);
void peer_reject(struct peer *p);
int peer_connect(struct peer *p, const char *prov, const char *pin);
void peer_disconnect(struct peer *p);

/* link */

enum link_type {
	LINK_VIRTUAL,
	LINK_WIFI,
	LINK_CNT,
};

struct link {
	struct manager *m;
	unsigned int type;
	char *interface;
	char *name;
	char *friendly_name;

	struct shl_dlist peers;

	struct wifi *w;
};

#define link_from_htable(_l) \
	shl_htable_offsetof((_l), struct link, name)
#define LINK_FIRST_PEER(_l) (shl_dlist_empty(&(_l)->peers) ? \
	NULL : peer_from_list((_l)->peers.next))

const char *link_type_to_str(unsigned int type);
unsigned int link_type_from_str(const char *str);
int link_make_name(unsigned int type, const char *interface, char **out);

int link_new(struct manager *m,
	     unsigned int type,
	     const char *interface,
	     struct link **out);
void link_free(struct link *l);

int link_set_friendly_name(struct link *l, const char *name);

int link_start_scan(struct link *l);
void link_stop_scan(struct link *l);

/* manager */

struct manager {
	sd_event *event;
	sd_bus *bus;
	sd_event_source *sigs[_NSIG];

	unsigned int peer_ids;

	size_t link_cnt;
	size_t peer_cnt;
	struct shl_htable links;
	struct shl_htable peers;

	char *friendly_name;
};

#define MANAGER_FIRST_LINK(_m) \
	SHL_HTABLE_FIRST_MACRO(&(_m)->links, link_from_htable)
#define MANAGER_FOREACH_LINK(_i, _m) \
	SHL_HTABLE_FOREACH_MACRO(_i, &(_m)->links, link_from_htable)
#define MANAGER_FOREACH_PEER(_i, _m) \
	SHL_HTABLE_FOREACH_MACRO(_i, &(_m)->peers, peer_from_htable)

int manager_dbus_connect(struct manager *m);
void manager_dbus_disconnect(struct manager *m);

struct link *manager_find_link(struct manager *m, const char *name);
struct peer *manager_find_peer(struct manager *m, const char *name);

/* dbus */

void peer_dbus_provision_request(struct peer *p,
				 const char *type,
				 const char *pin);
_shl_sentinel_
void peer_dbus_properties_changed(struct peer *p, const char *prop, ...);
void peer_dbus_added(struct peer *p);
void peer_dbus_removed(struct peer *p);

_shl_sentinel_
void link_dbus_properties_changed(struct link *l, const char *prop, ...);
void link_dbus_scan_stopped(struct link *l);
void link_dbus_added(struct link *l);
void link_dbus_removed(struct link *l);

#endif /* MIRACLED_H */
