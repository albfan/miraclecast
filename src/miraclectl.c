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
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/signalfd.h>
#include <systemd/sd-bus.h>
#include "miracle.h"
#include "shl_log.h"
#include "shl_macro.h"
#include "shl_util.h"

/*
 * Helpers for interactive commands
 */

static sd_bus *cli_bus;
static sd_event *cli_event;
static sd_event_source *cli_sigs[_NSIG];

static int cli_signal_fn(sd_event_source *source,
			 const struct signalfd_siginfo *ssi,
			 void *data)
{
	if (ssi->ssi_signo == SIGCHLD) {
		log_debug("caught SIGCHLD for %d", (int)ssi->ssi_pid);
		return 0;
	}

	log_notice("caught signal %d, exiting..", (int)ssi->ssi_signo);
	sd_event_exit(cli_event, 0);

	return 0;
}

static void cli_destroy(void)
{
	unsigned int i;

	if (!cli_bus)
		return;

	for (i = 0; cli_sigs[i]; ++i) {
		sd_event_source_unref(cli_sigs[i]);
		cli_sigs[i] = NULL;
	}

	sd_event_unref(cli_event);
	cli_event = NULL;
	sd_bus_unref(cli_bus);
	cli_bus = NULL;
}

static int cli_init(sd_bus *bus)
{
	static const int sigs[] = {
		SIGINT, SIGTERM, SIGQUIT, SIGHUP, SIGPIPE, SIGCHLD, 0
	};
	unsigned int i;
	sigset_t mask;
	int r;

	if (cli_bus || !bus)
		return log_EINVAL();

	cli_bus = sd_bus_ref(bus);

	r = sd_event_default(&cli_event);
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	r = sd_bus_attach_event(cli_bus, cli_event, 0);
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	for (i = 0; sigs[i]; ++i) {
		sigemptyset(&mask);
		sigaddset(&mask, sigs[i]);
		sigprocmask(SIG_BLOCK, &mask, NULL);

		r = sd_event_add_signal(cli_event,
					sigs[i],
					cli_signal_fn,
					NULL,
					&cli_sigs[i]);
		if (r < 0) {
			log_vERR(r);
			goto error;
		}
	}

	return 0;

error:
	cli_destroy();
	return r;
}

static int cli_run(void)
{
	if (!cli_bus)
		return log_EINVAL();

	return sd_event_loop(cli_event);
}

/*
 * verb: list
 */

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

/*
 * verb: show-link
 */

static int verb_show_link(sd_bus *bus, char **args, unsigned int n)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_cleanup_sd_bus_message_ sd_bus_message *m = NULL;
	_cleanup_free_ char *path = NULL, *name = NULL;
	_cleanup_free_ char *type = NULL, *iface = NULL, *fname = NULL;
	const char *t;
	int r;

	name = sd_bus_label_escape(args[1]);
	if (!name)
		return log_ENOMEM();

	path = shl_strcat("/org/freedesktop/miracle/link/", name);
	if (!path)
		return log_ENOMEM();

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       path,
			       "org.freedesktop.DBus.Properties",
			       "GetAll",
			       &err,
			       &m,
			       "s", "org.freedesktop.miracle.Link");
	if (r < 0) {
		log_error("cannot retrieve link %s: %s",
			  args[1], bus_error_message(&err, r));
		return r;
	}

	r = sd_bus_message_enter_container(m, 'a', "{sv}");
	if (r < 0)
		return log_bus_parser(r);

	while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
		r = sd_bus_message_read(m, "s", &t);
		if (r < 0)
			return log_bus_parser(r);

		if (!strcmp(t, "Type")) {
			r = bus_message_read_basic_variant(m, "s", &t);
			if (r < 0)
				return log_bus_parser(r);

			free(type);
			type = strdup(t);
			if (!type)
				return log_ENOMEM();
		} else if (!strcmp(t, "Interface")) {
			r = bus_message_read_basic_variant(m, "s", &t);
			if (r < 0)
				return log_bus_parser(r);

			free(iface);
			iface = strdup(t);
			if (!iface)
				return log_ENOMEM();
		} else if (!strcmp(t, "Name")) {
			r = bus_message_read_basic_variant(m, "s", &t);
			if (r < 0)
				return log_bus_parser(r);

			free(fname);
			fname = strdup(t);
			if (!fname)
				return log_ENOMEM();
		} else {
			r = sd_bus_message_skip(m, "v");
			if (r < 0)
				return log_bus_parser(r);
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

	printf("Link=%s\n", args[1]);
	if (type)
		printf("Type=%s\n", type);
	if (iface)
		printf("Interface=%s\n", iface);
	if (fname)
		printf("Name=%s\n", fname);

	return 0;
}

