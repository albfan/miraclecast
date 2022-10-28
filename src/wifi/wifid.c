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
#include <libudev.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-daemon.h>
#include <systemd/sd-event.h>
#include <time.h>
#include <unistd.h>
#include "shl_htable.h"
#include "shl_macro.h"
#include "shl_log.h"
#include "shl_util.h"
#include "util.h"
#include "wifid.h"
#include "config.h"

#define XSTR(x) STR(x)
#define STR(x) #x
const char *interface_name = NULL;
const char *config_methods = NULL;
unsigned int arg_wpa_loglevel = LOG_NOTICE;
bool arg_wpa_syslog = false;
bool use_dev = false;
bool lazy_managed = false;
const char *arg_ip_binary = NULL;


/*
 * Manager Handling
 */

struct link *manager_find_link(struct manager *m, unsigned int ifindex)
{
	unsigned int *elem;
	bool res;

	res = shl_htable_lookup_uint(&m->links, ifindex, &elem);
	if (!res)
		return NULL;

	return link_from_htable(elem);
}

struct link *manager_find_link_by_label(struct manager *m, const char *label)
{
	const char *next;
	unsigned int idx;
	int r;

	r = shl_atoi_u(label, 10, &next, &idx);
	if (r < 0 || *next)
		return NULL;

	return manager_find_link(m, idx);
}

static void manager_add_udev_link(struct manager *m,
				  struct udev_device *d)
{
	struct link *l;
	unsigned int ifindex;
	const char *ifname;
	int r;

	ifindex = ifindex_from_udev_device(d);
	if (!ifindex)
		return;

	ifname = udev_device_get_property_value(d, "INTERFACE");
	if (!ifname)
		return;

	if (interface_name && strcmp(interface_name, ifname)) {
		return;
	}

	/* ignore dynamic p2p interfaces */
	if (shl_startswith(ifname, "p2p-"))
		return;

	r = link_new(m, ifindex, ifname, &l);
	if (r < 0)
		return;

	if (m->friendly_name && l->managed)
		link_set_friendly_name(l, m->friendly_name);
	if (m->config_methods)
		link_set_config_methods(l, m->config_methods);

	if(use_dev)
		link_use_dev(l);
	if(arg_ip_binary)
		link_set_ip_binary(l, arg_ip_binary);

#ifdef RELY_UDEV
	bool managed = udev_device_has_tag(d, "miracle") && !lazy_managed;
#else
	bool managed = (!interface_name || !strcmp(interface_name, ifname)) && !lazy_managed;
#endif
	if (managed) {
		link_set_managed(l, true);
	} else {
		log_debug("ignored device: %s", ifname);
	}
}

static int manager_udev_fn(sd_event_source *source,
			   int fd,
			   uint32_t mask,
			   void *data)
{
	_cleanup_udev_device_ struct udev_device *d = NULL;
	struct manager *m = data;
	const char *action, *ifname;
	unsigned int ifindex;
	struct link *l;

	d = udev_monitor_receive_device(m->udev_mon);
	if (!d)
		return 0;

	ifindex = ifindex_from_udev_device(d);
	if (!ifindex)
		return 0;

	l = manager_find_link(m, ifindex);

	action = udev_device_get_action(d);
	if (action && !strcmp(action, "remove")) {
		if (l)
			link_free(l);
	} else if (l) {
		ifname = udev_device_get_property_value(d, "INTERFACE");
		if (action && !strcmp(action, "move")) {
			if (ifname)
				link_renamed(l, ifname);
		}

#ifdef RELY_UDEV
		if (udev_device_has_tag(d, "miracle") && !lazy_managed)
			link_set_managed(l, true);
		else
			link_set_managed(l, false);
#else
		if ((!interface_name || !strcmp(interface_name, ifname)) && !lazy_managed) {
			link_set_managed(l, true);
		} else {
			log_debug("ignored device: %s", ifname);
		}
#endif
	} else {
		manager_add_udev_link(m, d);
	}

	return 0;
}

static int manager_signal_fn(sd_event_source *source,
			     const struct signalfd_siginfo *ssi,
			     void *data)
{
	struct manager *m = data;

	if (ssi->ssi_signo == SIGCHLD) {
		log_debug("caught SIGCHLD for %ld, reaping child", (long)ssi->ssi_pid);
		waitid(P_PID, ssi->ssi_pid, NULL, WNOHANG|WEXITED);
		return 0;
	} else if (ssi->ssi_signo == SIGPIPE) {
		/* ignore SIGPIPE */
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

	manager_dbus_disconnect(m);

	shl_htable_clear_uint(&m->links, NULL, NULL);

	sd_event_source_unref(m->udev_mon_source);
	udev_monitor_unref(m->udev_mon);
	udev_unref(m->udev);

	for (i = 0; m->sigs[i]; ++i)
		sd_event_source_unref(m->sigs[i]);

	sd_bus_unref(m->bus);
	sd_event_unref(m->event);

	free(m->friendly_name);
	free(m->config_methods);
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
	char *cm;

	m = calloc(1, sizeof(*m));
	if (!m)
		return log_ENOMEM();

	shl_htable_init_uint(&m->links);


	if (config_methods) {
		cm = strdup(config_methods);
		if (!cm)
			return log_ENOMEM();

		free(m->config_methods);
		m->config_methods = cm;
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
					&m->sigs[i],
					sigs[i],
					manager_signal_fn,
					m);
		if (r < 0) {
			log_vERR(r);
			goto error;
		}

		/* low-priority to allow others to handle it first */
		sd_event_source_set_priority(m->sigs[i], 100);
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
			    &m->udev_mon_source,
			    udev_monitor_get_fd(m->udev_mon),
			    EPOLLHUP | EPOLLERR | EPOLLIN,
			    manager_udev_fn,
			    m);
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	r = manager_dbus_connect(m);
	if (r < 0)
		goto error;

	if (out)
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
	if (r < 0)
		goto error;

	r = sd_bus_message_enter_container(rep, 'v', "s");
	if (r < 0)
		goto error;

	r = sd_bus_message_read(rep, "s", &name);
	if (r < 0)
		name = "undefined";

	if (shl_isempty(name)) {
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
	_cleanup_udev_enumerate_ struct udev_enumerate *e = NULL;
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

		manager_add_udev_link(m, d);
		udev_device_unref(d);
	}

	return;

error:
	log_warning("cannot enumerate links via udev");
}

