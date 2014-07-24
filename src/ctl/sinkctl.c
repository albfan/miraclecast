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

#include <errno.h>
#include <getopt.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/signalfd.h>
#include <sys/time.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include <systemd/sd-journal.h>
#include <time.h>
#include <unistd.h>
#include "ctl.h"
#include "shl_macro.h"
#include "shl_util.h"

static sd_bus *bus;
static struct ctl_wifi *wifi;
static struct ctl_sink *sink;
static sd_event_source *scan_timeout;
static sd_event_source *sink_timeout;
static unsigned int sink_timeout_time;
static bool sink_connected;
static pid_t sink_pid;

static char *bound_link;
static struct ctl_link *running_link;
static struct ctl_peer *running_peer;

/*
 * cmd list
 */

static int cmd_list(char **args, unsigned int n)
{
	size_t link_cnt = 0, peer_cnt = 0;
	struct shl_dlist *i, *j;
	struct ctl_link *l;
	struct ctl_peer *p;

	/* list links */

	cli_printf("%6s %-24s %-30s\n",
		   "LINK", "INTERFACE", "FRIENDLY-NAME");

	shl_dlist_for_each(i, &wifi->links) {
		l = link_from_dlist(i);
		++link_cnt;

		cli_printf("%6s %-24s %-30s\n",
			   l->label,
			   shl_isempty(l->ifname) ?
			       "<unknown>" : l->ifname,
			   shl_isempty(l->friendly_name) ?
			       "<unknown>" : l->friendly_name);
	}

	cli_printf("\n");

	/* list peers */

	cli_printf("%6s %-24s %-30s %-10s\n",
		   "LINK", "PEER-ID", "FRIENDLY-NAME", "CONNECTED");

	shl_dlist_for_each(i, &wifi->links) {
		l = link_from_dlist(i);

		shl_dlist_for_each(j, &l->peers) {
			p = peer_from_dlist(j);
			++peer_cnt;

			cli_printf("%6s %-24s %-30s %-10s\n",
				   p->l->label,
				   p->label,
				   shl_isempty(p->friendly_name) ?
				       "<unknown>" : p->friendly_name,
				   p->connected ? "yes" : "no");
		}
	}

	cli_printf("\n %u peers and %u links listed.\n", peer_cnt, link_cnt);

	return 0;
}

/*
 * cmd: show
 */

static int cmd_show(char **args, unsigned int n)
{
	struct ctl_link *l = NULL;
	struct ctl_peer *p = NULL;

	if (n > 0) {
		if (!(l = ctl_wifi_find_link(wifi, args[0])) &&
		    !(p = ctl_wifi_find_peer(wifi, args[0])) &&
		    !(l = ctl_wifi_search_link(wifi, args[0])) &&
		    !(p = ctl_wifi_search_peer(wifi, args[0]))) {
			cli_error("unknown link or peer %s", args[0]);
			return 0;
		}
	}

	if (l) {
		cli_printf("Link=%s\n", l->label);
		if (l->ifindex > 0)
			cli_printf("InterfaceIndex=%u\n", l->ifindex);
		if (l->ifname && *l->ifname)
			cli_printf("InterfaceName=%s\n", l->ifname);
		if (l->friendly_name && *l->friendly_name)
			cli_printf("FriendlyName=%s\n", l->friendly_name);
		cli_printf("P2PScanning=%d\n", l->p2p_scanning);
		if (l->wfd_subelements && *l->wfd_subelements)
			cli_printf("WfdSubelements=%s\n", l->wfd_subelements);
	} else if (p) {
		cli_printf("Peer=%s\n", p->label);
		if (p->p2p_mac && *p->p2p_mac)
			cli_printf("P2PMac=%s\n", p->p2p_mac);
		if (p->friendly_name && *p->friendly_name)
			cli_printf("FriendlyName=%s\n", p->friendly_name);
		cli_printf("Connected=%d\n", p->connected);
		if (p->interface && *p->interface)
			cli_printf("Interface=%s\n", p->interface);
		if (p->local_address && *p->local_address)
			cli_printf("LocalAddress=%s\n", p->local_address);
		if (p->remote_address && *p->remote_address)
			cli_printf("RemoteAddress=%s\n", p->remote_address);
		if (p->wfd_subelements && *p->wfd_subelements)
			cli_printf("WfdSubelements=%s\n", p->wfd_subelements);
	} else {
		cli_printf("Show what?\n");
		return 0;
	}

	return 0;
}

/*
 * cmd: run
 */

static void run_on(struct ctl_link *l)
{
	if (running_link)
		return;

	running_link = l;
	ctl_link_set_wfd_subelements(l, "000600111c4400c8");
	ctl_link_set_p2p_scanning(l, true);
	cli_printf("now running on link %s\n", running_link->label);
}