/*
 * verb: show-peer
 */

static int verb_show_peer(sd_bus *bus, char **args, unsigned int n)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_cleanup_sd_bus_message_ sd_bus_message *m = NULL;
	_cleanup_free_ char *path = NULL, *name = NULL;
	_cleanup_free_ char *link = NULL, *fname = NULL, *iface = NULL;
	_cleanup_free_ char *laddr = NULL, *raddr = NULL;
	const char *t;
	int r, is_connected = false;

	name = sd_bus_label_escape(args[1]);
	if (!name)
		return log_ENOMEM();

	path = shl_strcat("/org/freedesktop/miracle/peer/", name);
	if (!path)
		return log_ENOMEM();

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       path,
			       "org.freedesktop.DBus.Properties",
			       "GetAll",
			       &err,
			       &m,
			       "s", "org.freedesktop.miracle.Peer");
	if (r < 0) {
		log_error("cannot retrieve peer %s: %s",
			  args[1], bus_error_message(&err, r));
		return r;
	}

	r = sd_bus_message_enter_container(m, 'a', "{sv}");
	if (r < 0)
		return log_bus_parser(r);

	while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
		r = sd_bus_message_read(m, "s", &t);
		if (r < 0)
			return log_bus_parser(r);

		if (!strcmp(t, "Link")) {
			r = bus_message_read_basic_variant(m, "o", &t);
			if (r < 0)
				return log_bus_parser(r);

			t = shl_startswith(t,
					   "/org/freedesktop/miracle/link/");
			if (t) {
				free(link);
				link = sd_bus_label_unescape(t);
				if (!link)
					return log_ENOMEM();
			}
		} else if (!strcmp(t, "Name")) {
			r = bus_message_read_basic_variant(m, "s", &t);
			if (r < 0)
				return log_bus_parser(r);

			free(fname);
			fname = strdup(t);
			if (!fname)
				return log_ENOMEM();
		} else if (!strcmp(t, "Connected")) {
			r = bus_message_read_basic_variant(m, "b",
							   &is_connected);
			if (r < 0)
				return log_bus_parser(r);
		} else if (!strcmp(t, "Interface")) {
			r = bus_message_read_basic_variant(m, "s", &t);
			if (r < 0)
				return log_bus_parser(r);

			free(iface);
			iface = strdup(t);
			if (!iface)
				return log_ENOMEM();
		} else if (!strcmp(t, "LocalAddress")) {
			r = bus_message_read_basic_variant(m, "s", &t);
			if (r < 0)
				return log_bus_parser(r);

			free(laddr);
			laddr = strdup(t);
			if (!laddr)
				return log_ENOMEM();
		} else if (!strcmp(t, "RemoteAddress")) {
			r = bus_message_read_basic_variant(m, "s", &t);
			if (r < 0)
				return log_bus_parser(r);

			free(raddr);
			raddr = strdup(t);
			if (!raddr)
				return log_ENOMEM();
		} else {
			r = sd_bus_message_skip(m, "v");
			if (r < 0)
				return log_bus_parser(r);
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

	printf("Peer=%s\n", args[1]);
	if (link)
		printf("Link=%s\n", link);
	if (fname)
		printf("Name=%s\n", fname);
	printf("Connected=%d\n", is_connected);
	if (iface)
		printf("Interface=%s\n", iface);
	if (laddr)
		printf("LocalAddress=%s\n", laddr);
	if (raddr)
		printf("RemoteAddress=%s\n", raddr);

	return 0;
}

/*
 * verb: add-link
 */

