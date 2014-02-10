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

#define LOG_SUBSYSTEM "wifi"

#include <errno.h>
#include <libwfd.h>
#include <linux/un.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <systemd/sd-event.h>
#include <unistd.h>
#include "miracle.h"
#include "miracled-wifi.h"
#include "shl_dlist.h"
#include "shl_log.h"
#include "shl_util.h"

struct wifi {
	sd_event *event;
	wifi_event_t event_fn;
	void *data;
	char *reply_buf;
	size_t reply_buf_size;

	struct wfd_wpa_ctrl *wpa;
	sd_event_source *wpa_source;
	struct shl_dlist devs;

	bool discoverable : 1;
	bool hup : 1;
};

struct wifi_dev {
	unsigned long ref;
	void *data;

	struct shl_dlist list;
	struct wifi *w;

	char mac[WFD_WPA_EVENT_MAC_STRLEN];
	char pin[WIFI_PIN_STRLEN + 1];
	unsigned int provision;

	char *name;

	char *ifname;
	unsigned int role;

	int dhcp_comm;
	pid_t dhcp_pid;
	sd_event_source *dhcp_comm_source;
	sd_event_source *dhcp_pid_source;
	char *local_addr;
	char *remote_addr;

	bool public : 1;		/* is device known to caller? */
	bool connected : 1;		/* is device properly connected? */
};

static int wifi_dev_new(struct wifi *w, const char *mac,
			struct wifi_dev **out);
static void wifi_dev_lost(struct wifi_dev *d);
static int wifi_dev_start(struct wifi_dev *d, const char *ifname,
			  unsigned int role);
static void wifi_dev_stop(struct wifi_dev *d);

/*
 * Management Helpers
 */

static struct wifi_dev *wifi_find_dev_by_mac(struct wifi *w, const char *mac)
{
	struct shl_dlist *i;
	struct wifi_dev *d;

	shl_dlist_for_each(i, &w->devs) {
		d = shl_dlist_entry(i, struct wifi_dev, list);
		if (!strcasecmp(d->mac, mac))
			return d;
	}

	return NULL;
}

static struct wifi_dev *wifi_find_dev_by_ifname(struct wifi *w,
						const char *ifname)
{
	struct shl_dlist *i;
	struct wifi_dev *d;

	shl_dlist_for_each(i, &w->devs) {
		d = shl_dlist_entry(i, struct wifi_dev, list);
		if (d->ifname && !strcmp(d->ifname, ifname))
			return d;
	}

	return NULL;
}

static void wifi_raise(struct wifi *w, struct wifi_event *ev)
{
	w->event_fn(w, w->data, ev);
}

static void wifi_hup(struct wifi *w)
{
	struct wifi_event wev = { };

	if (!wifi_is_open(w))
		return;

	log_info("HUP on wpa_supplicant socket");
	wifi_close(w);
	wev.type = WIFI_HUP;
	wifi_raise(w, &wev);
}

static void wifi_show_dev(struct wifi *w, struct wifi_dev *d)
{
	struct wifi_event wev = { };

	if (d->public)
		return;
	d->public = true;

	wev.type = WIFI_DEV_FOUND;
	wev.dev_found.dev = d;
	wifi_raise(w, &wev);
}

static void wifi_hide_dev(struct wifi *w, struct wifi_dev *d)
{
	struct wifi_event wev = { };

	if (!d->public)
		return;
	d->public = false;

	wev.type = WIFI_DEV_LOST;
	wev.dev_lost.dev = d;
	wifi_raise(w, &wev);
}

static void wifi_pbc_req(struct wifi *w, struct wifi_dev *d)
{
	struct wifi_event wev = { };

	wev.type = WIFI_DEV_PROVISION;
	wev.dev_provision.dev = d;
	wev.dev_provision.type = WIFI_PROVISION_PBC;
	wifi_raise(w, &wev);
}

static void wifi_display_req(struct wifi *w, struct wifi_dev *d,
			     const char *pin)
{
	struct wifi_event wev = { };

	wev.type = WIFI_DEV_PROVISION;
	wev.dev_provision.dev = d;
	wev.dev_provision.type = WIFI_PROVISION_DISPLAY;
	strncpy(wev.dev_provision.pin, pin, WIFI_PIN_STRLEN);
	wifi_raise(w, &wev);
}