static int cmd_run(char **args, unsigned int n)
{
	struct ctl_link *l;

	if (running_link) {
		cli_error("already running on %s", running_link->label);
		return 0;
	}

	l = ctl_wifi_search_link(wifi, args[0]);
	if (!l) {
		cli_error("unknown link %s", args[0]);
		return 0;
	}

	run_on(l);

	return 0;
}

/*
 * cmd: bind
 */

static int cmd_bind(char **args, unsigned int n)
{
	struct ctl_link *l;
	char *t;

	if (running_link) {
		cli_error("already running on %s", running_link->label);
		return 0;
	}

	t = strdup(args[0]);
	if (!t)
		cli_vENOMEM();

	free(bound_link);
	bound_link = t;

	l = ctl_wifi_search_link(wifi, bound_link);
	if (!l)
		return 0;

	run_on(l);

	return 0;
}

/*
 * cmd: quit/exit
 */

static int cmd_quit(char **args, unsigned int n)
{
	cli_exit();
	return 0;
}

/*
 * main
 */

static void schedule_timeout(sd_event_source **out,
			     uint64_t rel_usec,
			     sd_event_time_handler_t timeout_fn,
			     void *data)
{
	int r;

	rel_usec += shl_now(CLOCK_MONOTONIC);

	if (*out) {
		r = sd_event_source_set_time(*out, rel_usec);
		if (r < 0)
			cli_vERR(r);
	} else {
		r = sd_event_add_time(cli_event,
				      out,
				      CLOCK_MONOTONIC,
				      rel_usec,
				      0,
				      timeout_fn,
				      data);
		if (r < 0)
			cli_vERR(r);
	}
}

static void stop_timeout(sd_event_source **out)
{
	if (*out) {
		sd_event_source_set_enabled(*out, SD_EVENT_OFF);
		sd_event_source_unref(*out);
		*out = NULL;
	}
}

static int scan_timeout_fn(sd_event_source *s, uint64_t usec, void *data)
{
	stop_timeout(&scan_timeout);

	if (running_link)
		ctl_link_set_p2p_scanning(running_link, true);

	return 0;
}

static int sink_timeout_fn(sd_event_source *s, uint64_t usec, void *data)
{
	int r;

	stop_timeout(&sink_timeout);

	if (running_peer &&
	    running_peer->connected &&
	    ctl_sink_is_closed(sink)) {
		r = ctl_sink_connect(sink, running_peer->remote_address);
		if (r < 0) {
			if (sink_timeout_time++ >= 3)
				cli_vERR(r);
			else
				schedule_timeout(&sink_timeout,
						 sink_timeout_time * 1000ULL * 1000ULL,
						 sink_timeout_fn,
						 NULL);
		}
	}

	return 0;
}

static const struct cli_cmd cli_cmds[] = {
	{ "list",		NULL,					CLI_M,	CLI_LESS,	0,	cmd_list,		"List all objects" },
	{ "show",		"<link|peer>",				CLI_M,	CLI_LESS,	1,	cmd_show,		"Show detailed object information" },
	{ "run",		"<link>",				CLI_M,	CLI_EQUAL,	1,	cmd_run,		"Run sink on given link" },
	{ "bind",		"<link>",				CLI_M,	CLI_EQUAL,	1,	cmd_bind,		"Like 'run' but bind the link name to run when it is hotplugged" },
	{ "quit",		NULL,					CLI_Y,	CLI_MORE,	0,	cmd_quit,		"Quit program" },
	{ "exit",		NULL,					CLI_Y,	CLI_MORE,	0,	cmd_quit,		NULL },
	{ "help",		NULL,					CLI_M,	CLI_MORE,	0,	NULL,			"Print help" },
	{ },
};

