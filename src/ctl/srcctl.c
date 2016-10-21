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
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "ctl.h"
#include "ctl-src.h"
#include "wfd.h"
#include "shl_macro.h"
#include "shl_util.h"
#include "config.h"

static sd_bus *bus;
static struct ctl_wifi *wifi;
static struct ctl_src *src;
static sd_event_source *scan_timeout;
static sd_event_source *src_timeout;
static unsigned int src_timeout_time;
static bool src_connected;
static pid_t src_pid;

//static char *selected_ link;
static struct ctl_link *running_link;
static struct ctl_peer *running_peer;
static struct ctl_peer *pending_peer;

void launch_sender(struct ctl_src *s);
//
//char *gst_scale_res;
int gst_audio_en = 1;
static const int DEFAULT_RSTP_PORT = 1991;
//bool uibc;
int rstp_port;
//int uibc_port;
//
//unsigned int wfd_supported_res_cea  = 0x0000001f;	/* up to 720x576 */
//unsigned int wfd_supported_res_vesa = 0x00000003;	/* up to 800x600 */
//unsigned int wfd_supported_res_hh   = 0x00000000;	/* not supported */

/*
 * cmd: select
 */

static int cmd_select(char **args, unsigned int n)
{
	struct ctl_link *l;

	if (!n) {
		if (running_link) {
			cli_printf("link %s deselected\n",
				   running_link->label);
			running_link = NULL;
		}

		return 0;
	}

	l = ctl_wifi_search_link(wifi, args[0]);
	if (!l) {
		cli_error("unknown link %s", args[0]);
		return 0;
	}

	running_link = l;
	cli_printf("link %s selected\n", running_link->label);

	return 0;
}

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
 * cmd: set-friendly-name
 */

static int cmd_set_friendly_name(char **args, unsigned int n)
{
	struct ctl_link *l = NULL;
	const char *name;

	if (n < 1) {
		cli_printf("To what?\n");
		return 0;
	}

	if (n > 1) {
		l = ctl_wifi_search_link(wifi, args[0]);
		if (!l) {
			cli_error("unknown link %s", args[0]);
			return 0;
		}

		name = args[1];
	} else {
		name = args[0];
	}

	l = l ? : running_link;
	if (!l) {
		cli_error("no link selected");
		return 0;
	}

	return ctl_link_set_friendly_name(l, name);
}

/*
 * cmd: p2p-scan
 */

static int cmd_p2p_scan(char **args, unsigned int n)
{
	struct ctl_link *l = NULL;
	unsigned int i;
	bool stop = false;
	int r;

	for (i = 0; i < n; ++i) {
		if (!strcmp(args[i], "stop")) {
			stop = true;
		} else {
			l = ctl_wifi_search_link(wifi, args[i]);
			if (!l) {
				cli_error("unknown link %s", args[i]);
				return 0;
			}
		}
	}

	l = l ? : running_link;
	if (!l) {
		cli_error("no link selected");
		return 0;
	}

	ctl_link_set_wfd_subelements(l, "000600101c4400c8");
	r = ctl_link_set_p2p_scanning(l, !stop);
	if(!r && !running_link) {
		running_link = l;
	}

	return r;
}

/*
 * cmd: connect
 */

static bool is_valid_prov(const char *prov)
{
	return prov && (!strcmp(prov, "auto") ||
			!strcmp(prov, "pbc") ||
			!strcmp(prov, "display") ||
			!strcmp(prov, "pin"));
}

static int cmd_connect(char **args, unsigned int n)
{
	struct ctl_peer *p;
	const char *prov, *pin;

	if (n < 1) {
		cli_printf("To whom?\n");
		return 0;
	}

	p = ctl_wifi_search_peer(wifi, args[0]);
	if (!p) {
		cli_error("unknown peer %s", args[0]);
		return 0;
	}

	if (n > 2) {
		prov = args[1];
		pin = args[2];
	} else if (n > 1) {
		if (is_valid_prov(args[1])) {
			prov = args[1];
			pin = "";
		} else {
			prov = "auto";
			pin = args[1];
		}
	} else {
		prov = "auto";
		pin = "";
	}

	return ctl_peer_connect(p, prov, pin);
}

