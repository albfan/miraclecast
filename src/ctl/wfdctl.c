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

void ctl_wfd_free(struct ctl_wfd *wfd);

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
		return -ENOMEM;
	}

	r = ctl_wifi_new(&wfd->wifi, bus);
	if(0 > r) {
		ctl_wfd_free(wfd);
		return -ENOMEM;
	}

	shl_htable_init_str(&wfd->sinks);
	shl_htable_init_u64(&wfd->sessions);
	wfd->loop = sd_event_ref(loop);

	*out = wfd;

	return 0;
}

static void ctl_wfd_clear_sink(char **elem, void *ctx)
{
	wfd_sink_free(wfd_sink_from_htable(elem));
}

static void ctl_wfd_clear_session(uint64_t *elem, void *ctx)
{
	wfd_session_free(wfd_session_from_htable(elem));
}

void ctl_wfd_free(struct ctl_wfd *wfd)
{
	if(!wfd) {
		return;
	}

	shl_htable_clear_str(&wfd->sinks, ctl_wfd_clear_sink, NULL);
	shl_htable_clear_u64(&wfd->sessions, ctl_wfd_clear_session, NULL);

	if(wfd->loop) {
		sd_event_unref(wfd->loop);
	}

	free(wfd);
}

static int ctl_wfd_add_sink(struct ctl_wfd *wfd, struct wfd_sink *sink)
{
	int r = shl_htable_lookup_str(&wfd->sinks,
					wfd_sink_get_label(sink),
					NULL,
					NULL);
	if(r) {
		return -EEXIST;
	}

	r = shl_htable_insert_str(&wfd->sinks,
					wfd_sink_to_htable(sink),
					NULL);
	if(0 <= r) {
		++wfd->n_sinks;
	}

	return r;
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

static int ctl_wfd_remove_sink_by_label(struct ctl_wfd *wfd, const char *label)
{
	char **entry;
	if(!shl_htable_remove_str(&wfd->sinks, label, NULL, &entry)) {
		return 0;
	}

	wfd_sink_free(wfd_sink_from_htable(entry));

	return 1;
}

int ctl_wfd_add_session(struct ctl_wfd *wfd, struct wfd_session *s)
{
	int r;

	assert(wfd);
	assert(s && !s->id);

	wfd_session_set_id(s, ++wfd->id_pool);

	r = shl_htable_insert_u64(&wfd->sessions, wfd_session_to_htable(s));
	if(0 > r) {
		return r;
	}

	++wfd->n_sessions;

	return r;
}

int ctl_wfd_find_session_by_id(struct ctl_wfd *wfd,
				unsigned int id,
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
	int r = shl_htable_remove_u64(&wfd->sessions, id, &entry);
	if(r && out) {
		*out = wfd_session_from_htable(entry);
	}

	return r;
}

static int ctl_wfd_handle_signal(sd_event_source *s,
				const struct signalfd_siginfo *si,
				void *userdata)
{
	struct ctl_wfd *wfd = userdata;
	sd_event_exit(wfd->loop, 0);

	return 0;
}

int ctl_wfd_init(struct ctl_wfd *wfd)
{
	int i;
	const int signals[] = { SIGINT, SIGHUP, SIGQUIT, SIGTERM };
	int r = ctl_wifi_fetch(wfd->wifi);
	if(0 > r) {
		goto end;
	}

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
	union wfd_sube sube;
	int r = wfd_sube_parse(p->wfd_subelements, &sube);
	if(0 > r) {
		log_debug("invalid subelement: '%s'", p->wfd_subelements);
		return;
	}

	if(wfd_sube_device_is_sink(&sube)) {
		struct wfd_sink *sink;
		if(0 > wfd_sink_new(&sink, p, &sube)) {
			log_warning("failed to create sink (%s): %s",
							p->p2p_mac,
							strerror(errno));
			return;
		}
		if(0 > ctl_wfd_add_sink(wfd, sink)) {
			wfd_sink_free(sink);
			log_warning("failed to add sink (%s): %s",
							p->p2p_mac,
							strerror(errno));
			return;
		}
		/*if(0 > wfd_dbus_notify_new_sink(wfd_dbus, p->p2p_mac)) {*/
			/*log_warning("failed to notify about newly added sink (%s): %s",*/
							/*p->p2p_mac,*/
							/*strerror(errno));*/
			/*return;*/
		/*}*/
		log_debug("sink added: %s (%s)",
						wfd_sink_get_label(sink),
						p->friendly_name);
	}
}

void ctl_fn_peer_free(struct ctl_peer *p)
{
	union wfd_sube sube;
	int r = wfd_sube_parse(p->wfd_subelements, &sube);
	if(0 > r) {
		log_debug("invalid subelement: %s", p->wfd_subelements);
		return;
	}

	if(wfd_sube_device_is_sink(&sube)) {
		ctl_wfd_remove_sink_by_label(wfd, p->label);
	}
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
		goto unref_loop;
	}

	r = sd_bus_attach_event(bus, loop, 0);
	if(0 > r) {
		goto unref_bus;
	}

	r = ctl_wfd_new(&wfd, loop, bus);
	if(0 > r) {
		goto bus_detach_event;;
	}

	r = ctl_wfd_init(wfd);
	if(0 > r) {
		goto free_ctl_wfd;
	}

	r = wfd_dbus_new(&wfd_dbus, loop, bus);
	if(0 > r) {
		goto free_ctl_wfd;
	}

	r = wfd_dbus_expose(wfd_dbus);
	if(0 > r) {
		goto free_ctl_wfd;
	}

	r = ctl_wfd_run(wfd);
	if(0 > r) {
		log_warning("%s\n", strerror(errno));
	}

	wfd_dbus_free(wfd_dbus);

free_ctl_wfd:
	ctl_wfd_free(wfd);
bus_detach_event:
	sd_bus_detach_event(bus);
unref_bus:
	sd_bus_unref(bus);
unref_loop:
	sd_event_unref(loop);
end:
	return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

