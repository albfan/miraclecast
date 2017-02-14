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
#define LOG_SUBSYSTEM "wfdctl"

#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <systemd/sd-event.h>
#include <systemd/sd-journal.h>
#include <time.h>
#include <unistd.h>
#include "ctl.h"
#include "wfd.h"
#include "util.h"
#include "wfd-dbus.h"
#include "shl_macro.h"
#include "shl_htable.h"
#include "shl_util.h"
#include "shl_log.h"
#include "config.h"

static int ctl_wfd_init(struct ctl_wfd *wfd, sd_bus *bus);
static void ctl_wfd_free(struct ctl_wfd *wfd);

static struct ctl_wfd *wfd = NULL;
static struct wfd_dbus *wfd_dbus = NULL;

struct wfd_dbus * wfd_dbus_get()
{
	return wfd_dbus;
}

struct ctl_wfd * ctl_wfd_get()
{
	return wfd;
}

int ctl_wfd_new(struct ctl_wfd **out, sd_event *loop, sd_bus *bus)
{
	int r;
	struct ctl_wfd *wfd = calloc(1, sizeof(struct ctl_wfd));
	if(!wfd) {
		r = -ENOMEM;
		goto error;
	}

	shl_htable_init_str(&wfd->sinks);
	shl_htable_init_u64(&wfd->sessions);
	wfd->loop = sd_event_ref(loop);

	r = ctl_wfd_init(wfd, bus);
	if(0 > r) {
		goto error;
	}

	*out = wfd;

	return 0;

error:
	ctl_wfd_free(wfd);
	return r;
}

static void ctl_wfd_clear_sink(char **elem, void *ctx)
{
	if(*elem == ctx) {
		return;
	}
	wfd_sink_free(wfd_sink_from_htable(elem));
}

static void ctl_wfd_destroy(struct ctl_wfd *wfd)
{
	ctl_wifi_free(wfd->wifi);
	wfd->wifi = NULL;
	shl_htable_clear_str(&wfd->sinks, NULL, NULL);
	shl_htable_clear_u64(&wfd->sessions, NULL, NULL);
}

static void ctl_wfd_free(struct ctl_wfd *wfd)
{
	if(!wfd) {
		return;
	}

	ctl_wfd_destroy(wfd);

	if(wfd->loop) {
		sd_event_unref(wfd->loop);
	}

	free(wfd);
}

int ctl_wfd_add_sink(struct ctl_wfd *wfd,
				struct ctl_peer *p,
				union wfd_sube *sube,
				struct wfd_sink **out)
{
	_wfd_sink_free_ struct wfd_sink *s;
	int r = shl_htable_lookup_str(&wfd->sinks,
					p->label,
					NULL,
					NULL);
	if(r) {
		return -EEXIST;
	}

	r = wfd_sink_new(&s, p, sube);
	if(0 > r) {
		return r;
	}

	r = shl_htable_insert_str(&wfd->sinks,
					wfd_sink_to_htable(s),
					NULL);
	if(0 > r) {
		return r;
	}

	++wfd->n_sinks;
	*out = s;
	s = NULL;

	return 0;
}

int ctl_wfd_find_sink_by_label(struct ctl_wfd *wfd,
				const char *label,
				struct wfd_sink **out)
{
	char **entry;
	int r = shl_htable_lookup_str(&wfd->sinks, label, NULL, &entry);
	if(r) {
		*out = wfd_sink_from_htable(entry);
	}

	return r;
}

static int ctl_wfd_remove_sink_by_label(struct ctl_wfd *wfd,
				const char *label,
				struct wfd_sink **out)
{
	char **entry;
	int r = shl_htable_remove_str(&wfd->sinks, label, NULL, &entry);
	if(!r) {
		goto end;
	}

	--wfd->n_sinks;

	if(out) {
		*out = wfd_sink_from_htable(entry);
	}

end:
	return r;
}

uint64_t ctl_wfd_alloc_session_id(struct ctl_wfd *wfd)
{
	return ++wfd->id_pool;
}

int ctl_wfd_add_session(struct ctl_wfd *wfd, struct wfd_session *s)
{
	int r;

	assert(wfd);
	assert(s && wfd_session_get_id(s));
	assert(!ctl_wfd_find_session_by_id(wfd, wfd_session_get_id(s), NULL));

	r = shl_htable_insert_u64(&wfd->sessions, wfd_session_to_htable(s));
	if(0 > r) {
		return r;
	}

	++wfd->n_sessions;

	wfd_fn_session_new(s);

	return r;
}

int ctl_wfd_find_session_by_id(struct ctl_wfd *wfd,
				uint64_t id,
				struct wfd_session **out)
{
	uint64_t *entry;
	int r = shl_htable_lookup_u64(&wfd->sessions, id, &entry);
	if(r && out) {
		*out = wfd_session_from_htable(entry);
	}

	return r;
}

int ctl_wfd_remove_session_by_id(struct ctl_wfd *wfd,
				uint64_t id,
				struct wfd_session **out)
{
	uint64_t *entry;
	struct wfd_session *s;
	int r = shl_htable_remove_u64(&wfd->sessions, id, &entry);
	if(!r) {
		return 0;
	}

	--wfd->n_sessions;

	s = wfd_session_from_htable(entry);
	wfd_fn_session_free(s);
	if(out) {
		*out = s;
	}

	return 1;
}

static int ctl_wfd_fetch_info(sd_event_source *s, void *userdata)
{
	struct ctl_wfd *wfd = userdata;
	int r = ctl_wifi_fetch(wfd->wifi);
	if(0 > r) {
		log_warning("failed to fetch information about links and peers: %s",
						strerror(errno));
		sd_event_exit(wfd->loop, r);
	}

	return r;
}