static void wifi_pin_req(struct wifi *w, struct wifi_dev *d)
{
	struct wifi_event wev = { };

	wev.type = WIFI_DEV_PROVISION;
	wev.dev_provision.dev = d;
	wev.dev_provision.type = WIFI_PROVISION_PIN;
	wifi_raise(w, &wev);
}

/*
 * WPA Queries
 */

static int wifi_request_ok(struct wifi *w, const char *req)
{
	return wfd_wpa_ctrl_request_ok(w->wpa, req, strlen(req), -1);
}

static int wifi_requestv_ok(struct wifi *w, const char *format, va_list args)
{
	int r;
	char *req;

	r = vasprintf(&req, format, args);
	if (r < 0)
		return log_ENOMEM();

	r = wifi_request_ok(w, req);
	free(req);
	return r;
}

static int wifi_requestf_ok(struct wifi *w, const char *format, ...)
{
	int r;
	va_list args;

	va_start(args, format);
	r = wifi_requestv_ok(w, format, args);
	va_end(args);
	return r;
}

static ssize_t wifi_request(struct wifi *w, const char *req)
{
	int r;
	size_t siz;

	siz = w->reply_buf_size;
	r = wfd_wpa_ctrl_request(w->wpa, req, strlen(req), w->reply_buf,
				 &siz, -1);
	if (r < 0)
		return r;

	return siz;
}

static ssize_t wifi_request_retry(struct wifi *w, const char *req)
{
	size_t siz;
	char *buf;

	do {
		siz = wifi_request(w, req);
		if (siz + 1 < w->reply_buf_size)
			return siz;

		/* try resizing the buffer for future requests */
		siz = w->reply_buf_size * 2;
		if (siz <= w->reply_buf_size)
			return log_ENOMEM();

		buf = realloc(w->reply_buf, siz);
		if (!buf)
			return log_ENOMEM();

		w->reply_buf = buf;
		w->reply_buf_size = siz;
	} while (true);
}

static ssize_t wifi_requestv_retry(struct wifi *w, const char *format,
				   va_list args)
{
	int r;
	char *req;
	ssize_t l;

	r = vasprintf(&req, format, args);
	if (r < 0)
		return log_ENOMEM();

	l = wifi_request_retry(w, req);
	free(req);
	return l;
}

static ssize_t wifi_requestf_retry(struct wifi *w, const char *format, ...)
{
	ssize_t l;
	va_list args;

	va_start(args, format);
	l = wifi_requestv_retry(w, format, args);
	va_end(args);
	return l;
}

static int wifi_parse_peer(struct wifi *w, size_t len, struct wifi_dev **out)
{
	char buf[512];
	char *pos, *next, *val;
	int x1, x2, x3, x4, x5, x6, r;
	struct wifi_dev *d;

	if (!strncmp(w->reply_buf, "FAIL\n", 5))
		return -EAGAIN;

	pos = strchr(w->reply_buf, '\n');
	if (pos)
		*pos = 0;

	/* verify mac-address */
	r = sscanf(w->reply_buf, "%2x:%2x:%2x:%2x:%2x:%2x",
		   &x1, &x2, &x3, &x4, &x5, &x6);
	if (r != 6) {
		log_debug("invalid P2P_PEER response: %s", w->reply_buf);
		return -EINVAL;
	}
	sprintf(buf, "%2x:%2x:%2x:%2x:%2x:%2x", x1, x2, x3, x4, x5, x6);

	d = wifi_find_dev_by_mac(w, buf);
	if (!d) {
		r = wifi_dev_new(w, buf, &d);
		if (r < 0)
			return r;
	}

	/* parse additional information */
	next = pos;
	while (next) {
		pos = ++next;
		next = strchr(pos, '\n');
		if (next)
			*next = 0;

		if ((val = shl_startswith(pos, "device_name="))) {
			val = strdup(val);
			if (val) {
				free(d->name);
				d->name = val;
			}
		}
	}

	*out = d;
	return 0;
}

static int wifi_read_peer(struct wifi *w, const char *mac,
			  struct wifi_dev **out)
{
	ssize_t l;