static int manager_startup(struct manager *m)
{
	int r;

	r = shl_mkdir_p_prefix("/run", "/run/miracle", 0755);
	if (r >= 0)
		r = shl_mkdir_p_prefix("/run/miracle",
				       "/run/miracle/wifi",
				       0700);
	if (r < 0) {
		log_error("cannot create maintenance directories in /run: %d",
			  r);
		return r;
	}

	manager_read_name(m);
	manager_read_links(m);

	return 0;
}

static int manager_run(struct manager *m)
{
	return sd_event_loop(m->event);
}

static int help(void)
{
	/*
	 * 80-char barrier:
	 *      01234567890123456789012345678901234567890123456789012345678901234567890123456789
	 */
	printf("%s [OPTIONS...] ...\n\n"
	       "Wifi Management Daemon.\n\n"
	       "  -h --help                Show this help\n"
	       "     --version             Show package version\n"
	       "     --log-level <lvl>     Maximum level for log messages\n"
	       "     --log-time            Prefix log-messages with timestamp\n"
	       "     --log-date-time       Prefix log-messages with date time\n"
	       "\n"
	       "  -i --interface           Choose the interface to use\n"
	       "     --config-methods      Define config methods for pairing, default 'pbc'\n"
	       "\n"
	       "     --wpa-loglevel <lvl>  wpa_supplicant log-level\n"
	       "     --wpa-syslog          wpa_supplicant use syslog\n"
	       "     --use-dev             enable workaround for 'no ifname' issue\n"
	       "     --lazy-managed        manage interface only when user decide to do\n"
	       "     --ip-binary <path>    path to 'ip' binary [default: "XSTR(IP_BINARY)"]\n"
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
		ARG_LOG_DATE_TIME,
		ARG_WPA_LOGLEVEL,
		ARG_WPA_SYSLOG,
		ARG_USE_DEV,
		ARG_CONFIG_METHODS,
		ARG_LAZY_MANAGED,
		ARG_IP_BINARY,
	};
	static const struct option options[] = {
		{ "help",       	no_argument,		NULL,	'h' },
		{ "version",	        no_argument,		NULL,	ARG_VERSION },
		{ "log-level",	        required_argument,	NULL,	ARG_LOG_LEVEL },
		{ "log-time",	        no_argument,		NULL,	ARG_LOG_TIME },
		{ "log-date-time",	no_argument,		NULL,	ARG_LOG_DATE_TIME },

		{ "wpa-loglevel",	required_argument,	NULL,	ARG_WPA_LOGLEVEL },
		{ "wpa-syslog",	no_argument,	NULL,	ARG_WPA_SYSLOG },
		{ "interface",	required_argument,	NULL,	'i' },
		{ "use-dev",	no_argument,	NULL,	ARG_USE_DEV },
		{ "config-methods",	required_argument,	NULL,	ARG_CONFIG_METHODS },
		{ "lazy-managed",	no_argument,	NULL,	ARG_LAZY_MANAGED },
		{ "ip-binary",	required_argument,	NULL,	ARG_IP_BINARY },
		{}
	};
	int c;

	while ((c = getopt_long(argc, argv, "hi:", options, NULL)) >= 0) {
		switch (c) {
		case 'h':
			return help();
		case 'i':
			interface_name = optarg;
			break;
		case ARG_VERSION:
			puts(PACKAGE_STRING);
			return 0;
		case ARG_LOG_LEVEL:
			log_max_sev = log_parse_arg(optarg);
			break;
		case ARG_LOG_TIME:
			log_init_time();
			break;
		case ARG_LOG_DATE_TIME:
			log_date_time = true;
			break;
		case ARG_USE_DEV:
			use_dev = true;
			break;
		case ARG_CONFIG_METHODS:
			config_methods = optarg;
			break;
		case ARG_LAZY_MANAGED:
			lazy_managed = true;
			break;
		case ARG_WPA_LOGLEVEL:
			arg_wpa_loglevel = log_parse_arg(optarg);
			break;
		case ARG_WPA_SYSLOG:
			arg_wpa_syslog = true;
			break;
		case ARG_IP_BINARY:
			arg_ip_binary = optarg;
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
		   "miracle-wifid - revision %s %s %s",
		   "1.0", __DATE__, __TIME__);

	return 1;
}

int main(int argc, char **argv)
{
	struct manager *m = NULL;
	int r;

	srand(time(NULL));

   GKeyFile* gkf = load_ini_file();

   if (gkf) {
      gchar* log_level;
      log_level = g_key_file_get_string (gkf, "wifid", "log-level", NULL);
      if (log_level) {
         log_max_sev = log_parse_arg(log_level);
         g_free(log_level);
      }
      g_key_file_free(gkf);
   }

	r = parse_argv(argc, argv);
	if (r < 0)
		return EXIT_FAILURE;
	if (!r)
		return EXIT_SUCCESS;

	if (getuid() != 0) {
      r = EACCES;
		log_notice("Must run as root");
      goto finish;
	}

	r = manager_new(&m);
	if (r < 0)
		goto finish;

	r = manager_startup(m);
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
