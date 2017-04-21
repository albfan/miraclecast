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
#define LOG_SUBSYSTEM "dispd"

#include <locale.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <systemd/sd-event.h>
#include <systemd/sd-daemon.h>
#include "ctl.h"
#include "disp.h"
#include "wfd.h"
#include "wfd-dbus.h"
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
	shl_htable_init_uint(&wfd->sessions);
	wfd->loop = sd_event_ref(loop);

	r = ctl_wfd_init(wfd, bus);
	if(0 > r) {
		goto error;
	}

	*out = wfd;

	return 0;

error:
	ctl_wfd_free(wfd);
	return log_ERRNO();
}

static void ctl_wfd_free(struct ctl_wfd *wfd)
{
	if(!wfd) {
		return;
	}

	ctl_wifi_free(wfd->wifi);
	wfd->wifi = NULL;
	shl_htable_clear_str(&wfd->sinks, NULL, NULL);
	shl_htable_clear_uint(&wfd->sessions, NULL, NULL);

	if(wfd->loop) {
		sd_event_unref(wfd->loop);
	}

	free(wfd);
}

static int ctl_wfd_handle_shutdown(sd_event_source *s,
				uint64_t usec,
				void *userdata)
{
	struct ctl_wfd *wfd = userdata;

	sd_event_exit(wfd->loop, 0);

	return 0;
}

void ctl_wfd_shutdown(struct ctl_wfd *wfd)
{
	uint64_t now;
	int r = sd_event_now(ctl_wfd_get_loop(), CLOCK_MONOTONIC, &now);
	if(0 > r) {
		goto error;
	}

	r = sd_event_add_time(ctl_wfd_get_loop(),
					NULL,
					CLOCK_MONOTONIC,
					now + 100 * 1000,
					0,
					ctl_wfd_handle_shutdown,
					wfd);
	if(0 <= r) {
		return;
	}

error:
	sd_event_exit(wfd->loop, 0);
}

int ctl_wfd_add_sink(struct ctl_wfd *wfd,
				struct ctl_peer *p,
				union wfd_sube *sube,
				struct wfd_sink **out)
{
	_wfd_sink_free_ struct wfd_sink *s = NULL;
	int r = shl_htable_lookup_str(&wfd->sinks,
					p->label,
					NULL,
					NULL);
	if(r) {
		return -EEXIST;
	}

	r = wfd_sink_new(&s, p, sube);
	if(0 > r) {
		return log_ERRNO();
	}

