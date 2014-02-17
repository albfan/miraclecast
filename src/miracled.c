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
#include <getopt.h>
#include <libudev.h>
#include <libwfd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-daemon.h>
#include <systemd/sd-event.h>
#include <time.h>
#include <unistd.h>
#include "miracled.h"
#include "shl_htable.h"
#include "shl_macro.h"
#include "shl_log.h"
#include "shl_util.h"

/*
 * Peer Handling
 */

struct peer *manager_find_peer(struct manager *m, const char *name)
{
	char **elem;
	bool res;

	res = shl_htable_lookup_str(&m->peers, name, NULL, &elem);
	if (!res)
		return NULL;

	return peer_from_htable(elem);
}

/*
 * Link Handling
 */

struct link *manager_find_link(struct manager *m, const char *name)
{
	char **elem;
	bool res;

	res = shl_htable_lookup_str(&m->links, name, NULL, &elem);
	if (!res)
		return NULL;

	return link_from_htable(elem);
}

/*
 * Manager Handling
 */

static void manager_add_link_from_udev(struct manager *m,
				       struct udev_device *d)
{
	struct link *l;

	if (!udev_device_has_tag(d, "miracle"))
		return;

	log_debug("link %s tagged via udev",
		  udev_device_get_sysname(d));

	link_new(m,
		 LINK_WIFI,
		 udev_device_get_sysname(d),
		 &l);
}

static void manager_remove_link_from_udev(struct manager *m,
					  struct udev_device *d)
{
	_shl_cleanup_free_ char *name = NULL;
	int r;
	struct link *l;

	r = link_make_name(LINK_WIFI, udev_device_get_sysname(d), &name);
	if (r < 0)
		return log_vERR(r);

	l = manager_find_link(m, name);
	if (!l)
		return;

	log_debug("link %s removed via udev", name);

	link_free(l);
}

static int manager_udev_fn(sd_event_source *source,
			   int fd,
			   uint32_t mask,
			   void *data)
{
	struct manager *m = data;
	struct udev_device *d = NULL;
	const char *action;

	d = udev_monitor_receive_device(m->udev_mon);
	if (!d)
		goto out;

	action = udev_device_get_action(d);
	if (!action)
		goto out;

	if (!strcmp(action, "add"))
		manager_add_link_from_udev(m, d);
	else if (!strcmp(action, "remove"))
		manager_remove_link_from_udev(m, d);

out:
	udev_device_unref(d);
	return 0;
}

static int manager_signal_fn(sd_event_source *source,
			     const struct signalfd_siginfo *ssi,
			     void *data)
{
	struct manager *m = data;

	if (ssi->ssi_signo == SIGCHLD) {
		log_debug("caught SIGCHLD for %d", (int)ssi->ssi_pid);
		return 0;
	}

	log_notice("caught signal %d, exiting..", (int)ssi->ssi_signo);
	sd_event_exit(m->event, 0);

	return 0;
}

static void manager_free(struct manager *m)
{
	unsigned int i;
	struct link *l;

	if (!m)
		return;

	while ((l = MANAGER_FIRST_LINK(m)))
		link_free(l);

	shl_htable_clear_str(&m->links, NULL, NULL);
	shl_htable_clear_str(&m->peers, NULL, NULL);

	manager_dbus_disconnect(m);

	sd_event_source_unref(m->udev_mon_source);
	udev_monitor_unref(m->udev_mon);
	udev_unref(m->udev);

	for (i = 0; m->sigs[i]; ++i)
		sd_event_source_unref(m->sigs[i]);
	sd_bus_unref(m->bus);
	sd_event_unref(m->event);
	free(m->friendly_name);
	free(m);
}

static int manager_new(struct manager **out)
{
	char buf[64] = { };
	struct manager *m;
	static const int sigs[] = {
		SIGINT, SIGTERM, SIGQUIT, SIGHUP, SIGPIPE, SIGCHLD, 0
	};
	unsigned int i;
	sigset_t mask;
	int r;

	m = calloc(1, sizeof(*m));
	if (!m)
		return log_ENOMEM();

	shl_htable_init_str(&m->links);
	shl_htable_init_str(&m->peers);

	snprintf(buf, sizeof(buf) - 1, "unknown-%u", rand());
	m->friendly_name = strdup(buf);
	if (!m->friendly_name) {
		r = log_ENOMEM();
		goto error;
	}

	r = sd_event_default(&m->event);
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	r = sd_event_set_watchdog(m->event, true);
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	r = sd_bus_default_system(&m->bus);
	if (r < 0) {
		log_error("cannot connect to system bus: %d", r);
		goto error;
	}

	r = sd_bus_attach_event(m->bus, m->event, 0);
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	for (i = 0; sigs[i]; ++i) {
		sigemptyset(&mask);
		sigaddset(&mask, sigs[i]);
		sigprocmask(SIG_BLOCK, &mask, NULL);

		r = sd_event_add_signal(m->event,
					sigs[i],
					manager_signal_fn,
					m,
					&m->sigs[i]);
		if (r < 0) {
			log_vERR(r);
			goto error;
		}
	}

	m->udev = udev_new();
	if (!m->udev) {
		r = log_ENOMEM();
		goto error;
	}

	m->udev_mon = udev_monitor_new_from_netlink(m->udev, "udev");
	if (!m->udev_mon) {
		r = log_ENOMEM();
		goto error;
	}

	r = udev_monitor_filter_add_match_subsystem_devtype(m->udev_mon,
							    "net",
							    "wlan");
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	r = udev_monitor_enable_receiving(m->udev_mon);
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	r = sd_event_add_io(m->event,
			    udev_monitor_get_fd(m->udev_mon),
			    EPOLLHUP | EPOLLERR | EPOLLIN,
			    manager_udev_fn,
			    m,
			    &m->udev_mon_source);
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	r = manager_dbus_connect(m);
	if (r < 0)
		goto error;

	*out = m;
	return 0;

error:
	manager_free(m);
	return r;
}

