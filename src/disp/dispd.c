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
#include "dispd.h"
#include "wfd.h"
#include "dispd-dbus.h"
#include "config.h"

#define CLI_PROMPT "\001" CLI_BLUE "\002" "[dispctl] # " "\001" CLI_DEFAULT "\002"

#define HISTORY_FILENAME ".miracle-disp.history"

static int dispd_init(struct dispd *dispd, sd_bus *bus);
static void dispd_free(struct dispd *dispd);

static struct dispd_dbus *dispd_dbus = NULL;

char* get_cli_prompt()
{
        return CLI_PROMPT;
}

/*
 * get history filename
 */

char* get_history_filename()
{
  return HISTORY_FILENAME;
}

struct dispd_dbus * dispd_dbus_get()
{
	return dispd_dbus;
}
static struct dispd *dispd = NULL;

struct ctl_wifi *get_wifi()
{
        return dispd->wifi;
}

struct dispd * dispd_get()
{
	return dispd;
}

int dispd_new(struct dispd **out, sd_event *loop, sd_bus *bus)
{
	int r;
	struct dispd *dispd = calloc(1, sizeof(struct dispd));
	if(!dispd) {
		r = -ENOMEM;
		goto error;
	}

	shl_htable_init_str(&dispd->sinks);
	shl_htable_init_uint(&dispd->sessions);
	dispd->loop = sd_event_ref(loop);

	r = dispd_init(dispd, bus);
	if(0 > r) {
		goto error;
	}

	*out = dispd;

	return 0;

error:
	dispd_free(dispd);
	return log_ERR(r);
}

static void dispd_free(struct dispd *dispd)
{
	if(!dispd) {
		return;
	}

	ctl_wifi_free(dispd->wifi);
	dispd->wifi = NULL;
	shl_htable_clear_str(&dispd->sinks, NULL, NULL);
	shl_htable_clear_uint(&dispd->sessions, NULL, NULL);

	if(dispd->loop) {
		sd_event_unref(dispd->loop);
	}

	free(dispd);
}

static int dispd_handle_shutdown(sd_event_source *s,
				uint64_t usec,
				void *userdata)
{
	struct dispd *dispd = userdata;

	sd_event_exit(dispd->loop, 0);

	return 0;
}

void dispd_shutdown(struct dispd *dispd)
{
	uint64_t now;
	int r = sd_event_now(dispd_get_loop(), CLOCK_MONOTONIC, &now);
	if(0 > r) {
		goto error;
	}

	r = sd_event_add_time(dispd_get_loop(),
					NULL,
					CLOCK_MONOTONIC,
					now + 100 * 1000,
					0,
					dispd_handle_shutdown,
					dispd);
	if(0 <= r) {
		return;
	}

error:
	sd_event_exit(dispd->loop, 0);
}

int dispd_add_sink(struct dispd *dispd,
				struct ctl_peer *p,
				union wfd_sube *sube,
				struct dispd_sink **out)
{
	_dispd_sink_free_ struct dispd_sink *s = NULL;
	int r = shl_htable_lookup_str(&dispd->sinks,
					p->label,
					NULL,
					NULL);
	if(r) {
		return -EEXIST;
	}

	r = dispd_sink_new(&s, p, sube);
	if(0 > r) {
		return log_ERR(r);
	}

	r = shl_htable_insert_str(&dispd->sinks,
					dispd_sink_to_htable(s),
					NULL);
	if(0 > r) {
		return log_ERR(r);
	}

	++dispd->n_sinks;
	*out = s;
	s = NULL;

	return 0;
}

int dispd_find_sink_by_label(struct dispd *dispd,
				const char *label,
				struct dispd_sink **out)
{
	char **entry;
	int r = shl_htable_lookup_str(&dispd->sinks, label, NULL, &entry);
	if(r && out) {
		*out = dispd_sink_from_htable(entry);
	}

	return r;
}

static int dispd_remove_sink_by_label(struct dispd *dispd,
				const char *label,
				struct dispd_sink **out)
{
	char **entry;
	int r = shl_htable_remove_str(&dispd->sinks, label, NULL, &entry);
	if(!r) {
		goto end;
	}

	--dispd->n_sinks;

	if(out) {
		*out = dispd_sink_from_htable(entry);
	}

end:
	return r;
}

unsigned int dispd_alloc_session_id(struct dispd *dispd)
{
	return ++dispd->id_pool;
}

int dispd_add_session(struct dispd *dispd, struct dispd_session *s)
{
	int r;

	assert(dispd);
	assert(s && dispd_session_get_id(s));
	assert(!dispd_find_session_by_id(dispd, dispd_session_get_id(s), NULL));

	r = shl_htable_insert_uint(&dispd->sessions, dispd_session_to_htable(s));
	if(0 > r) {
		return log_ERR(r);
	}

	++dispd->n_sessions;

	dispd_fn_session_new(s);

	return 0;
}