static void spawn_gst(void)
{
	char *argv[64];
	pid_t pid;
	int fd_journal, i;
	sigset_t mask;

	if (sink_pid > 0)
		return;

	pid = fork();
	if (pid < 0) {
		return cli_vERRNO();
	} else if (!pid) {
		/* child */

		sigemptyset(&mask);
		sigprocmask(SIG_SETMASK, &mask, NULL);

		/* redirect stdout/stderr to journal */
		fd_journal = sd_journal_stream_fd("miracle-sinkctl-gst",
						  LOG_INFO,
						  false);
		if (fd_journal >= 0) {
			/* dup journal-fd to stdout and stderr */
			dup2(fd_journal, 1);
			dup2(fd_journal, 2);
		} else {
			/* no journal? redirect stdout to parent's stderr */
			dup2(2, 1);
		}

		i = 0;
		argv[i++] = (char*) "/usr/bin/gst-launch-1.0";
		argv[i++] = "-v";
		if (cli_max_sev >= 7)
			argv[i++] = "--gst-debug=3";
		argv[i++] = "udpsrc";
		argv[i++] = "port=1991";
		argv[i++] = "caps=\"application/x-rtp, media=video\"";
		argv[i++] = "!";
		argv[i++] = "rtpjitterbuffer";
		argv[i++] = "!";
		argv[i++] = "rtpmp2tdepay";
		argv[i++] = "!";
		argv[i++] = "tsdemux";
		argv[i++] = "!";
		argv[i++] = "h264parse";
		argv[i++] = "!";
		argv[i++] = "avdec_h264";
		argv[i++] = "!";
		argv[i++] = "autovideosink";
		argv[i++] = "sync=false";
		argv[i] = NULL;

		execve(argv[0], argv, environ);
		_exit(1);
	} else {
		sink_pid = pid;
	}
}

static void kill_gst(void)
{
	if (sink_pid <= 0)
		return;

	kill(sink_pid, SIGTERM);
	sink_pid = 0;
}

void ctl_fn_sink_connected(struct ctl_sink *s)
{
	cli_notice("SINK connected");
	sink_connected = true;
	spawn_gst();
}

void ctl_fn_sink_disconnected(struct ctl_sink *s)
{
	if (!sink_connected) {
		/* treat HUP as timeout */
		sink_timeout_fn(sink_timeout, 0, NULL);
	} else {
		cli_notice("SINK disconnected");
		sink_connected = false;
	}
}

void ctl_fn_peer_new(struct ctl_peer *p)
{
	if (p->l != running_link || shl_isempty(p->wfd_subelements))
		return;

	if (cli_running())
		cli_printf("[" CLI_GREEN "ADD" CLI_DEFAULT "] Peer: %s\n",
			   p->label);
}

void ctl_fn_peer_free(struct ctl_peer *p)
{
	if (p->l != running_link || shl_isempty(p->wfd_subelements))
		return;

	if (p == running_peer) {
		cli_printf("no longer running on peer %s\n",
			   running_peer->label);
		stop_timeout(&sink_timeout);
		kill_gst();
		ctl_sink_close(sink);
		running_peer = NULL;
		stop_timeout(&scan_timeout);
		ctl_link_set_p2p_scanning(p->l, true);
	}

	if (cli_running())
		cli_printf("[" CLI_RED "REMOVE" CLI_DEFAULT "] Peer: %s\n",
			   p->label);
}

void ctl_fn_peer_provision_discovery(struct ctl_peer *p,
				     const char *prov,
				     const char *pin)
{
	if (p->l != running_link || shl_isempty(p->wfd_subelements))
		return;

	if (cli_running())
		cli_printf("[" CLI_YELLOW "PROV" CLI_DEFAULT "] Peer: %s Type: %s PIN: %s\n",
			   p->label, prov, pin);

	if (!running_peer) {
		/* auto accept any incoming connection attempt */
		ctl_peer_connect(p, "auto", "");

		/* 60s timeout in case the connect fails. Yes, stupid wpas does
		 * not catch this and notify us.. and as it turns out, DHCP
		 * negotiation with some proprietary devices can take up to 30s
		 * so lets be safe. */
		schedule_timeout(&scan_timeout,
				 60 * 1000ULL * 1000ULL,
				 scan_timeout_fn,
				 NULL);
	}
}

void ctl_fn_peer_formation_failure(struct ctl_peer *p, const char *reason)
{
	if (p->l != running_link || shl_isempty(p->wfd_subelements))
		return;

	if (cli_running())
		cli_printf("[" CLI_YELLOW "FAIL" CLI_DEFAULT "] Peer: %s Reason: %s\n",
			   p->label, reason);

	if (!running_peer) {
		stop_timeout(&scan_timeout);
		ctl_link_set_p2p_scanning(p->l, true);
	}
}

void ctl_fn_peer_connected(struct ctl_peer *p)
{
	if (p->l != running_link || shl_isempty(p->wfd_subelements))
		return;

	if (cli_running())
		cli_printf("[" CLI_GREEN "CONNECT" CLI_DEFAULT "] Peer: %s\n",
			   p->label);

	if (!running_peer) {
		running_peer = p;
		cli_printf("now running on peer %s\n", running_peer->label);
		stop_timeout(&scan_timeout);

		sink_connected = false;
		sink_timeout_time = 1;
		schedule_timeout(&sink_timeout,
				 sink_timeout_time * 1000ULL * 1000ULL,
				 sink_timeout_fn,
				 NULL);
	}
}

