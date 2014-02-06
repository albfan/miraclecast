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
#include <locale.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <systemd/sd-bus.h>
#include "miracle.h"
#include "shl_log.h"
#include "shl_macro.h"
#include "shl_util.h"

static int verb_list_links(sd_bus *bus, sd_bus_message *m)
{
	char *link;
	unsigned int link_cnt = 0;
	const char *obj;
	int r;

	r = sd_bus_message_enter_container(m, 'a', "{oa{sa{sv}}}");
	if (r < 0)
		return log_bus_parser(r);

	while ((r = sd_bus_message_enter_container(m,
						   'e',
						   "oa{sa{sv}}")) > 0) {
		r = sd_bus_message_read(m, "o", &obj);
		if (r < 0)
			return log_bus_parser(r);

		obj = shl_startswith(obj, "/org/freedesktop/miracle/link/");
		if (obj) {
			link = sd_bus_label_unescape(obj);
			if (!link)
				return log_ENOMEM();

			printf("%24s %-9s\n", link, "");

			free(link);
			++link_cnt;
		}

		r = sd_bus_message_skip(m, "a{sa{sv}}");
		if (r < 0)
			return log_bus_parser(r);

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return log_bus_parser(r);
	}
	if (r < 0)
		return log_bus_parser(r);

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return log_bus_parser(r);

	return link_cnt;
}

static int verb_list_peer(sd_bus *bus, sd_bus_message *m, const char *peer)
{
	_cleanup_free_ char *link = NULL;
	const char *obj;
	int r;

	r = sd_bus_message_enter_container(m, 'a', "{sa{sv}}");
	if (r < 0)
		return log_bus_parser(r);

	while ((r = sd_bus_message_enter_container(m,
						   'e',
						   "sa{sv}")) > 0) {
		r = sd_bus_message_read(m, "s", &obj);
		if (r < 0)
			return log_bus_parser(r);

		if (strcmp(obj, "org.freedesktop.miracle.Peer")) {
			r = sd_bus_message_skip(m, "a{sv}");
			if (r < 0)
				return log_bus_parser(r);
			r = sd_bus_message_exit_container(m);
			if (r < 0)
				return log_bus_parser(r);
			continue;
		}

		r = sd_bus_message_enter_container(m, 'a', "{sv}");
		if (r < 0)
			return log_bus_parser(r);

		while ((r = sd_bus_message_enter_container(m,
							   'e',
							   "sv")) > 0) {
			r = sd_bus_message_read(m, "s", &obj);
			if (r < 0)
				return log_bus_parser(r);

			if (!strcmp(obj, "Link")) {
				r = sd_bus_message_enter_container(m,
								   'v',
								   "o");
				if (r < 0)
					return log_bus_parser(r);

				r = sd_bus_message_read(m, "o", &obj);
				if (r < 0)
					return log_bus_parser(r);

				obj = shl_startswith(obj,
					"/org/freedesktop/miracle/link/");
				if (obj) {
					free(link);
					link = sd_bus_label_unescape(obj);
					if (!link)
						return log_ENOMEM();
				}

				r = sd_bus_message_exit_container(m);
				if (r < 0)
					return log_bus_parser(r);
			} else {
				sd_bus_message_skip(m, "v");
			}

			r = sd_bus_message_exit_container(m);
			if (r < 0)
				return log_bus_parser(r);
		}
		if (r < 0)
			return log_bus_parser(r);

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return log_bus_parser(r);

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return log_bus_parser(r);
	}
	if (r < 0)
		return log_bus_parser(r);

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return log_bus_parser(r);

	printf("%24s %-9s\n", link ? : "<none>", peer);

	return 0;
}

static int verb_list_peers(sd_bus *bus, sd_bus_message *m)
{
	_cleanup_free_ char *peer = NULL;
	unsigned int peer_cnt = 0;
	const char *obj;
	int r;

	r = sd_bus_message_enter_container(m, 'a', "{oa{sa{sv}}}");
	if (r < 0)
		return log_bus_parser(r);

	while ((r = sd_bus_message_enter_container(m,
						   'e',
						   "oa{sa{sv}}")) > 0) {
		r = sd_bus_message_read(m, "o", &obj);
		if (r < 0)
			return log_bus_parser(r);

		obj = shl_startswith(obj, "/org/freedesktop/miracle/peer/");
		if (!obj) {
			r = sd_bus_message_skip(m, "a{sa{sv}}");
			if (r < 0)
				return log_bus_parser(r);
			r = sd_bus_message_exit_container(m);
			if (r < 0)
				return log_bus_parser(r);
			continue;
		}

		free(peer);
		peer = sd_bus_label_unescape(obj);
		if (!peer)
			return log_ENOMEM();

		++peer_cnt;
		r = verb_list_peer(bus, m, peer);
		if (r < 0)
			return r;

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return log_bus_parser(r);
	}
	if (r < 0)
		return log_bus_parser(r);

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return log_bus_parser(r);

	return peer_cnt;
}