static int verb_add_link(sd_bus *bus, char **args, unsigned int n)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_cleanup_sd_bus_message_ sd_bus_message *m = NULL;
	_cleanup_free_ char *link = NULL, *type = NULL;
	const char *name;
	char *t, *iface;
	int r;

	type = strdup(args[1]);
	if (!type)
		return log_ENOMEM();

	t = strchr(type, ':');
	if (!t)
		return log_EINVAL();

	*t = 0;
	iface = t + 1;

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       "/org/freedesktop/miracle",
			       "org.freedesktop.miracle.Manager",
			       "AddLink",
			       &err,
			       &m,
			       "ss", type, iface);
	if (r < 0) {
		log_error("cannot add link %s:%s: %s",
			  type, iface, bus_error_message(&err, r));
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

/*
 * verb: remove-link
 */

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

/*
 * verb: set-link-name
 */

static int verb_set_link_name(sd_bus *bus, char **args, unsigned int n)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_cleanup_sd_bus_message_ sd_bus_message *m = NULL;
	_cleanup_free_ char *path = NULL, *name = NULL;
	int r;

	name = sd_bus_label_escape(args[1]);
	if (!name)
		return log_ENOMEM();

	path = shl_strcat("/org/freedesktop/miracle/link/", name);
	if (!path)
		return log_ENOMEM();

	r = sd_bus_message_new_method_call(bus,
					   "org.freedesktop.miracle",
					   path,
					   "org.freedesktop.DBus.Properties",
					   "Set",
					   &m);
	if (r < 0)
		return log_bus_create(r);

	r = sd_bus_message_append(m, "ss",
				  "org.freedesktop.miracle.Link", "Name");
	if (r < 0)
		return log_bus_create(r);

	r = sd_bus_message_open_container(m, 'v', "s");
	if (r < 0)
		return log_bus_create(r);

	r = sd_bus_message_append(m, "s", args[2]);
	if (r < 0)
		return log_bus_create(r);

	r = sd_bus_message_close_container(m);
	if (r < 0)
		return log_bus_create(r);

	r = sd_bus_call(bus, m, 0, &err, NULL);
	if (r < 0) {
		log_error("cannot set friendly-name to %s on link %s: %s",
			  args[2], args[1], bus_error_message(&err, r));
		return r;
	}

	printf("Friendly-name set to %s on link %s\n", args[2], args[1]);
	return 0;
}

/*
 * verb: start-scan
 */

static int verb_start_scan(sd_bus *bus, char **args, unsigned int n)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_cleanup_free_ char *path = NULL, *name = NULL;
	int r;

	name = sd_bus_label_escape(args[1]);
	if (!name)
		return log_ENOMEM();

	path = shl_strcat("/org/freedesktop/miracle/link/", name);
	if (!path)
		return log_ENOMEM();

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       path,
			       "org.freedesktop.miracle.Link",
			       "StartScan",
			       &err,
			       NULL,
			       NULL);
	if (r < 0) {
		log_error("cannot start scan on link %s: %s",
			  args[1], bus_error_message(&err, r));
		return r;
	}

	printf("Scan on link %s started\n", args[1]);
	return 0;
}

/*
 * verb: stop-scan
 */

static int verb_stop_scan(sd_bus *bus, char **args, unsigned int n)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_cleanup_free_ char *path = NULL, *name = NULL;
	int r;

	name = sd_bus_label_escape(args[1]);
	if (!name)
		return log_ENOMEM();

	path = shl_strcat("/org/freedesktop/miracle/link/", name);
	if (!path)
		return log_ENOMEM();

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       path,
			       "org.freedesktop.miracle.Link",
			       "StopScan",
			       &err,
			       NULL,
			       NULL);
	if (r < 0) {
		log_error("cannot stop scan on link %s: %s",
			  args[1], bus_error_message(&err, r));
		return r;
	}

	printf("Scan on link %s stopped\n", args[1]);
	return 0;
}

/*
 * verb: scan
 */

static int verb_scan_list_peer(sd_bus *bus,
			       sd_bus_message *m,
			       const char *link_filter,
			       const char *peer)
{
	const char *obj, *link = NULL, *name = NULL;
	int r, connected = false;

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
				r = bus_message_read_basic_variant(m, "o",
								   &obj);
				if (r < 0)
					return log_bus_parser(r);

				obj = shl_startswith(obj,
					"/org/freedesktop/miracle/link/");
				if (obj)
					link = obj;
			} else if (!strcmp(obj, "Name")) {
				r = bus_message_read_basic_variant(m, "s",
								   &name);
				if (r < 0)
					return log_bus_parser(r);
			} else if (!strcmp(obj, "Connected")) {
				r = bus_message_read_basic_variant(m, "b",
								   &connected);
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

	if (!strcmp(link, link_filter))
		printf("%4s %-24s %-4s\n",
		       peer, name, connected ? "yes" : "no");

	return 0;
}

static int verb_scan_list(sd_bus *bus, const char *link)
{
	_cleanup_sd_bus_message_ sd_bus_message *m = NULL;
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_cleanup_free_ char *peer = NULL;
	const char *obj;
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

		r = verb_scan_list_peer(bus, m, link, peer);
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

	return 0;
}

