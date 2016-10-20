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
#include <systemd/sd-bus.h>
#include "ctl.h"
#include "shl_macro.h"
#include "shl_util.h"
#include "util.h"
#include "config.h"

static sd_bus *bus;
static struct ctl_wifi *wifi;

static struct ctl_link *selected_link;

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

	cli_printf("%6s %-24s %-30s %-10s\n",
		   "LINK", "INTERFACE", "FRIENDLY-NAME", "MANAGED");

	shl_dlist_for_each(i, &wifi->links) {
		l = link_from_dlist(i);
		++link_cnt;

		cli_printf("%6s %-24s %-30s %-10s\n",
			   l->label,
			   shl_isempty(l->ifname) ?
			       "<unknown>" : l->ifname,
			   shl_isempty(l->friendly_name) ?
			       "<unknown>" : l->friendly_name,
			   l->managed ? "yes": "no");
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
 * cmd: select
 */

static int cmd_select(char **args, unsigned int n)
{
	struct ctl_link *l;

	if (!n) {
		if (selected_link) {
			cli_printf("link %s deselected\n",
				   selected_link->label);
			selected_link = NULL;
		}

		return 0;
	}

	l = ctl_wifi_search_link(wifi, args[0]);
	if (!l) {
		cli_error("unknown link %s", args[0]);
		return 0;
	}

	selected_link = l;
	cli_printf("link %s selected\n", selected_link->label);

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
	} else {
		l = selected_link;
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
		cli_printf("Managed=%d\n", l->managed);
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

	l = l ? : selected_link;
	if (!l) {
		cli_error("no link selected");
		return 0;
	}

	if (!l->managed) {
		cli_printf("link %s not managed\n", l->label);
		return 0;
	}

	return ctl_link_set_friendly_name(l, name);
}

/*
 * cmd: set-managed
 */

static int cmd_set_managed(char **args, unsigned int n)
{
	struct ctl_link *l = NULL;
	const char *value;
	bool managed = true;

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

		value = args[1];
	} else {
		value = args[0];
	}

	l = l ? : selected_link;
	if (!l) {
		cli_error("no link selected");
		return 0;
	}

	if (!strcmp(value, "no")) {
		managed = false;
	}
	return ctl_link_set_managed(l, managed);
}

/*
 * cmd: p2p-scan
 */

