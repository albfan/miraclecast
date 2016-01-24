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
#include "shl_macro.h"
#include "shl_log.h"
#include "shl_util.h"
#include "config.h"

static void manager_free(struct manager *m)
{
	if (!m)
		return;

	free(m);
}

static int manager_new(struct manager **out)
{
	return -EINVAL;
}

static int manager_run(struct manager *m)
{
	return 0;
}

static int help(void)
{
	/*
	 * 80-char barrier:
	 *      01234567890123456789012345678901234567890123456789012345678901234567890123456789
	 */
	printf("%s [OPTIONS...] ...\n\n"
	       "Remote-display Management-daemon.\n\n"
	       "  -h --help             Show this help\n"
	       "     --version          Show package version\n"
	       "     --log-level <lvl>  Maximum level for log messages\n"
	       "     --log-time         Prefix log-messages with timestamp\n"
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
			log_max_sev = log_parse_arg(optarg);
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
		   "1.0", __DATE__, __TIME__);

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