	l = wifi_requestf_retry(w, "P2P_PEER %s", mac);
	if (l < 0) {
		log_error("cannot issue P2P_PEER: %d", (int)l);
		return (int)l;
	}

	return wifi_parse_peer(w, l, out);
}

static int wifi_read_next_peer(struct wifi *w, const char *prev,
			       struct wifi_dev **out)
{
	ssize_t l;

	l = wifi_requestf_retry(w, "P2P_PEER NEXT-%s", prev);
	if (l < 0) {
		log_error("cannot issue P2P_PEER: %d", (int)l);
		return (int)l;
	}

	return wifi_parse_peer(w, l, out);
}

/*
 * WPA Event Parsers
 */

static void wifi_event_p2p_find_stopped(struct wifi *w, char *msg,
					struct wfd_wpa_event *ev)
{
	if (!w->discoverable)
		return;

	w->discoverable = false;
}

static void wifi_event_p2p_device_found(struct wifi *w, char *msg,
					struct wfd_wpa_event *ev)
{
	struct wifi_dev *d;
	int r;

	log_debug("received P2P-DEVICE-FOUND event: %s",
		  ev->p.p2p_device_found.peer_mac);

	r = wifi_read_peer(w, ev->p.p2p_device_found.peer_mac, &d);
	if (r < 0)
		return;

	wifi_show_dev(w, d);
}

static void wifi_event_p2p_device_lost(struct wifi *w, char *msg,
				       struct wfd_wpa_event *ev)
{
	struct wifi_dev *d;

	d = wifi_find_dev_by_mac(w, ev->p.p2p_device_lost.peer_mac);
	if (!d) {
		log_debug("stray P2P-DEVICE-LOST event: %s", msg);
		return;
	}

	log_debug("received P2P-DEVICE-LOST event: %s",
		  ev->p.p2p_device_lost.peer_mac);
	wifi_dev_lost(d);
	wifi_hide_dev(w, d);
	wifi_dev_unref(d);
}

static void wifi_event_p2p_prov_disc_pbc_req(struct wifi *w, char *msg,
					     struct wfd_wpa_event *ev)
{
	struct wifi_dev *d;

	d = wifi_find_dev_by_mac(w, ev->p.p2p_prov_disc_pbc_req.peer_mac);
	if (!d) {
		log_debug("stray P2P-PROV-DISC-PBC-REQ event: %s", msg);
		return;
	}

	log_debug("received P2P-PROV-DISC-PBC-REQ event: %s",
		  ev->p.p2p_prov_disc_pbc_req.peer_mac);

	d->pin[0] = 0;
	d->provision = WIFI_PROVISION_PBC,
	wifi_pbc_req(w, d);
}

static void wifi_event_p2p_prov_disc_show_pin(struct wifi *w, char *msg,
					      struct wfd_wpa_event *ev)
{
	struct wifi_dev *d;

	d = wifi_find_dev_by_mac(w, ev->p.p2p_prov_disc_show_pin.peer_mac);
	if (!d) {
		log_debug("stray P2P-PROV-DISC-SHOW-PIN event: %s", msg);
		return;
	}

	log_debug("received P2P-PROV-DISC-SHOW-PIN event: %s:%s",
		  ev->p.p2p_prov_disc_show_pin.pin,
		  ev->p.p2p_prov_disc_show_pin.peer_mac);

	strncpy(d->pin, ev->p.p2p_prov_disc_show_pin.pin, WIFI_PIN_STRLEN);
	d->provision = WIFI_PROVISION_DISPLAY,
	wifi_display_req(w, d, d->pin);
}

static void wifi_event_p2p_prov_disc_enter_pin(struct wifi *w, char *msg,
					       struct wfd_wpa_event *ev)
{
	struct wifi_dev *d;

	d = wifi_find_dev_by_mac(w, ev->p.p2p_prov_disc_enter_pin.peer_mac);
	if (!d) {
		log_debug("stray P2P-PROV-DISC-ENTER-PIN event: %s", msg);
		return;
	}

	log_debug("received P2P-PROV-DISC-ENTER-PIN event: %s",
		  ev->p.p2p_prov_disc_enter_pin.peer_mac);

	d->pin[0] = 0;
	d->provision = WIFI_PROVISION_PIN,
	wifi_pin_req(w, d);
}