static int verb_list(sd_bus *bus, char **args, unsigned int n)
{
	_cleanup_sd_bus_message_ sd_bus_message *m = NULL;
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	unsigned int link_cnt, peer_cnt;
	int r;

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       "/org/freedesktop/miracle",
			       "org.freedesktop.DBus.ObjectManager",
			       "GetManagedObjects",
			       &err,
			       &m,
			       "");
	if (r < 0) {
		log_error("cannot retrieve objects: %s",
			  bus_error_message(&err, r));
		return r;
	}

	printf("%24s %-9s\n", "LINK", "PEER");

	/* print links */

	r = verb_list_links(bus, m);
	if (r < 0)
		return r;
	link_cnt = r;

	sd_bus_message_rewind(m, true);
	if (link_cnt > 0)
		printf("\n");

	/* print peers */

	r = verb_list_peers(bus, m);
	if (r < 0)
		return r;
	peer_cnt = r;

	if (peer_cnt > 0 || !link_cnt)
		printf("\n");

	/* print stats */

	printf(" %u peers and %u links listed.\n", peer_cnt, link_cnt);

	return 0;
}

static int verb_add_link(sd_bus *bus, char **args, unsigned int n)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_cleanup_sd_bus_message_ sd_bus_message *m = NULL;
	_cleanup_free_ char *link = NULL;
	const char *name;
	int r;

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       "/org/freedesktop/miracle",
			       "org.freedesktop.miracle.Manager",
			       "AddLink",
			       &err,
			       &m,
			       "ss", args[1], args[2]);
	if (r < 0) {
		log_error("cannot add link %s:%s: %s",
			  args[1], args[2], bus_error_message(&err, r));
		return r;
	}

	r = sd_bus_message_read(m, "s", &name);
	if (r < 0)
		return log_bus_parser(r);

	link = sd_bus_label_unescape(name);
	if (!link)
		return log_ENOMEM();

	printf("Link added as %s\n", link);
	return 0;
}

static int verb_remove_link(sd_bus *bus, char **args, unsigned int n)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	int r;

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       "/org/freedesktop/miracle",
			       "org.freedesktop.miracle.Manager",
			       "RemoveLink",
			       &err,
			       NULL,
			       "s", args[1]);
	if (r < 0) {
		log_error("cannot remove link %s: %s",
			  args[1], bus_error_message(&err, r));
		return r;
	}

	printf("Link %s removed\n", args[1]);
	return 0;
}

static int help(void)
{
	printf("%s [OPTIONS...] {COMMAND} ...\n\n"
	       "Send control command to or query the MiracleCast manager.\n\n"
	       "  -h --help             Show this help\n"
	       "     --version          Show package version\n"
	       "     --log-level <lvl>  Maximum level for log messages\n"
	       "     --log-time         Prefix log-messages with timestamp\n"
	       "\n"
	       "Commands:\n"
	       "  list                  List managed links and their peers\n"
	       "  add-link LINK...      Start managing the given link\n"
	       "  remove-link LINK...   Stop managing the given link\n"
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

	return 1;
}

static int miraclectl_main(sd_bus *bus, int argc, char *argv[])
{
	static const struct {
		const char *verb;
		const enum {
			MORE,
			LESS,
			EQUAL
		} argc_cmp;
		const int argc;
		int (*dispatch) (sd_bus *bus, char **args, unsigned int n);
	} verbs[] = {
		{ "list",		LESS,	1,	verb_list },
		{ "add-link",		EQUAL,	3,	verb_add_link },
		{ "remove-link",	EQUAL,	2,	verb_remove_link },
	};
	int left;
	unsigned int i;

	left = argc - optind;
	if (left <= 0) {
		/* no argument means "list" */
		i = 0;
	} else {
		if (!strcmp(argv[optind], "help")) {
			help();
			return 0;
		}

		for (i = 0; i < SHL_ARRAY_LENGTH(verbs); i++)
			if (!strcmp(argv[optind], verbs[i].verb))
				break;

		if (i >= SHL_ARRAY_LENGTH(verbs)) {
			log_error("unknown operation %s", argv[optind]);
			return -EINVAL;
		}
	}

	switch (verbs[i].argc_cmp) {
	case EQUAL:
		if (left != verbs[i].argc) {
			log_error("invalid number of arguments");
			return -EINVAL;
		}

		break;
	case MORE:
		if (left < verbs[i].argc) {
			log_error("too few arguments");
			return -EINVAL;
		}

		break;
	case LESS:
		if (left > verbs[i].argc) {
			log_error("too many arguments");
			return -EINVAL;
		}

		break;
	}

	return verbs[i].dispatch(bus, argv + optind, left);
}

int main(int argc, char **argv)
{
	sd_bus *bus;
	int r;

	setlocale(LC_ALL, "");

	r = parse_argv(argc, argv);
	if (r < 0)
		return EXIT_FAILURE;
	if (!r)
		return EXIT_SUCCESS;

	r = sd_bus_default_system(&bus);
	if (r < 0) {
		log_error("cannot connect to system bus: %s", strerror(-r));
		return EXIT_FAILURE;
	}

	r = miraclectl_main(bus, argc, argv);
	sd_bus_unref(bus);

	return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
