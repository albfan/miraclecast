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

#define LOG_SUBSYSTEM "link"

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
 * Link Handling
 */

struct peer *link_find_peer(struct link *l, const char *p2p_mac)
{
	char **elem;
	bool res;

	res = shl_htable_lookup_str(&l->peers, p2p_mac, NULL, &elem);
	if (!res)
		return NULL;

	return peer_from_htable(elem);
}

struct peer *link_find_peer_by_label(struct link *l, const char *label)
{
	char mac[MAC_STRLEN];

	reformat_mac(mac, label);

	return link_find_peer(l, mac);
}

int link_new(struct manager *m,
	     unsigned int ifindex,
	     const char *ifname,
	     struct link **out)
{
	struct link *l;
	int r;

	if (!m || !ifindex || !ifname)
		return log_EINVAL();

	if (shl_htable_lookup_uint(&m->links, ifindex, NULL))
		return -EALREADY;

	log_debug("new link: %s (%u)", ifname, ifindex);

	l = calloc(1, sizeof(*l));
	if (!l)
		return log_ENOMEM();

	l->m = m;
	l->ifindex = ifindex;
	shl_htable_init_str(&l->peers);

	l->ifname = strdup(ifname);
	if (!l->ifname) {
		r = log_ENOMEM();
		goto error;
	}

	r = supplicant_new(l, &l->s);
	if (r < 0)
		goto error;

	r = shl_htable_insert_uint(&m->links, &l->ifindex);
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	++m->link_cnt;
	log_info("add link: %s", l->ifname);

	if (out)
		*out = l;

	l->public = true;
	link_dbus_added(l);
	return 0;

error:
	link_free(l);
	return r;
}

void link_free(struct link *l)
{
	if (!l)
		return;

	log_debug("free link: %s (%u)", l->ifname, l->ifindex);

	link_set_managed(l, false);

	link_dbus_removed(l);
	l->public = false;

	if (shl_htable_remove_uint(&l->m->links, l->ifindex, NULL)) {
		log_info("remove link: %s", l->ifname);
		--l->m->link_cnt;
	}

	supplicant_free(l->s);

	/* link_set_managed(l, false) already removed all peers */
	shl_htable_clear_str(&l->peers, NULL, NULL);

	free(l->wfd_subelements);
	free(l->friendly_name);
	free(l->ifname);
	free(l->config_methods);
	free(l);
}

void link_use_dev(struct link *l)
{
    l->use_dev = true;
}

bool link_is_using_dev(struct link *l)
{
    return l->use_dev;
}

int link_set_config_methods(struct link *l, char *config_methods)
{
   char *cm;

	if (!config_methods)
		return log_EINVAL();

	cm = strdup(config_methods);
	if (!cm)
		return log_ENOMEM();

	free(l->config_methods);
	l->config_methods = config_methods;
   return 0;
}

bool link_get_managed(struct link *l)
{
	return l->managed;
}

int link_set_managed(struct link *l, bool set)
{
	int r;

	if (!l)
		return log_EINVAL();
	if (l->managed == set)
		return 0;

	if (set) {
		log_info("manage link %s", l->ifname);

		r = supplicant_start(l->s);
		if (r < 0) {
			log_error("cannot start supplicant on %s", l->ifname);
			return -EFAULT;
		}
	} else {
		log_info("link %s no longer managed", l->ifname);
		supplicant_stop(l->s);
	}

	l->managed = set;
	return 0;
}

int link_renamed(struct link *l, const char *ifname)
{
	char *t;

	if (!l || !ifname)
		return log_EINVAL();
	if (!strcmp(l->ifname, ifname))
		return 0;

	log_info("link %s (%u) was renamed to %s",
		 l->ifname, l->ifindex, ifname);

	t = strdup(ifname);
	if (!t)
		return log_ENOMEM();

	free(l->ifname);
	l->ifname = t;

	link_dbus_properties_changed(l, "InterfaceName", NULL);

	return 0;
}

int link_set_friendly_name(struct link *l, const char *name)
{
	char *t;
	int r;

	if (!l || !name || !*name)
		return log_EINVAL();

	if (!l->managed)
		return log_EUNMANAGED();

	t = strdup(name);
	if (!t)
		return log_ENOMEM();

	if (supplicant_is_ready(l->s)) {
		r = supplicant_set_friendly_name(l->s, name);
		if (r < 0) {
			free(t);
			return r;
		}
	}

	free(l->friendly_name);
	l->friendly_name = t;
	link_dbus_properties_changed(l, "FriendlyName", NULL);

	return 0;
}

const char *link_get_friendly_name(struct link *l)
{
	if (!l)
		return NULL;

	return l->friendly_name;
}

int link_set_wfd_subelements(struct link *l, const char *val)
{
	char *t;
	int r;

	if (!l || !val)
		return log_EINVAL();

	if (!l->managed)
		return log_EUNMANAGED();

	t = strdup(val);
	if (!t)
		return log_ENOMEM();

	if (supplicant_is_ready(l->s)) {
		r = supplicant_set_wfd_subelements(l->s, val);
		if (r < 0) {
			free(t);
			return r;
		}
	}

	free(l->wfd_subelements);
	l->wfd_subelements = t;
	link_dbus_properties_changed(l, "WfdSubelements", NULL);

	return 0;
}

const char *link_get_wfd_subelements(struct link *l)
{
	if (!l)
		return NULL;

	return l->wfd_subelements;
}

int link_set_p2p_scanning(struct link *l, bool set)
{
	if (!l)
		return log_EINVAL();

	if (!l->managed)
		return log_EUNMANAGED();

	if (set) {
		return supplicant_p2p_start_scan(l->s);
	} else {
		supplicant_p2p_stop_scan(l->s);
		return 0;
	}
}

bool link_get_p2p_scanning(struct link *l)
{
	if (!l) {
		log_vEINVAL();
		return false;
	}

	if (!l->managed) {
		return false;
	}

	return supplicant_p2p_scanning(l->s);
}

void link_supplicant_started(struct link *l)
{
	if (!l || l->public)
		return;

   if (l->m->friendly_name && l->managed)
	   link_set_friendly_name(l, l->m->friendly_name);
	log_info("link %s managed", l->ifname);
}

void link_supplicant_stopped(struct link *l)
{
	if (!l || !l->public)
		return;

	log_info("link %s unmanaged", l->ifname);
}

void link_supplicant_p2p_scan_changed(struct link *l, bool new_value)
{
	if (!l)
		return;

	link_dbus_properties_changed(l, "P2PScanning", NULL);
}