static void manager_read_name(struct manager *m)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_cleanup_sd_bus_message_ sd_bus_message *rep = NULL;
	const char *name;
	char *str;
	int r;

	r = sd_bus_call_method(m->bus,
			       "org.freedesktop.hostname1",
			       "/org/freedesktop/hostname1",
			       "org.freedesktop.DBus.Properties",
			       "Get",
			       &err,
			       &rep,
			       "ss", "org.freedesktop.hostname1", "Hostname");
	if (r < 0) {
		return;
	}

	r = sd_bus_message_enter_container(rep, 'v', "s");
	if (r < 0)
		goto error;

	r = sd_bus_message_read(rep, "s", &name);
	if (r < 0)
		goto error;

	if (!name || !*name) {
		log_warning("no hostname set on systemd.hostname1, using: %s",
			    m->friendly_name);
		return;
	}

	str = strdup(name);
	if (!str)
		return log_vENOMEM();

	free(m->friendly_name);
	m->friendly_name = str;
	log_debug("friendly-name from local hostname: %s", str);

	return;

error:
	log_warning("cannot read hostname from systemd.hostname1: %s",
		    bus_error_message(&err, r));
}

static void manager_read_links(struct manager *m)
{
	struct udev_enumerate *e = NULL;
	struct udev_list_entry *l;
	struct udev_device *d;
	int r;

	e = udev_enumerate_new(m->udev);
	if (!e)
		goto error;

	r = udev_enumerate_add_match_subsystem(e, "net");
	if (r < 0)
		goto error;

	r = udev_enumerate_add_match_property(e, "DEVTYPE", "wlan");
	if (r < 0)
		goto error;

	r = udev_enumerate_add_match_is_initialized(e);
	if (r < 0)
		goto error;

	r = udev_enumerate_scan_devices(e);
	if (r < 0)
		goto error;

	udev_list_entry_foreach(l, udev_enumerate_get_list_entry(e)) {
		d = udev_device_new_from_syspath(m->udev,
						 udev_list_entry_get_name(l));
		if (!d)
			goto error;

		manager_add_link_from_udev(m, d);
		udev_device_unref(d);
	}

	goto out;

error:
	log_warning("cannot enumerate links via udev");
out:
	udev_enumerate_unref(e);
}

static int manager_run(struct manager *m)
{
	manager_read_name(m);
	manager_read_links(m);
	return sd_event_loop(m->event);
}

static int help(void)
{
	printf("%s [OPTIONS...] ...\n\n"
	       "Wifi-Display Daemon.\n\n"
	       "  -h --help             Show this help\n"
	       "     --version          Show package version\n"
	       "     --log-level <lvl>  Maximum level for log messages\n"
	       "     --log-time         Prefix log-messages with timestamp\n"
	       , program_invocation_short_name);

	return 0;
}

static int parse_argv(int argc, char *argv[])
{
	enum {
		ARG_VERSION = 0x100,
		ARG_LOG_LEVEL,
		ARG_LOG_TIME,
	};
	static const struct option options[] = {
		{ "help",	no_argument,		NULL,	'h' },
		{ "version",	no_argument,		NULL,	ARG_VERSION },
		{ "log-level",	required_argument,	NULL,	ARG_LOG_LEVEL },
		{ "log-time",	no_argument,		NULL,	ARG_LOG_TIME },
		{}
	};
	int c;

	while ((c = getopt_long(argc, argv, "h", options, NULL)) >= 0) {
		switch (c) {
		case 'h':
			return help();
		case ARG_VERSION:
			puts(PACKAGE_STRING);
			return 0;
		case ARG_LOG_LEVEL:
			log_max_sev = atoi(optarg);
			break;
		case ARG_LOG_TIME:
			log_init_time();
			break;
		case '?':
			return -EINVAL;
		}
	}

	if (optind < argc) {
		log_error("unparsed remaining arguments starting with: %s",
			  argv[optind]);
		return -EINVAL;
	}

	log_format(LOG_DEFAULT_BASE, NULL, LOG_INFO,
		   "miracled - revision %s %s %s",
		   "some-rev-TODO-xyz", __DATE__, __TIME__);

	return 1;
}

int main(int argc, char **argv)
{
	struct manager *m = NULL;
	int r;

	srand(time(NULL));

	r = parse_argv(argc, argv);
	if (r < 0)
		return EXIT_FAILURE;
	if (!r)
		return EXIT_SUCCESS;

	r = manager_new(&m);
	if (r < 0)
		goto finish;

	r = sd_notify(false, "READY=1\n"
			     "STATUS=Running..");
	if (r < 0) {
		log_vERR(r);
		goto finish;
	}

	r = manager_run(m);

finish:
	sd_notify(false, "STATUS=Exiting..");
	manager_free(m);

	log_debug("exiting..");
	return abs(r);
}