/*
 * cmd: disconnect
 */

static int cmd_disconnect(char **args, unsigned int n)
{
	struct ctl_peer *p;

	if (n < 1) {
		cli_printf("From whom?\n");
		return 0;
	}

	p = ctl_wifi_search_peer(wifi, args[0]);
	if (!p) {
		cli_error("unknown peer %s", args[0]);
		return 0;
	}

	return ctl_peer_disconnect(p);
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

	if (pending_peer) {
		if (cli_running()) {
			cli_printf("[" CLI_RED "TIMEOUT" CLI_DEFAULT "] waiting for %s\n",
				pending_peer->friendly_name);
		}
		pending_peer = NULL;
	}

	if (running_link)
		ctl_link_set_p2p_scanning(running_link, true);

	return 0;
}

static int src_timeout_fn(sd_event_source *s, uint64_t usec, void *data)
{
	int r;

	stop_timeout(&src_timeout);

	if (running_peer &&
		running_peer->connected &&
		ctl_src_is_closed(src)) {
		r = ctl_src_listen(src, running_peer->local_address);
		if (r < 0) {
			if (src_timeout_time++ >= 3)
				cli_vERR(r);
			else
				schedule_timeout(&src_timeout,
						 src_timeout_time * 1000ULL * 1000ULL,
						 src_timeout_fn,
						 NULL);
		}

		log_info("listening on %s", running_peer->local_address);
	}

	return 0;
}

static const struct cli_cmd cli_cmds[] = {
	{ "list",		NULL,					CLI_M,	CLI_LESS,	0,	cmd_list,		"List all objects" },
	{ "select",		"[link]",				CLI_Y,	CLI_LESS,	1,	cmd_select,		"Select default link" },
	{ "show",		"<link|peer>",				CLI_M,	CLI_LESS,	1,	cmd_show,		"Show detailed object information" },
	{ "set-friendly-name",	"[link] <name>",			CLI_M,	CLI_LESS,	2,	cmd_set_friendly_name,	"Set friendly name of an object" },
	{ "p2p-scan",		"[link] [stop]",			CLI_Y,	CLI_LESS,	2,	cmd_p2p_scan,		"Control neighborhood P2P scanning" },
	{ "connect",		"<peer> [provision] [pin]",		CLI_M,	CLI_LESS,	3,	cmd_connect,		"Connect to peer" },
	{ "disconnect",		"<peer>",				CLI_M,	CLI_EQUAL,	1,	cmd_disconnect,		"Disconnect from peer" },
	{ "quit",		NULL,					CLI_Y,	CLI_MORE,	0,	cmd_quit,		"Quit program" },
	{ "exit",		NULL,					CLI_Y,	CLI_MORE,	0,	cmd_quit,		NULL },
	{ "help",		NULL,					CLI_M,	CLI_MORE,	0,	NULL,			"Print help" },
	{ },
};

static void spawn_gst(struct ctl_src *s)
{
	pid_t pid;
	int fd_journal;
	sigset_t mask;

	if (src_pid > 0)
		return;

	pid = fork();
	if (pid < 0) {
		return cli_vERRNO();
	} else if (!pid) {
		/* child */

		sigemptyset(&mask);
		sigprocmask(SIG_SETMASK, &mask, NULL);

		/* redirect stdout/stderr to journal */
		fd_journal = sd_journal_stream_fd("miracle-srcctl-gst",
						  LOG_DEBUG,
						  false);
		if (fd_journal >= 0) {
			/* dup journal-fd to stdout and stderr */
			dup2(fd_journal, 1);
			dup2(fd_journal, 2);
		} else {
			/* no journal? redirect stdout to parent's stderr */
			dup2(2, 1);
		}

		launch_sender(s);
		_exit(1);
	} else {
		src_pid = pid;
	}
}