static void wifi_event_p2p_go_neg_success(struct wifi *w, char *msg,
					  struct wfd_wpa_event *ev)
{
	struct wifi_dev *d;

	d = wifi_find_dev_by_mac(w, ev->p.p2p_go_neg_success.peer_mac);
	if (!d) {
		log_debug("stray P2P-GO-NEG-SUCCESS event: %s", msg);
		return;
	}

	log_debug("received P2P-GO-NEG-SUCCESS: %u:%s",
		  ev->p.p2p_go_neg_success.role, d->mac);
}

static void wifi_event_p2p_group_started(struct wifi *w, char *msg,
					 struct wfd_wpa_event *ev)
{
	struct wifi_dev *d;

	d = wifi_find_dev_by_mac(w, ev->p.p2p_group_started.go_mac);
	if (!d) {
		log_debug("stray P2P-GROUP-STARTED event: %s", msg);
		return;
	}

	log_debug("received P2P-GROUP-STARTED: %s:%u:%s",
		  ev->p.p2p_group_started.ifname,
		  ev->p.p2p_group_started.role, d->mac);

	if (d->ifname) {
		if (strcmp(d->ifname, ev->p.p2p_group_started.ifname))
			log_warning("ifname mismatch on group-starte: d.%s, e.%s",
				    d->ifname, ev->p.p2p_group_started.ifname);
		if (d->role != ev->p.p2p_group_started.role)
			log_warning("role mismatch on group-start: d.%u, e.%u",
				    d->role, ev->p.p2p_group_started.role);
		return;
	}

	wifi_dev_start(d, ev->p.p2p_group_started.ifname,
		       ev->p.p2p_group_started.role);
}

static void wifi_event_p2p_group_removed(struct wifi *w, char *msg,
					 struct wfd_wpa_event *ev)
{
	struct wifi_dev *d;

	d = wifi_find_dev_by_ifname(w, ev->p.p2p_group_removed.ifname);
	if (!d) {
		log_debug("stray P2P-GROUP-REMOVED event: %s", msg);
		return;
	}

	log_debug("received P2P-GROUP-REMOVED: %s:%u:%s",
		  ev->p.p2p_group_removed.ifname,
		  ev->p.p2p_group_removed.role, d->mac);

	if (d->role != ev->p.p2p_group_removed.role)
		log_warning("role mismatch on group-remove: d.%u, e.%u",
			    d->role, ev->p.p2p_group_removed.role);

	wifi_dev_stop(d);
}

static void wifi_event_ctrl_event_terminating(struct wifi *w, char *msg,
					      struct wfd_wpa_event *ev)
{
	log_debug("received CTRL-EVENT-TERMINATING");
	w->hup = true;
}

static void wifi_wpa_event_fn(struct wfd_wpa_ctrl *ctrl, void *data,
			      void *buf, size_t len)
{
	struct wifi *w = data;
	struct wfd_wpa_event ev;
	int r;

	wfd_wpa_event_init(&ev);
	r = wfd_wpa_event_parse(&ev, buf);
	if (r < 0) {
		log_error("cannot parse wpa-event (%d): %s", r, (char*)buf);
		return;
	}

	switch (ev.type) {
	case WFD_WPA_EVENT_P2P_FIND_STOPPED:
		wifi_event_p2p_find_stopped(w, buf, &ev);
		break;
	case WFD_WPA_EVENT_P2P_DEVICE_FOUND:
		wifi_event_p2p_device_found(w, buf, &ev);
		break;
	case WFD_WPA_EVENT_P2P_DEVICE_LOST:
		wifi_event_p2p_device_lost(w, buf, &ev);
		break;
	case WFD_WPA_EVENT_P2P_PROV_DISC_PBC_REQ:
		wifi_event_p2p_prov_disc_pbc_req(w, buf, &ev);
		break;
	case WFD_WPA_EVENT_P2P_PROV_DISC_SHOW_PIN:
		wifi_event_p2p_prov_disc_show_pin(w, buf, &ev);
		break;
	case WFD_WPA_EVENT_P2P_PROV_DISC_ENTER_PIN:
		wifi_event_p2p_prov_disc_enter_pin(w, buf, &ev);
		break;
	case WFD_WPA_EVENT_P2P_GO_NEG_SUCCESS:
		wifi_event_p2p_go_neg_success(w, buf, &ev);
		break;
	case WFD_WPA_EVENT_P2P_GROUP_STARTED:
		wifi_event_p2p_group_started(w, buf, &ev);
		break;
	case WFD_WPA_EVENT_P2P_GROUP_REMOVED:
		wifi_event_p2p_group_removed(w, buf, &ev);
		break;
	case WFD_WPA_EVENT_CTRL_EVENT_SCAN_STARTED:
		/* ignore */
		break;
	case WFD_WPA_EVENT_CTRL_EVENT_TERMINATING:
		wifi_event_ctrl_event_terminating(w, buf, &ev);
		break;
	case WFD_WPA_EVENT_UNKNOWN:
		/* fallthrough */
	default:
		log_debug("unhandled wpa-event: %s", (char*)buf);
		break;
	}

	wfd_wpa_event_reset(&ev);
}