static int cmd_p2p_scan(char **args, unsigned int n)
{
	struct ctl_link *l = NULL;
	unsigned int i;
	bool stop = false;

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

	l = l ? : selected_link;
	if (!l) {
		cli_error("no link selected");
		return 0;
	}

	if (!l->managed) {
		cli_printf("link %s not managed\n", l->label);
		return 0;
	}

	return ctl_link_set_p2p_scanning(l, !stop);
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

	if (!p->l->managed) {
		cli_printf("link %s not managed\n", p->l->label);
		return 0;
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

	if (!p->l->managed) {
		cli_printf("link %s not managed\n", p->l->label);
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

static const struct cli_cmd cli_cmds[] = {
	{ "list",		NULL,					CLI_M,	CLI_LESS,	0,	cmd_list,		"List all objects" },
	{ "select",		"[link]",				CLI_Y,	CLI_LESS,	1,	cmd_select,		"Select default link" },
	{ "show",		"[link|peer]",				CLI_M,	CLI_LESS,	1,	cmd_show,		"Show detailed object information" },
	{ "set-friendly-name",	"[link] <name>",			CLI_M,	CLI_LESS,	2,	cmd_set_friendly_name,	"Set friendly name of an object" },
	{ "set-managed",	"[link] <yes|no>",	CLI_M,	CLI_LESS,	2,	cmd_set_managed,	"Manage or unmnage a link" },
	{ "p2p-scan",		"[link] [stop]",			CLI_Y,	CLI_LESS,	2,	cmd_p2p_scan,		"Control neighborhood P2P scanning" },
	{ "connect",		"<peer> [provision] [pin]",		CLI_M,	CLI_LESS,	3,	cmd_connect,		"Connect to peer" },
	{ "disconnect",		"<peer>",				CLI_M,	CLI_EQUAL,	1,	cmd_disconnect,		"Disconnect from peer" },
	{ "quit",		NULL,					CLI_Y,	CLI_MORE,	0,	cmd_quit,		"Quit program" },
	{ "exit",		NULL,					CLI_Y,	CLI_MORE,	0,	cmd_quit,		NULL },
	{ "help",		NULL,					CLI_M,	CLI_MORE,	0,	NULL,			"Print help" },
	{ },
};

void ctl_fn_peer_new(struct ctl_peer *p)
{
	if (cli_running())
		cli_printf("[" CLI_GREEN "ADD" CLI_DEFAULT "] Peer: %s\n",
			   p->label);
}

void ctl_fn_peer_free(struct ctl_peer *p)
{
	if (cli_running())
		cli_printf("[" CLI_RED "REMOVE" CLI_DEFAULT "] Peer: %s\n",
			   p->label);
}

void ctl_fn_peer_provision_discovery(struct ctl_peer *p,
				     const char *prov,
				     const char *pin)
{
	if (cli_running())
		cli_printf("[" CLI_YELLOW "PROV" CLI_DEFAULT "] Peer: %s Type: %s PIN: %s\n",
			   p->label, prov, pin);
}
void ctl_fn_peer_go_neg_request(struct ctl_peer *p,
				     const char *prov,
				     const char *pin)
{
}
void ctl_fn_peer_formation_failure(struct ctl_peer *p, const char *reason)
{
	if (cli_running())
		cli_printf("[" CLI_YELLOW "FAIL" CLI_DEFAULT "] Peer: %s Reason: %s\n",
			   p->label, reason);
}

void ctl_fn_peer_connected(struct ctl_peer *p)
{
	if (cli_running())
		cli_printf("[" CLI_GREEN "CONNECT" CLI_DEFAULT "] Peer: %s\n",
			   p->label);
}

void ctl_fn_peer_disconnected(struct ctl_peer *p)
{
	if (cli_running())
		cli_printf("[" CLI_YELLOW "DISCONNECT" CLI_DEFAULT "] Peer: %s\n",
			   p->label);
}

void ctl_fn_link_new(struct ctl_link *l)
{
	if (cli_running())
		cli_printf("[" CLI_GREEN "ADD" CLI_DEFAULT "] Link: %s\n",
			   l->label);
}

void ctl_fn_link_free(struct ctl_link *l)
{
	if (l == selected_link) {
		cli_printf("link %s deselected\n",
			   selected_link->label);
		selected_link = NULL;
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
	printf("%s [OPTIONS...] {COMMAND} ...\n\n"
	       "Send control command to or query the MiracleCast Wifi-Manager. If no arguments\n"
	       "are given, an interactive command-line tool is provided.\n\n"
	       "  -h --help                      Show this help\n"
	       "     --help-commands             Show avaliable commands\n"
	       "     --version                   Show package version\n"
	       "     --log-level <lvl>           Maximum level for log messages\n"
	       "     --log-journal-level <lvl>   Maximum level for journal log messages\n"
	       "\n"
	       "Commands:\n"
	       , program_invocation_short_name);
	/*
	 * 80-char barrier:
	 *      01234567890123456789012345678901234567890123456789012345678901234567890123456789
	 */
}

static int ctl_interactive(void)
{
	struct shl_dlist *i;
	struct ctl_link *l;
	int r;

	r = cli_init(bus, cli_cmds);
	if (r < 0)
		return r;

	r = ctl_wifi_fetch(wifi);
	if (r < 0)
		goto error;

	r = cli_run();

	/* stop managed scans, but only in interactive mode */
	shl_dlist_for_each(i, &wifi->links) {
		l = link_from_dlist(i);
		if (l->have_p2p_scan)
			ctl_link_set_p2p_scanning(l, false);
	}

error:
	cli_destroy();
	return r;
}

static int ctl_single(char **argv, int argc)
{
	int r;

	r = ctl_wifi_fetch(wifi);
	if (r < 0)
		return r;

	r = cli_do(cli_cmds, argv, argc);
	if (r == -EAGAIN)
		cli_error("unknown operation %s", argv[0]);

	return r;
}

static int ctl_main(int argc, char *argv[])
{
	int r, left;

	r = ctl_wifi_new(&wifi, bus);
	if (r < 0)
		return r;

	left = argc - optind;
	if (left <= 0)
		r = ctl_interactive();
	else
		r = ctl_single(argv + optind, left);

	ctl_wifi_free(wifi);
	return r;
}

static int parse_argv(int argc, char *argv[])
{
	enum {
		ARG_VERSION = 0x100,
		ARG_LOG_LEVEL,
		ARG_JOURNAL_LEVEL,
      ARG_HELP_COMMANDS,
	};
	static const struct option options[] = {
		{ "help",	no_argument,		NULL,	'h' },
		{ "help-commands",	no_argument,		NULL,	ARG_HELP_COMMANDS },
		{ "version",	no_argument,		NULL,	ARG_VERSION },
		{ "log-level",	required_argument,	NULL,	ARG_LOG_LEVEL },
		{ "log-journal-level",	required_argument,	NULL,	ARG_JOURNAL_LEVEL },
		{}
	};
	int c;

	while ((c = getopt_long(argc, argv, "h", options, NULL)) >= 0) {
		switch (c) {
		case 'h':
		   cli_fn_help();
			return 0;
		case ARG_HELP_COMMANDS:
			return cli_help(cli_cmds, 20);
		case ARG_VERSION:
			puts(PACKAGE_STRING);
			return 0;
		case ARG_LOG_LEVEL:
			cli_max_sev = log_parse_arg(optarg);
			break;
		case ARG_JOURNAL_LEVEL:
			log_max_sev = log_parse_arg(optarg);
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

   GKeyFile* gkf = load_ini_file();

   if (gkf) {
      gchar* log_level;
      log_level = g_key_file_get_string (gkf, "wifictl", "log-journal-level", NULL);
      if (log_level) {
         log_max_sev = log_parse_arg(log_level);
         g_free(log_level);
      }
      log_level = g_key_file_get_string (gkf, "wifictl", "log-level", NULL);
      if (log_level) {
         cli_max_sev = log_parse_arg(log_level);
         g_free(log_level);
      }
      g_key_file_free(gkf);
   }

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