void launch_sender(struct ctl_src *s) {
	char * argv[64];
	char resolution[64];
	char port[64];
	char uibc_portStr[64];
	int i = 0;

	argv[i++] = "miracle-sender";
	//if (gst_audio_en) {
	//	argv[i++] = "--acodec";
	//	argv[i++] = "aac";
	//}
	argv[i++] = "--host";
	argv[i++] = inet_ntoa(((struct sockaddr_in *) &s->addr)->sin_addr);
	argv[i++] = "-p";
	sprintf(port, "%d", rstp_port);
	argv[i++] = port;

//	if (s->hres && s->vres) {
//		sprintf(resolution, "%dx%d", s->hres, s->vres);
//		argv[i++] = "-r";
//		argv[i++] = resolution;
//	}

	argv[i] = NULL;

	if (execvpe(argv[0], argv, environ) < 0) {
		cli_debug("stream sender failed (%d): %m", errno);
		int i = 0;
		cli_debug("printing environment: ");
		while (environ[i]) {
			cli_debug("%s", environ[i++]);
		}
	}
}

//void launch_uibc_daemon(int port) {
//	char *argv[64];
//	char portStr[64];
//	int i = 0;
//	argv[i++] = "miracle-uibcctl";
//	argv[i++] = "localhost";
//	sprintf(portStr, "%d", port);
//	argv[i++] = portStr;
//	argv[i] = NULL;
//
//	cli_debug("uibc daemon: %s", argv[0]);
//	execvpe(argv[0], argv, environ);
//}
//
static void kill_gst(void)
{
	if (src_pid <= 0)
		return;

	kill(src_pid, SIGTERM);
	src_pid = 0;
}

void ctl_fn_src_connected(struct ctl_src *s)
{
	cli_notice("SOURCE connected");
	src_connected = true;
}

void ctl_fn_src_disconnected(struct ctl_src *s)
{
	if (!src_connected) {
		/* treat HUP as timeout */
		src_timeout_fn(src_timeout, 0, NULL);
	} else {
		cli_notice("SRC disconnected");
		src_connected = false;
	}
}

void ctl_fn_src_playing(struct ctl_src *s)
{
	cli_printf("SRC got play request\n");
	// TODO src_connected must be true, why if() failed?
	//if (src_connected)
		spawn_gst(s);
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

	if (p == pending_peer) {
		cli_printf("no longer waiting for peer %s (%s)\n",
			   p->friendly_name, p->label);
		pending_peer = NULL;
		stop_timeout(&scan_timeout);
		ctl_link_set_p2p_scanning(p->l, true);
	}

	if (p == running_peer) {
		cli_printf("no longer running on peer %s\n",
			   running_peer->label);
		stop_timeout(&src_timeout);
		kill_gst();
		ctl_src_close(src);
		running_peer = NULL;
		stop_timeout(&scan_timeout);
		ctl_link_set_p2p_scanning(p->l, true);
	}

	if (cli_running())
		cli_printf("[" CLI_RED "REMOVE" CLI_DEFAULT "] Peer: %s\n",
			   p->label);
}
//
void ctl_fn_peer_provision_discovery(struct ctl_peer *p,
					 const char *prov,
					 const char *pin)
{
	if (p->l != running_link || shl_isempty(p->wfd_subelements))
		return;

	if (cli_running())
		cli_printf("[" CLI_YELLOW "PROV" CLI_DEFAULT "] Peer: %s Type: %s PIN: %s\n",
			   p->label, prov, pin);
}