void ctl_fn_peer_disconnected(struct ctl_peer *p)
{
	if (p->l != running_link || shl_isempty(p->wfd_subelements))
		return;

	if (p == running_peer) {
		cli_printf("no longer running on peer %s\n",
			   running_peer->label);
		stop_timeout(&sink_timeout);
		kill_gst();
		ctl_sink_close(sink);
		running_peer = NULL;
		stop_timeout(&scan_timeout);
		ctl_link_set_p2p_scanning(p->l, true);
	}

	if (cli_running())
		cli_printf("[" CLI_YELLOW "DISCONNECT" CLI_DEFAULT "] Peer: %s\n",
			   p->label);
}

void ctl_fn_link_new(struct ctl_link *l)
{
	if (cli_running())
		cli_printf("[" CLI_GREEN "ADD" CLI_DEFAULT "] Link: %s\n",
			   l->label);

	/* If we're not running but have a bound link, try to find it now and
	 * start running if the link is now available. */
	if (!running_link && bound_link) {
		l = ctl_wifi_search_link(wifi, bound_link);
		if (l)
			run_on(l);
	}
}

void ctl_fn_link_free(struct ctl_link *l)
{
	if (l == running_link) {
		cli_printf("no longer running on link %s\n",
			   running_link->label);
		running_link = NULL;
		stop_timeout(&scan_timeout);
	}

	if (cli_running())
		cli_printf("[" CLI_RED "REMOVE" CLI_DEFAULT "] Link: %s\n",
			   l->label);
}

void cli_fn_help()
{
	/*
	 * 80-char barrier:
	 *      01234567890123456789012345678901234567890123456789012345678901234567890123456789
	 */
	printf("%s [OPTIONS...] ...\n\n"
	       "Control a dedicated local sink via MiracleCast.\n"
	       "  -h --help             Show this help\n"
	       "     --version          Show package version\n"
	       "     --log-level <lvl>  Maximum level for log messages\n"
	       "\n"
	       "Commands:\n"
	       , program_invocation_short_name);
	/*
	 * 80-char barrier:
	 *      01234567890123456789012345678901234567890123456789012345678901234567890123456789
	 */
}

static int ctl_interactive(char **argv, int argc)
{
	int r;

	r = cli_init(bus, cli_cmds);
	if (r < 0)
		return r;

	r = ctl_sink_new(&sink, cli_event);
	if (r < 0)
		goto error;

	r = ctl_wifi_fetch(wifi);
	if (r < 0)
		goto error;

	if (argc > 0) {
		r = cli_do(cli_cmds, argv, argc);
		if (r == -EAGAIN)
			cli_error("unknown operation %s", argv[0]);
	}

	r = cli_run();

error:
	ctl_sink_free(sink);
	cli_destroy();
	return r;
}

static int ctl_main(int argc, char *argv[])
{
	struct shl_dlist *i;
	struct ctl_link *l;
	int r, left;

	r = ctl_wifi_new(&wifi, bus);
	if (r < 0)
		return r;

	left = argc - optind;
	r = ctl_interactive(argv + optind, left <= 0 ? 0 : left);

	/* stop all scans */
	shl_dlist_for_each(i, &wifi->links) {
		l = link_from_dlist(i);
		if (l->have_p2p_scan)
			ctl_link_set_p2p_scanning(l, false);
	}

	ctl_wifi_free(wifi);
	return r;
}

static int parse_argv(int argc, char *argv[])
{
	enum {
		ARG_VERSION = 0x100,
		ARG_LOG_LEVEL,
	};
	static const struct option options[] = {
		{ "help",	no_argument,		NULL,	'h' },
		{ "version",	no_argument,		NULL,	ARG_VERSION },
		{ "log-level",	required_argument,	NULL,	ARG_LOG_LEVEL },
		{}
	};
	int c;

	while ((c = getopt_long(argc, argv, "h", options, NULL)) >= 0) {
		switch (c) {
		case 'h':
			return cli_help(cli_cmds);
		case ARG_VERSION:
			puts(PACKAGE_STRING);
			return 0;
		case ARG_LOG_LEVEL:
			cli_max_sev = atoi(optarg);
			break;
		case '?':
			return -EINVAL;
		}
	}

	return 1;
}

int main(int argc, char **argv)
{
	int r;

	setlocale(LC_ALL, "");

	r = parse_argv(argc, argv);
	if (r < 0)
		return EXIT_FAILURE;
	if (!r)
		return EXIT_SUCCESS;

	r = sd_bus_default_system(&bus);
	if (r < 0) {
		cli_error("cannot connect to system bus: %s", strerror(-r));
		return EXIT_FAILURE;
	}

	r = ctl_main(argc, argv);
	sd_bus_unref(bus);

	return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