static int wifi_wpa_fd_fn(struct sd_event_source *source, int fd,
			  uint32_t mask, void *data)
{
	struct wifi *w = data;
	int r;

	r = wfd_wpa_ctrl_dispatch(w->wpa, 0);
	if (r < 0) {
		log_debug("dispatching wpa_supplicant messages failed: %d", r);
		wifi_hup(w);
	} else if (w->hup) {
		wifi_hup(w);
	}

	return 0;
}

/*
 * Wifi Object Management
 */

int wifi_new(sd_event *event, wifi_event_t event_fn, void *data,
	     struct wifi **out)
{
	struct wifi *w;
	int r;

	if (!event || !event_fn || !out)
		return log_EINVAL();

	w = calloc(1, sizeof(*w));
	if (!w)
		return log_ENOMEM();

	shl_dlist_init(&w->devs);
	w->event = sd_event_ref(event);
	w->event_fn = event_fn;
	w->data = data;

	w->reply_buf_size = 4096;
	w->reply_buf = calloc(1, w->reply_buf_size);
	if (!w->reply_buf) {
		r = log_ENOMEM();
		goto error;
	}

	r = wfd_wpa_ctrl_new(wifi_wpa_event_fn, w, &w->wpa);
	if (r < 0)
		goto error;

	r = sd_event_add_io(w->event,
			    wfd_wpa_ctrl_get_fd(w->wpa),
			    EPOLLHUP | EPOLLERR | EPOLLIN,
			    wifi_wpa_fd_fn,
			    w,
			    &w->wpa_source);
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	*out = w;
	return 0;

error:
	wifi_free(w);
	return r;
}

void wifi_free(struct wifi *w)
{
	if (!w)
		return;

	wifi_close(w);
	sd_event_source_unref(w->wpa_source);
	wfd_wpa_ctrl_unref(w->wpa);
	free(w->reply_buf);
	sd_event_unref(w->event);
	free(w);
}

void wifi_set_data(struct wifi *w, void *data)
{
	if (!w)
		return;

	w->data = data;
}

void *wifi_get_data(struct wifi *w)
{
	return w ? w->data : NULL;
}

bool wifi_is_open(struct wifi *w)
{
	return w && wfd_wpa_ctrl_is_open(w->wpa);
}

static int wifi_read_all_peers(struct wifi *w)
{
	int r;
	struct wifi_dev *d = NULL;

	while (true) {
		if (!d)
			r = wifi_read_peer(w, "FIRST", &d);
		else
			r = wifi_read_next_peer(w, d->mac, &d);
		if (r < 0)
			break;

		d->public = true;
	}

	return (r == -EAGAIN) ? 0 : r;
}

int wifi_open(struct wifi *w, const char *wpa_path)
{
	int r;

	if (!w || !wpa_path)
		return log_EINVAL();
	if (wifi_is_open(w))
		return -EALREADY;

	log_debug("open wifi on: %s", wpa_path);

	r = wfd_wpa_ctrl_open(w->wpa, wpa_path);
	if (r < 0) {
		log_error("cannot open wpa_supplicant socket %s: %d",
			  wpa_path, r);
		goto error;
	}

	r = wifi_read_all_peers(w);
	if (r < 0)
		goto error;

	return 0;

error:
	wifi_close(w);
	return r;
}