void ctl_fn_peer_go_neg_request(struct ctl_peer *p,
					 const char *prov,
					 const char *pin)
{
	if (p->l != running_link || shl_isempty(p->wfd_subelements))
		return;

	if (cli_running())
		cli_printf("[" CLI_YELLOW "GO NEG" CLI_DEFAULT "] Peer: %s Type: %s PIN: %s\n",
			   p->label, prov, pin);

	if (!running_peer) {
		/* auto accept any incoming connection attempt */
		ctl_peer_connect(p, "auto", "");
		pending_peer = p;

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

	pending_peer = NULL;

	if (!running_peer) {
		running_peer = p;
		cli_printf("now running on peer %s\n", running_peer->label);
		stop_timeout(&scan_timeout);

		src_connected = false;
		src_timeout_time = 1;
		schedule_timeout(&src_timeout,
				 src_timeout_time * 1000ULL * 1000ULL,
				 src_timeout_fn,
				 NULL);
	}
}
//
void ctl_fn_peer_disconnected(struct ctl_peer *p)
{
	if (p->l != running_link || shl_isempty(p->wfd_subelements))
		return;

	if (p == running_peer) {
		cli_printf("no longer running on peer %s\n",
			   running_peer->label);
		stop_timeout(&src_timeout);
		kill_gst();
		ctl_src_close(src);
		running_peer = NULL;
		stop_timeout(&scan_timeout);
		ctl_link_set_p2p_scanning(p->l, true);
	}

	if (cli_running())
		cli_printf("[" CLI_YELLOW "DISCONNECT" CLI_DEFAULT "] Peer: %s\n",
			   p->label);
}
//
void ctl_fn_link_new(struct ctl_link *l)
{
	if (cli_running())
		cli_printf("[" CLI_GREEN "ADD" CLI_DEFAULT "] Link: %s\n",
			   l->label);
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
	 *		01234567890123456789012345678901234567890123456789012345678901234567890123456789
	 */
	printf("%s [OPTIONS...] ...\n\n"
		   "Control a dedicated local sink via MiracleCast.\n"
		   "  -h --help				Show this help\n"
		   "	 --version			Show package version\n"
		   "	 --log-level <lvl>	Maximum level for log messages\n"
		   "	 --log-journal-level <lvl>	Maximum level for journal log messages\n"
		   "	 --audio <0/1>		Enable audio support (default %d)\n"
		   "\n"
		   , program_invocation_short_name, gst_audio_en
		   );
	/*
	 * 80-char barrier:
	 *		01234567890123456789012345678901234567890123456789012345678901234567890123456789
	 */
}

static int ctl_interactive(char **argv, int argc)
{
	int r;

	r = cli_init(bus, cli_cmds);
	if (r < 0)
		return r;

	r = ctl_src_new(&src, cli_event);
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
	ctl_src_free(src);
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
		ARG_JOURNAL_LEVEL,
		ARG_AUDIO,
	};
	static const struct option options[] = {
		{ "help",	no_argument,		NULL,	'h' },
		{ "version",	no_argument,		NULL,	ARG_VERSION },
		{ "log-level",	required_argument,	NULL,	ARG_LOG_LEVEL },
		{ "log-journal-level",	required_argument,	NULL,	ARG_JOURNAL_LEVEL },
		{ "audio",	required_argument,	NULL,	ARG_AUDIO },
		{}
	};
	int c;

	rstp_port = DEFAULT_RSTP_PORT;

	while ((c = getopt_long(argc, argv, "h", options, NULL)) >= 0) {
		switch (c) {
		case 'h':
			return cli_help(cli_cmds);
		case ARG_VERSION:
			puts(PACKAGE_STRING);
			return 0;
		case ARG_LOG_LEVEL:
			cli_max_sev = log_parse_arg(optarg);
			break;
		case ARG_JOURNAL_LEVEL:
			log_max_sev = log_parse_arg(optarg);
			break;
		case ARG_AUDIO:
			gst_audio_en = atoi(optarg);
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

/* vim: set tabstop=4 softtabstop=4 shiftwidth=4 noexpandtab : */
