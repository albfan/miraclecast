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

#define LOG_SUBSYSTEM "peer"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include "miracled.h"
#include "miracled-wifi.h"
#include "shl_log.h"

int peer_make_name(unsigned int id, char **out)
{
	char buf[64] = { };
	char *name;

	if (!out)
		return 0;

	snprintf(buf, sizeof(buf) - 1, "%u", id);
	name = sd_bus_label_escape(buf);
	if (!name)
		return log_ENOMEM();

	*out = name;
	return 0;
}

static int peer_new(struct link *l, struct peer **out)
{
	unsigned int id;
	char *name;
	struct peer *p;
	int r;

	if (!l)
		return log_EINVAL();

	id = ++l->m->peer_ids;
	r = peer_make_name(id, &name);
	if (r < 0)
		return r;

	if (shl_htable_lookup_str(&l->m->peers, name, NULL, NULL)) {
		free(name);
		return -EALREADY;
	}

	log_debug("new peer: %s", name);

	p = calloc(1, sizeof(*p));
	if (!p) {
		free(name);
		return log_ENOMEM();
	}

	p->l = l;
	p->id = id;
	p->name = name;

	if (out)
		*out = p;

	return 0;
}

static int peer_link(struct peer *p)
{
	int r;

	r = shl_htable_insert_str(&p->l->m->peers, &p->name, NULL);
	if (r < 0)
		return log_ERR(r);

	++p->l->m->peer_cnt;
	shl_dlist_link(&p->l->peers, &p->list);
	peer_dbus_added(p);
	log_info("new peer: %s@%s", p->name, p->l->name);

	return 0;
}

int peer_new_wifi(struct link *l, struct wifi_dev *d, struct peer **out)
{
	int r;
	struct peer *p;

	r = peer_new(l, &p);
	if (r < 0)
		return r;

	p->d = d;
	wifi_dev_set_data(p->d, p);

	r = peer_link(p);
	if (r < 0)
		goto error;

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

	log_debug("free peer: %s", p->name);

	if (shl_htable_remove_str(&p->l->m->peers, p->name, NULL, NULL)) {
		log_info("remove managed peer: %s@%s", p->name, p->l->name);
		peer_dbus_removed(p);
		--p->l->m->peer_cnt;
		shl_dlist_unlink(&p->list);
	}

	wifi_dev_set_data(p->d, NULL);

	free(p->name);
	free(p);
}

const char *peer_get_friendly_name(struct peer *p)
{
	if (!p)
		return NULL;

	if (p->d)
		return wifi_dev_get_name(p->d);
	else
		return NULL;
}

bool peer_is_connected(struct peer *p)
{
	if (!p)
		return false;

	if (p->d)
		return wifi_dev_is_ready(p->d);
	else
		return false;
}

const char *peer_get_interface(struct peer *p)
{
	if (!p)
		return NULL;

	if (p->d)
		return wifi_dev_get_interface(p->d);
	else
		return NULL;
}

const char *peer_get_local_address(struct peer *p)
{
	if (!p)
		return NULL;

	if (p->d)
		return wifi_dev_get_local_address(p->d);
	else
		return NULL;
}

const char *peer_get_remote_address(struct peer *p)
{
	if (!p)
		return NULL;

	if (p->d)
		return wifi_dev_get_remote_address(p->d);
	else
		return NULL;
}

void peer_process_wifi(struct peer *p, struct wifi_event *ev)
{
	if (!p || !p->d)
		return;

	switch (ev->type) {
	case WIFI_DEV_PROVISION:
		break;
	case WIFI_DEV_CONNECT:
		peer_dbus_properties_changed(p, "Connected", NULL);
		break;
	case WIFI_DEV_DISCONNECT:
		peer_dbus_properties_changed(p, "Connected", NULL);
		break;
	default:
		log_debug("unhandled WIFI event: %u", ev->type);
		break;
	}
}