void wifi_close(struct wifi *w)
{
	struct wifi_dev *d;

	if (!w)
		return;

	if (wfd_wpa_ctrl_is_open(w->wpa)) {
		log_debug("close wifi");
		wifi_set_discoverable(w, false);
	}

	while (!shl_dlist_empty(&w->devs)) {
		d = shl_dlist_first_entry(&w->devs, struct wifi_dev, list);
		wifi_dev_lost(d);
		wifi_dev_unref(d);
	}

	wfd_wpa_ctrl_close(w->wpa);
	w->wpa = NULL;
}

int wifi_set_discoverable(struct wifi *w, bool on)
{
	int r;

	if (!w || !wifi_is_open(w))
		return log_EINVAL();
	if (w->discoverable == on)
		return 0;

	if (on) {
		r = wifi_request_ok(w, "P2P_FIND");
		if (r < 0) {
			log_warning("cannot issue P2P_FIND: %d", r);
			return r;
		}
	} else {
		r = wifi_request_ok(w, "P2P_STOP_FIND");
		if (r < 0) {
			log_warning("cannot issue P2P_STOP_FIND: %d", r);
			return r;
		}
	}

	w->discoverable = on;
	return 0;
}

int wifi_set_name(struct wifi *w, const char *name)
{
	if (!w || !wifi_is_open(w) || !name || !*name)
		return log_EINVAL();

	return wifi_requestf_ok(w, "SET device_name %s", name);
}

struct wifi_dev *wifi_get_devs(struct wifi *w)
{
	if (!w || shl_dlist_empty(&w->devs))
		return NULL;

	return shl_dlist_first_entry(&w->devs, struct wifi_dev, list);
}

struct wifi_dev *wifi_dev_next(struct wifi_dev *d)
{
	if (!d || !d->w || d->list.next == &d->w->devs)
		return NULL;

	return shl_dlist_entry(d->list.next, struct wifi_dev, list);
}

/*
 * Wifi Device
 */

static int wifi_dev_new(struct wifi *w, const char *mac,
			struct wifi_dev **out)
{
	struct wifi_dev *d;

	log_debug("new device: %s", mac);

	d = calloc(1, sizeof(*d));
	if (!d)
		return log_ENOMEM();

	d->ref = 1;
	shl_dlist_link(&w->devs, &d->list);
	d->w = w;

	strncpy(d->mac, mac, sizeof(d->mac) - 1);

	d->pin[0] = 0;
	d->provision = WIFI_PROVISION_CNT;

	d->role = WFD_WPA_EVENT_ROLE_CNT;
	d->dhcp_comm = -1;

	*out = d;
	return 0;
}

static void wifi_dev_set_connected(struct wifi_dev *d, bool set, bool event)
{
	struct wifi_event wev = { };

	if (d->connected == set)
		return;
	if (!wifi_dev_is_running(d))
		return log_vEINVAL();

	if (set) {
		d->connected = true;

		wev.type = WIFI_DEV_CONNECT;
		wev.dev_connect.dev = d;
		if (event)
			wifi_raise(d->w, &wev);
	} else {
		d->connected = false;
		wev.type = WIFI_DEV_DISCONNECT;
		wev.dev_disconnect.dev = d;
		if (event)
			wifi_raise(d->w, &wev);
	}
}

static int wifi_dev_spawn_dhcp_client(struct wifi_dev *d)
{
	char *argv[64], loglevel[64], commfd[64];
	int i, r, fds[2];
	pid_t pid;
	sigset_t mask;

	r = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds);
	if (r < 0)
		return log_ERRNO();

	pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return log_ERRNO();
	} else if (!pid) {
		/* child */

		close(fds[0]);
		sprintf(loglevel, "%u", log_max_sev);
		sprintf(commfd, "%d", fds[1]);
		sigemptyset(&mask);
		sigprocmask(SIG_SETMASK, &mask, NULL);

		/* redirect stdout to stderr */
		dup2(2, 1);

		i = 0;
		argv[i++] = (char*) BUILD_BINDIR "/miracle-dhcp";
		argv[i++] = "--log-level";
		argv[i++] = loglevel;
		argv[i++] = "--netdev";
		argv[i++] = d->ifname;
		argv[i++] = "--comm-fd";
		argv[i++] = commfd;
		argv[i] = NULL;

		execve(argv[0], argv, environ);
		_exit(1);
	}

	close(fds[1]);
	d->dhcp_comm = fds[0];
	d->dhcp_pid = pid;

	return 0;
}

