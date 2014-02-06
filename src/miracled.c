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
#include <unistd.h>
#include "miracled.h"
#include "shl_htable.h"
#include "shl_log.h"

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
	for (i = 0; m->sigs[i]; ++i)
		sd_event_source_unref(m->sigs[i]);
	sd_bus_unref(m->bus);
	sd_event_unref(m->event);
	free(m);
}

static int manager_new(struct manager **out)
{
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

	r = manager_dbus_connect(m);
	if (r < 0)
		goto error;

	*out = m;
	return 0;

error:
	manager_free(m);
	return r;
}

static int manager_run(struct manager *m)
{
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
	       "\n"
	       "     --netdev <dev>     Network device to run on\n"
	       "     --wpa-rundir <dir> wpa_supplicant runtime dir [default: /run/wpa_supplicant]\n"
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