int dispd_find_session_by_id(struct dispd *dispd,
				unsigned int id,
				struct dispd_session **out)
{
	unsigned int *entry;
	int r = shl_htable_lookup_uint(&dispd->sessions, id, &entry);
	if(r && out) {
		*out = dispd_session_from_htable(entry);
	}

	return r;
}

int dispd_remove_session_by_id(struct dispd *dispd,
				unsigned int id,
				struct dispd_session **out)
{
	unsigned int *entry;
	struct dispd_session *s;
	int r = shl_htable_remove_uint(&dispd->sessions, id, &entry);
	if(!r) {
		return 0;
	}

	--dispd->n_sessions;

	s = dispd_session_from_htable(entry);
	dispd_fn_session_free(s);
	if(out) {
		*out = s;
	}

	return 1;
}

static int dispd_fetch_info(sd_event_source *s, void *userdata)
{
	struct dispd *dispd = userdata;
	int r;

	sd_event_source_unref(s);
   
	r = ctl_wifi_fetch(dispd->wifi);
	if(0 > r) {
		log_warning("failed to fetch information about links and peers: %s",
						strerror(errno));
		sd_event_exit(dispd->loop, r);
	}

	return r;
}

static int dispd_handle_signal(sd_event_source *s,
				const struct signalfd_siginfo *ssi,
				void *userdata)
{
	int r;
	siginfo_t siginfo;
	struct dispd *dispd = userdata;

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

	return sd_event_exit(dispd->loop, 0);
}

static int dispd_init(struct dispd *dispd, sd_bus *bus)
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

		r = sd_event_add_signal(dispd->loop,
						NULL,
						signals[i],
						dispd_handle_signal,
						dispd);
		if(0 > r) {
			break;
		}
	}

	r = ctl_wifi_new(&wifi, bus);
	if(0 > r) {
		log_vENOMEM();
		goto end;
	}

	r = sd_event_add_defer(dispd->loop, NULL, dispd_fetch_info, dispd);
	if(0 > r) {
		log_vERRNO();

		ctl_wifi_free(wifi);
		goto end;
	}

	dispd->wifi = wifi;

end:
	return r;
}

void ctl_fn_peer_new(struct ctl_peer *p)
{
	struct dispd_sink *s;
	union wfd_sube sube;
	int r;

	log_debug("new peer %s (%s) shows up, wfd_subelems: '%s'",
					p->label,
					p->friendly_name,
					p->wfd_subelements);

	if(!p->wfd_subelements || !*p->wfd_subelements) {
		log_info("peer %s has no wfd_subelems, ignore it", p->label);
		return;
	}

	r = wfd_sube_parse(p->wfd_subelements, &sube);
	if(0 > r) {
		log_debug("peer %s has no valid subelement, ignore it", p->label);
		return;
	}

	if(wfd_sube_device_is_sink(&sube)) {
		r = dispd_add_sink(dispd_get(), p, &sube, &s);
		if(0 > r) {
			log_warning("failed to add sink (%s, '%s'): %s",
							p->friendly_name,
							p->p2p_mac,
							strerror(errno));
			return log_vERRNO();
		}

		r = dispd_fn_sink_new(s);
		if(0 > r) {
			log_warning("failed to publish newly added sink (%s): %s",
							dispd_sink_get_label(s),
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
	struct dispd_sink *s;
	int r;

	r = dispd_remove_sink_by_label(dispd, p->label, &s);
	if(r) {
		dispd_fn_sink_free(s);
		log_info("sink %s removed", s->label);
		dispd_sink_free(s);
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

	r = dispd_dbus_new(&dispd_dbus, event, bus);
	if(0 > r) {
		goto bus_detach_event;
	}

	r = dispd_new(&dispd, event, bus);
	if(0 > r) {
		goto free_dispd_dbus;
	}

	r = dispd_dbus_expose(dispd_dbus);
	if(0 > r) {
		log_warning("unable to publish WFD service: %s", strerror(errno));
		goto free_dispd;
	}

	r = sd_notify(false, "READY=1\n"
			     "STATUS=Running..");
	if (0 > r) {
		log_warning("unable to notify systemd that we are ready: %s", strerror(errno));
		goto free_dispd_dbus;
	}

	sd_event_loop(event);

	sd_notify(false, "STATUS=Exiting..");

free_dispd:
	dispd_free(dispd);
free_dispd_dbus:
	dispd_dbus_free(dispd_dbus);
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