static int wifi_dev_comm_fn(sd_event_source *source, int fd, uint32_t mask,
			    void *data)
{
	struct wifi_dev *d = data;
	char buf[512], *t;
	ssize_t l;

	l = recv(fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
	if (l < 0) {
		l = -errno;
		if (l == -EAGAIN || l == -EINTR)
			return 0;

		log_vERRNO();
		goto error;
	} else if (!l) {
		log_error("HUP on dhcp comm socket");
		goto error;
	} else if (l > sizeof(buf) - 1) {
		l = sizeof(buf) - 1;
	}

	buf[l] = 0;
	log_debug("dhcp-comm: %s", buf);

	/* we only parse "X:<addr>" right now */
	if (l < 3 || buf[1] != ':' || !buf[2])
		return 0;

	t = strdup(&buf[2]);
	if (!t) {
		log_vENOMEM();
		return 0;
	}

	switch (buf[0]) {
	case 'L':
		free(d->local_addr);
		d->local_addr = t;
		break;
	case 'G':
		free(d->remote_addr);
		d->remote_addr = t;
		break;
	default:
		free(t);
		break;
	}

	if (d->local_addr && d->remote_addr) {
		/* got DHCP lease, connection is established */
		wifi_dev_set_connected(d, true, true);
	}

	return 0;

error:
	wifi_dev_stop(d);
	return 0;
}

static int wifi_dev_pid_fn(sd_event_source *source, const siginfo_t *info,
			   void *data)
{
	struct wifi_dev *d = data;

	log_error("DHCP client/server for %s died, stopping connection",
		  d->mac);
	wifi_dev_stop(d);

	return 0;
}

static int wifi_dev_start(struct wifi_dev *d, const char *ifname,
			  unsigned int role)
{
	int r;

	if (d->ifname)
		return 0;
	if (!ifname || role >= WFD_WPA_EVENT_ROLE_CNT)
		return log_EINVAL();

	d->ifname = strdup(ifname);
	if (!d->ifname)
		return log_ENOMEM();

	d->role = role;

	switch (d->role) {
	case WFD_WPA_EVENT_ROLE_GO:
		break;
	case WFD_WPA_EVENT_ROLE_CLIENT:
		r = wifi_dev_spawn_dhcp_client(d);
		if (r < 0) {
			log_error("cannot spawn DHCP client for: %s:%s",
				  ifname, d->mac);
			goto error;
		}
		break;
	default:
		log_error("unknown wpa-role: %u", d->role);
		goto error;
	}

	r = sd_event_add_io(d->w->event,
			    d->dhcp_comm,
			    EPOLLHUP | EPOLLERR | EPOLLIN,
			    wifi_dev_comm_fn,
			    d,
			    &d->dhcp_comm_source);
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	r = sd_event_add_child(d->w->event,
			       d->dhcp_pid,
			       WEXITED,
			       wifi_dev_pid_fn,
			       d,
			       &d->dhcp_pid_source);
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	return 0;

error:
	wifi_dev_stop(d);
	return r;
}

static void wifi_dev_stop(struct wifi_dev *d)
{
	int r;
	pid_t rp;

	if (!d->ifname)
		return;

	wifi_dev_set_connected(d, false, true);
	wifi_requestf_ok(d->w, "P2P_GROUP_REMOVE %s", d->ifname);

	free(d->local_addr);
	d->local_addr = NULL;
	free(d->remote_addr);
	d->remote_addr = NULL;

	if (d->dhcp_pid > 0) {
		sd_event_source_unref(d->dhcp_pid_source);
		d->dhcp_pid_source = NULL;

		log_debug("killing DHCP pid:%d and waiting for exit..",
			  d->dhcp_pid);
		r = kill(d->dhcp_pid, SIGTERM);
		if (r >= 0)
			rp = waitpid(d->dhcp_pid, NULL, 0);
		if (r < 0 || rp != d->dhcp_pid) {
			r = kill(d->dhcp_pid, SIGKILL);
			if (r >= 0)
				waitpid(d->dhcp_pid, &r, 0);
		}
		d->dhcp_pid = 0;
	}

	if (d->dhcp_comm >= 0) {
		sd_event_source_unref(d->dhcp_comm_source);
		d->dhcp_comm_source = NULL;
		close(d->dhcp_comm);
		d->dhcp_comm = -1;
	}

	free(d->ifname);
	d->ifname = NULL;
	d->role = WFD_WPA_EVENT_ROLE_CNT;
}

static void wifi_dev_lost(struct wifi_dev *d)
{
	if (!wifi_dev_is_available(d))
		return;

	log_debug("lost device: %s", d->mac);

	wifi_dev_stop(d);
	shl_dlist_unlink(&d->list);
	d->w = NULL;
}

void wifi_dev_ref(struct wifi_dev *d)
{
	if (!d || !d->ref)
		return;

	++d->ref;
}

void wifi_dev_unref(struct wifi_dev *d)
{
	if (!d || !d->ref || --d->ref)
		return;

	wifi_dev_set_connected(d, false, false);
	wifi_dev_lost(d);
	free(d->name);
	free(d);
}

void wifi_dev_set_data(struct wifi_dev *d, void *data)
{
	if (!d)
		return;

	d->data = data;
}

void *wifi_dev_get_data(struct wifi_dev *d)
{
	return d ? d->data : NULL;
}

bool wifi_dev_is_available(struct wifi_dev *d)
{
	return d && d->w;
}

bool wifi_dev_is_running(struct wifi_dev *d)
{
	return d && d->ifname;
}

bool wifi_dev_is_ready(struct wifi_dev *d)
{
	return d && d->connected;
}

void wifi_dev_allow(struct wifi_dev *d, const char *pin)
{
	int r;

	if (!wifi_dev_is_available(d) || wifi_dev_is_running(d))
		return;
	if (d->provision == WIFI_PROVISION_CNT)
		return;

	r = 0;
	switch (d->provision) {
	case WIFI_PROVISION_PBC:
		r = wifi_requestf_ok(d->w,
				     "P2P_CONNECT %s pbc display go_intent=0",
				     d->mac);
		break;
	case WIFI_PROVISION_DISPLAY:
		if (!*d->pin) {
			log_vEINVAL();
			break;
		}

		r = wifi_requestf_ok(d->w,
				     "P2P_CONNECT %s %s display go_intent=0",
				     d->mac, d->pin);
		break;
	case WIFI_PROVISION_PIN:
		if (!pin || !*pin) {
			log_vEINVAL();
			break;
		}

		r = wifi_requestf_ok(d->w,
				     "P2P_CONNECT %s %s display go_intent=0",
				     d->mac, pin);
		break;
	}

	if (r < 0)
		log_warning("cannot issue P2P_CONNECT on dev_allow(): %d", r);

	d->provision = WIFI_PROVISION_CNT;
}

void wifi_dev_reject(struct wifi_dev *d)
{
	if (!wifi_dev_is_available(d) || wifi_dev_is_running(d))
		return;
	if (d->provision == WIFI_PROVISION_CNT)
		return;

	wifi_request_ok(d->w, "P2P_CANCEL");
	d->provision = WIFI_PROVISION_CNT;
}

int wifi_dev_connect(struct wifi_dev *d, unsigned int provision,
		     const char *pin)
{
	if (!wifi_dev_is_available(d))
		return log_EINVAL();
	if (wifi_dev_is_running(d))
		return 0;

	return -EINVAL;
}

void wifi_dev_disconnect(struct wifi_dev *d)
{
	if (!wifi_dev_is_running(d))
		return;

	wifi_dev_set_connected(d, false, false);
	wifi_dev_stop(d);
}

const char *wifi_dev_get_name(struct wifi_dev *d)
{
	if (!d)
		return NULL;

	return d->name;
}

const char *wifi_dev_get_interface(struct wifi_dev *d)
{
	if (!wifi_dev_is_ready(d))
		return NULL;

	return d->ifname;
}

const char *wifi_dev_get_local_address(struct wifi_dev *d)
{
	if (!wifi_dev_is_ready(d))
		return NULL;

	return d->local_addr;
}

const char *wifi_dev_get_remote_address(struct wifi_dev *d)
{
	if (!wifi_dev_is_ready(d))
		return NULL;

	return d->remote_addr;
}