static int verb_scan_match_fn(sd_bus *bus,
			      sd_bus_message *m,
			      void *data,
			      sd_bus_error *err)
{
	_cleanup_free_ char *peer = NULL;
	const char *obj, *link = data;
	int r;

	r = sd_bus_message_read(m, "o", &obj);
	if (r < 0)
		return log_bus_parser(r);

	obj = shl_startswith(obj, "/org/freedesktop/miracle/peer/");
	if (!obj)
		return 0;

	peer = sd_bus_label_unescape(obj);
	if (!peer)
		return log_ENOMEM();

	r = verb_scan_list_peer(bus, m, link, peer);
	if (r < 0)
		return r;

	return 0;
}

static void verb_scan_listen(sd_bus *bus, const char *link)
{
	int r;

	r = sd_bus_add_match(bus,
			     "type='signal',"
			     "sender='org.freedesktop.miracle',"
			     "interface='org.freedesktop.DBus.ObjectManager',"
			     "member='InterfacesAdded'",
			     verb_scan_match_fn,
			     (void*)link);
	if (r < 0)
		return log_error("cannot add dbus match: %d", r);

	r = cli_init(bus);
	if (r < 0)
		goto error;

	verb_scan_list(bus, link);
	cli_run();

error:
	cli_destroy();
	sd_bus_remove_match(bus,
			    "type='signal',"
			    "sender='org.freedesktop.miracle',"
			    "interface='org.freedesktop.DBus.ObjectManager'",
			    verb_scan_match_fn,
			    NULL);
}

static int verb_scan(sd_bus *bus, char **args, unsigned int n)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_cleanup_free_ char *path = NULL, *name = NULL;
	int r;

	name = sd_bus_label_escape(args[1]);
	if (!name)
		return log_ENOMEM();

	path = shl_strcat("/org/freedesktop/miracle/link/", name);
	if (!path)
		return log_ENOMEM();

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       path,
			       "org.freedesktop.miracle.Link",
			       "StartScan",
			       &err,
			       NULL,
			       NULL);
	if (r < 0)
		log_warning("cannot start scan on link %s (already running?): %s",
			    args[1], bus_error_message(&err, r));

	printf("Scan on link %s started, listing peers..\n", args[1]);
	printf("%4s %-24s %-4s\n", "ID", "NAME", "CONNECTED");
	verb_scan_listen(bus, name);

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       path,
			       "org.freedesktop.miracle.Link",
			       "StopScan",
			       &err,
			       NULL,
			       NULL);
	if (r < 0)
		log_error("cannot stop scan on link %s: %s",
			  args[1], bus_error_message(&err, r));
	else
		printf("Scan on link %s stopped\n", args[1]);

	return 0;
}

/*
 * main
 */

static int help(void)
{
	/*
	 * 80-char barrier:
	 *      01234567890123456789012345678901234567890123456789012345678901234567890123456789
	 */
	printf("%s [OPTIONS...] {COMMAND} ...\n\n"
	       "Send control command to or query the MiracleCast manager.\n\n"
	       "  -h --help             Show this help\n"
	       "     --version          Show package version\n"
	       "     --log-level <lvl>  Maximum level for log messages\n"
	       "     --log-time         Prefix log-messages with timestamp\n"
	       "\n"
	       "Commands:\n"
	       "  list                            List managed links and their peers\n"
	       "  show-link LINK...               Show details of given link\n"
	       "  show-peer PEER...               Show details of given peer\n"
	       "  add-link LINK...                Start managing the given link\n"
	       "  remove-link LINK...             Stop managing the given link\n"
	       "  set-link-name LINK NAME         Set friendly-name of given link\n"
	       "  start-scan LINK...              Start peer discovery on given link\n"
	       "  stop-scan LINK...               Stop peer discovery on given link\n"
	       "  scan LINK...                    Interactive scan on given link\n"
	       , program_invocation_short_name);
	/*
	 * 80-char barrier:
	 *      01234567890123456789012345678901234567890123456789012345678901234567890123456789
	 */

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
		{ "show-link",		EQUAL,	2,	verb_show_link },
		{ "show-peer",		EQUAL,	2,	verb_show_peer },
		{ "add-link",		EQUAL,	2,	verb_add_link },
		{ "remove-link",	EQUAL,	2,	verb_remove_link },
		{ "set-link-name",	EQUAL,	3,	verb_set_link_name },
		{ "start-scan",		EQUAL,	2,	verb_start_scan },
		{ "stop-scan",		EQUAL,	2,	verb_stop_scan },
		{ "scan",		EQUAL,	2,	verb_scan },
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