	r = shl_htable_insert_str(&wfd->sinks,
					wfd_sink_to_htable(s),
					NULL);
	if(0 > r) {
		return log_ERRNO();
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
	if(r && out) {
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

unsigned int ctl_wfd_alloc_session_id(struct ctl_wfd *wfd)
{
	return ++wfd->id_pool;
}

int ctl_wfd_add_session(struct ctl_wfd *wfd, struct wfd_session *s)
{
	int r;

	assert(wfd);
	assert(s && wfd_session_get_id(s));
	assert(!ctl_wfd_find_session_by_id(wfd, wfd_session_get_id(s), NULL));

	r = shl_htable_insert_uint(&wfd->sessions, wfd_session_to_htable(s));
	if(0 > r) {
		return log_ERRNO();
	}

	++wfd->n_sessions;

	wfd_fn_session_new(s);

	return 0;
}

int ctl_wfd_find_session_by_id(struct ctl_wfd *wfd,
				unsigned int id,
				struct wfd_session **out)
{
	unsigned int *entry;
	int r = shl_htable_lookup_uint(&wfd->sessions, id, &entry);
	if(r && out) {
		*out = wfd_session_from_htable(entry);
	}

	return r;
}

int ctl_wfd_remove_session_by_id(struct ctl_wfd *wfd,
				unsigned int id,
				struct wfd_session **out)
{
	unsigned int *entry;
	struct wfd_session *s;
	int r = shl_htable_remove_uint(&wfd->sessions, id, &entry);
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
	int r;

	sd_event_source_unref(s);
   
	r = ctl_wifi_fetch(wfd->wifi);
	if(0 > r) {
		log_warning("failed to fetch information about links and peers: %s",
						strerror(errno));
		sd_event_exit(wfd->loop, r);
	}

	return r;
}

static int ctl_wfd_handle_signal(sd_event_source *s,
				const struct signalfd_siginfo *ssi,
				void *userdata)
{
	int r;
	siginfo_t siginfo;
	struct ctl_wfd *wfd = userdata;

	if(ssi->ssi_signo == SIGCHLD) {
		r = waitid(P_PID, ssi->ssi_pid, &siginfo, WNOHANG | WEXITED);
		if(0 > r) {
			log_warning("failed to reaping child %d", ssi->ssi_pid);
		}
		else {
			log_info("child %d exit: %d",
							ssi->ssi_pid,
							siginfo.si_code);
		}
		return 0;
	}

	return sd_event_exit(wfd->loop, 0);
}

static int ctl_wfd_init(struct ctl_wfd *wfd, sd_bus *bus)
{
	int i, r;
	const int signals[] = {
		SIGINT, SIGHUP, SIGQUIT, SIGTERM, SIGCHLD,
	};
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
		log_vENOMEM();
		goto end;
	}

	r = sd_event_add_defer(wfd->loop, NULL, ctl_wfd_fetch_info, wfd);
	if(0 > r) {
		log_vERRNO();

		ctl_wifi_free(wifi);
		goto end;
	}

	wfd->wifi = wifi;

end:
	return r;
}

void ctl_fn_peer_new(struct ctl_peer *p)
{
	struct wfd_sink *s;
	union wfd_sube sube;
    const char *sube_str;
	int r;

	log_debug("new peer %s (%s) shows up, wfd_subelems: '%s'",
					p->friendly_name,
					p->label,
					p->wfd_subelements);

	if(p->wfd_subelements && *p->wfd_subelements) {
		sube_str = p->wfd_subelements;
	}
	else {
		sube_str = "000600111c4400c8";
		log_info("peer %s has no wfd_subelems, assume %s",
						p->label,
						sube_str);
	}

	r = wfd_sube_parse(sube_str, &sube);
	if(0 > r) {
		log_debug("peer %s has invalid subelement", p->label);
		return log_vERRNO();
	}

	if(wfd_sube_device_is_sink(&sube)) {
		r = ctl_wfd_add_sink(ctl_wfd_get(), p, &sube, &s);
		if(0 > r) {
			log_warning("failed to add sink (%s, '%s'): %s",
							p->friendly_name,
							p->p2p_mac,
							strerror(errno));
			return log_vERRNO();
		}

		r = wfd_fn_sink_new(s);
		if(0 > r) {
			log_warning("failed to publish newly added sink (%s): %s",
							wfd_sink_get_label(s),
							strerror(errno));
			return log_vERRNO();
		}

		log_info("sink %s added", s->label);
	}

	if(wfd_sube_device_is_source(&sube)) {
		log_info("source %s ignired", p->label);
	}
}

void ctl_fn_peer_free(struct ctl_peer *p)
{
	struct wfd_sink *s;
	int r;

	r = ctl_wfd_remove_sink_by_label(wfd, p->label, &s);
	if(r) {
		wfd_fn_sink_free(s);
		log_info("sink %s removed", s->label);
		wfd_sink_free(s);
	}

	log_info("peer %s down", p->label);
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
	sd_event *event;
	sd_bus *bus;

	setlocale(LC_ALL, "");
	setlocale(LC_TIME, "en_US.UTF-8");

	if(getenv("LOG_LEVEL")) {
		log_max_sev = log_parse_arg(getenv("LOG_LEVEL"));
	}

	r = sd_event_default(&event);
	if(0 > r) {
		log_warning("can't create default event loop");
		goto end;
	}

	r = sd_event_set_watchdog(event, true);
	if (0 > r) {
		log_warning("unable to start automatic watchdog support: %s", strerror(errno));
		goto unref_event;
	}

	r = sd_bus_default_system(&bus);
	if(0 > r) {
		log_warning("unable to connect to system DBus: %s", strerror(errno));
		goto disable_watchdog;
	}

	r = sd_bus_attach_event(bus, event, 0);
	if(0 > r) {
		goto unref_bus;
	}

	r = wfd_dbus_new(&wfd_dbus, event, bus);
	if(0 > r) {
		goto bus_detach_event;
	}

	r = ctl_wfd_new(&wfd, event, bus);
	if(0 > r) {
		goto free_wfd_dbus;
	}

	r = wfd_dbus_expose(wfd_dbus);
	if(0 > r) {
		log_warning("unable to publish WFD service: %s", strerror(errno));
		goto free_ctl_wfd;
	}

	r = sd_notify(false, "READY=1\n"
			     "STATUS=Running..");
	if (0 > r) {
		log_warning("unable to notify systemd that we are ready: %s", strerror(errno));
		goto free_wfd_dbus;
	}

	sd_event_loop(event);

	sd_notify(false, "STATUS=Exiting..");

free_ctl_wfd:
	ctl_wfd_free(wfd);
free_wfd_dbus:
	wfd_dbus_free(wfd_dbus);
bus_detach_event:
	sd_bus_detach_event(bus);
unref_bus:
	sd_bus_flush_close_unref(bus);
disable_watchdog:
	sd_event_set_watchdog(event, false);
unref_event:
	sd_event_unref(event);
end:
	return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