static int ctl_wfd_handle_signal(sd_event_source *s,
				const struct signalfd_siginfo *si,
				void *userdata)
{
	struct ctl_wfd *wfd = userdata;
	ctl_wfd_destroy(wfd);

	return sd_event_exit(wfd->loop, 0);
}

static int ctl_wfd_init(struct ctl_wfd *wfd, sd_bus *bus)
{
	int i, r;
	const int signals[] = { SIGINT, SIGHUP, SIGQUIT, SIGTERM };
	struct ctl_wifi *wifi;

	for(i = 0; i < SHL_ARRAY_LENGTH(signals); i ++) {
		sigset_t mask;
		sigemptyset(&mask);
		sigaddset(&mask, signals[i]);
		r = sigprocmask(SIG_BLOCK, &mask, NULL);
		if(0 > r) {
			break;
		}

		r = sd_event_add_signal(wfd->loop,
						NULL,
						signals[i],
						ctl_wfd_handle_signal,
						wfd);
		if(0 > r) {
			break;
		}
	}

	r = ctl_wifi_new(&wifi, bus);
	if(0 > r) {
		r = -ENOMEM;
		goto end;
	}

	r = sd_event_add_defer(wfd->loop, NULL, ctl_wfd_fetch_info, wfd);
	if(0 > r) {
		ctl_wifi_free(wifi);
		goto end;
	}

	wfd->wifi = wifi;

end:
	return r;
}

int ctl_wfd_run(struct ctl_wfd *wfd)
{
	return sd_event_loop(wfd->loop);
}

/* Callbacks from ctl-src */
void wfd_fn_src_connected(struct wfd_src *s)
{
}

void wfd_fn_src_disconnected(struct wfd_src *s)
{
}

void wfd_fn_src_setup(struct wfd_src *s)
{
}

void wfd_fn_src_playing(struct wfd_src *s)
{
}

void ctl_fn_peer_new(struct ctl_peer *p)
{
	struct wfd_sink *s;
	union wfd_sube sube;
	int r = wfd_sube_parse_with_id(WFD_SUBE_ID_DEVICE_INFO,
					p->wfd_subelements,
					&sube);
	if(0 > r) {
		log_debug("invalid subelement: '%s'", p->wfd_subelements);
		return;
	}

	if(wfd_sube_device_is_sink(&sube)) {
		r = ctl_wfd_add_sink(ctl_wfd_get(), p, &sube, &s);
		if(0 > r) {
			log_warning("failed to add sink (%s, '%s'): %s",
							p->friendly_name,
							p->p2p_mac,
							strerror(errno));
			return;
		}

		r = wfd_fn_sink_new(s);
		if(0 > r) {
			log_warning("failed to publish newly added sink (%s): %s",
							wfd_sink_get_label(s),
							strerror(errno));
			return;
		}

		log_info("sink %s added", s->label);
	}
}

void ctl_fn_peer_free(struct ctl_peer *p)
{
	struct wfd_sink *s;
	_shl_free_ char *label = NULL;
	int r = ctl_wfd_remove_sink_by_label(wfd, p->label, &s);
	if(!r) {
		return;
	}

	label = strdup(s->label);

	r = wfd_fn_sink_free(s);
	if(0 > r) {
		log_warning("failed to unpublish removed sink (%s): %s",
						wfd_sink_get_label(s),
						strerror(errno));
	}

	wfd_sink_free(s);

	log_info("sink %s removed", label);
}

void ctl_fn_peer_provision_discovery(struct ctl_peer *p,
				const char *prov,
				const char *pin)
{
}

void ctl_fn_peer_go_neg_request(struct ctl_peer *p,
				const char *prov,
				const char *pin)
{
}

void ctl_fn_peer_formation_failure(struct ctl_peer *p, const char *reason)
{
}

void ctl_fn_peer_connected(struct ctl_peer *p)
{
}

void ctl_fn_peer_disconnected(struct ctl_peer *p)
{
}

void ctl_fn_link_new(struct ctl_link *l)
{
}

void ctl_fn_link_free(struct ctl_link *l)
{
}

void cli_fn_help()
{
}

int main(int argc, char **argv)
{
	int r;
	sd_event *loop;
	sd_bus *bus;

	setlocale(LC_ALL, "");

	if(getenv("LOG_LEVEL")) {
		log_max_sev = log_parse_arg(getenv("LOG_LEVEL"));
	}

	r = sd_event_default(&loop);
	if(0 > r) {
		goto end;
	}

	r = sd_bus_default_system(&bus);
	if(0 > r) {
		log_warning("unabled to connect to system DBus: %s", strerror(errno));
		goto unref_loop;
	}

	r = sd_bus_attach_event(bus, loop, 0);
	if(0 > r) {
		log_warning("unabled to attache DBus event source to loop: %s",
						strerror(errno));
		goto unref_bus;
	}

	r = ctl_wfd_new(&wfd, loop, bus);
	if(0 > r) {
		goto bus_detach_event;;
	}

	r = wfd_dbus_new(&wfd_dbus, loop, bus);
	if(0 > r) {
		goto free_ctl_wfd;
	}

	r = wfd_dbus_expose(wfd_dbus);
	if(0 > r) {
		log_warning("unabled to publish WFD service: %s", strerror(errno));
		goto free_ctl_wfd;
	}

	r = ctl_wfd_run(wfd);
	if(0 > r) {
		log_warning("unabled to keep WFD service running: %s", strerror(errno));
	}

	wfd_dbus_free(wfd_dbus);
	wfd_dbus = NULL;

free_ctl_wfd:
	ctl_wfd_free(wfd);
	wfd = NULL;
bus_detach_event:
	sd_bus_detach_event(bus);
unref_bus:
	sd_bus_unref(bus);
unref_loop:
	sd_event_unref(loop);
end:
	return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

