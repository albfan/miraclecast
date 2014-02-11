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
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/signalfd.h>
#include <systemd/sd-bus.h>
#include "miracle.h"
#include "shl_macro.h"
#include "shl_util.h"

/* *sigh* readline doesn't include all their deps, so put them last */
#include <readline/history.h>
#include <readline/readline.h>

static sd_bus *bus;
static char *selected_link;

/*
 * Helpers for interactive commands
 */

static sd_event *cli_event;
static sd_event_source *cli_sigs[_NSIG];
static sd_event_source *cli_stdin;
static bool cli_rl;
static int cli_max_sev = LOG_NOTICE;

#define CLI_DEFAULT		"\x1B[0m"
#define CLI_RED			"\x1B[0;91m"
#define CLI_GREEN		"\x1B[0;92m"
#define CLI_YELLOW		"\x1B[0;93m"
#define CLI_BLUE		"\x1B[0;94m"
#define CLI_BOLDGRAY		"\x1B[1;30m"
#define CLI_BOLDWHITE		"\x1B[1;37m"
#define CLI_PROMPT		CLI_BLUE "[miraclectl] # " CLI_DEFAULT

static const struct cli_cmd {
	const char *cmd;
	const char *args;
	enum {
		CLI_N,	/* no */
		CLI_M,	/* maybe */
		CLI_Y,	/* yes */
	} cli_cmp;
	enum {
		MORE,
		LESS,
		EQUAL,
	} argc_cmp;
	int argc;
	int (*fn) (char **args, unsigned int n);
	const char *desc;
} cli_cmds[];

static bool is_cli(void)
{
	return cli_rl;
}

static void cli_printv(const char *fmt, va_list args)
{
	SHL_PROTECT_ERRNO;
	_shl_cleanup_free_ char *line = NULL;
	int point;
	bool async;

	/* In case we print messages during readline() activity, we need to
	 * correctly save and restore RL-internal state. */
	async = is_cli() && !RL_ISSTATE(RL_STATE_DONE);

	if (async) {
		point = rl_point;
		line = rl_copy_text(0, rl_end);
		rl_save_prompt();
		rl_replace_line("", 0);
		rl_redisplay();
	}

	vprintf(fmt, args);

	if (async) {
		rl_restore_prompt();
		rl_replace_line(line, 0);
		rl_point = point;
		rl_redisplay();
	}
}

static void cli_printf(const char *fmt, ...)
{
	SHL_PROTECT_ERRNO;
	va_list args;

	va_start(args, fmt);
	cli_printv(fmt, args);
	va_end(args);
}

#define cli_log(_fmt, ...) \
	cli_printf(_fmt "\n", ##__VA_ARGS__)
#define cli_log_fn(_fmt, ...) \
	cli_printf(_fmt " (%s() in %s:%d)\n", ##__VA_ARGS__, __func__, __FILE__, __LINE__)
#define cli_error(_fmt, ...) \
	((LOG_ERROR <= cli_max_sev) ? \
	cli_log_fn("ERROR: " _fmt, ##__VA_ARGS__) : (void)0)
#define cli_warning(_fmt, ...) \
	((LOG_WARNING <= cli_max_sev) ? \
	cli_log_fn("WARNING: " _fmt, ##__VA_ARGS__) : (void)0)
#define cli_notice(_fmt, ...) \
	((LOG_NOTICE <= cli_max_sev) ? \
	cli_log("NOTICE: " _fmt, ##__VA_ARGS__) : (void)0)
#define cli_debug(_fmt, ...) \
	((LOG_DEBUG <= cli_max_sev) ? \
	cli_log_fn("DEBUG: " _fmt, ##__VA_ARGS__) : (void)0)

#define cli_EINVAL() \
	(cli_error("invalid arguments"), -EINVAL)
#define cli_vEINVAL() \
	((void)cli_EINVAL())

#define cli_EFAULT() \
	(cli_error("internal operation failed"), -EFAULT)
#define cli_vEFAULT() \
	((void)cli_EFAULT())

#define cli_ENOMEM() \
	(cli_error("out of memory"), -ENOMEM)
#define cli_vENOMEM() \
	((void)cli_ENOMEM())

#define cli_EPIPE() \
	(cli_error("fd closed unexpectedly"), -EPIPE)
#define cli_vEPIPE() \
	((void)cli_EPIPE())

#define cli_ERRNO() \
	(cli_error("syscall failed (%d): %m", errno), -errno)
#define cli_vERRNO() \
	((void)cli_ERRNO())

#define cli_ERR(_r) \
	(errno = -(_r), cli_error("syscall failed (%d): %m", (_r)), (_r))
#define cli_vERR(_r) \
	((void)cli_ERR(_r))

#define cli_log_parser(_r) \
	(cli_error("cannot parse dbus message: %s", \
		   strerror((_r) < 0 ? -(_r) : (_r))), (_r))

static int cli_help(void)
{
	unsigned int i;

	if (is_cli()) {
		cli_printf("Available commands:\n");
	} else {
		/*
		 * 80-char barrier:
		 *      01234567890123456789012345678901234567890123456789012345678901234567890123456789
		 */
		printf("%s [OPTIONS...] {COMMAND} ...\n\n"
		       "Send control command to or query the MiracleCast manager. If no arguments are\n"
		       "given, an interactive command-line tool is provided.\n\n"
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

	for (i = 0; cli_cmds[i].cmd; ++i) {
		if (!cli_cmds[i].desc)
			continue;
		if (is_cli() && cli_cmds[i].cli_cmp == CLI_N)
			continue;
		if (!is_cli() && cli_cmds[i].cli_cmp == CLI_Y)
			continue;

		cli_printf("  %s %-*s %s\n",
			   cli_cmds[i].cmd,
			   (int)(25 - strlen(cli_cmds[i].cmd)),
			   cli_cmds[i].args ? : "",
			   cli_cmds[i].desc ? : "");
	}

	return 0;
}

static int cli_do(char **args, unsigned int n)
{
	unsigned int i;
	const char *cmd;
	int r;

	if (!n)
		return -EAGAIN;

	cmd = *args++;
	--n;

	for (i = 0; cli_cmds[i].cmd; ++i) {
		if (strcmp(cmd, cli_cmds[i].cmd))
			continue;
		if (is_cli() && cli_cmds[i].cli_cmp == CLI_N)
			continue;
		if (!is_cli() && cli_cmds[i].cli_cmp == CLI_Y)
			continue;

		switch (cli_cmds[i].argc_cmp) {
		case EQUAL:
			if (n != cli_cmds[i].argc) {
				cli_printf("Invalid number of arguments\n");
				return -EINVAL;
			}

			break;
		case MORE:
			if (n < cli_cmds[i].argc) {
				cli_printf("too few arguments\n");
				return -EINVAL;
			}

			break;
		case LESS:
			if (n > cli_cmds[i].argc) {
				cli_printf("too many arguments\n");
				return -EINVAL;
			}

			break;
		}

		if (cli_cmds[i].fn) {
			r = cli_cmds[i].fn(args, n);
			return (r == -EAGAIN) ? -EINVAL : r;
		}

		break;
	}

	if (!strcmp(cmd, "help"))
		return cli_help();

	return -EAGAIN;
}

static void cli_handler_fn(char *input)
{
	_shl_cleanup_free_ char *original = input;
	_shl_cleanup_strv_ char **args = NULL;
	int r;

	if (!input) {
		rl_insert_text("quit");
		rl_redisplay();
		rl_crlf();
		sd_event_exit(cli_event, 0);
		return;
	}

	r = shl_qstr_tokenize(input, &args);
	if (r < 0)
		return cli_vENOMEM();
	else if (!r)
		return;

	add_history(original);
	r = cli_do(args, r);
	if (r != -EAGAIN)
		return;

	cli_printf("Command not found\n");
}

static int cli_stdin_fn(sd_event_source *source,
			int fd,
			uint32_t mask,
			void *data)
{
	if (mask & EPOLLIN) {
		rl_callback_read_char();
		return 0;
	}

	if (mask & (EPOLLHUP | EPOLLERR))
		sd_event_exit(cli_event, 0);

	return 0;
}

static int cli_signal_fn(sd_event_source *source,
			 const struct signalfd_siginfo *ssi,
			 void *data)
{
	if (ssi->ssi_signo == SIGCHLD) {
		cli_debug("caught SIGCHLD for %d", (int)ssi->ssi_pid);
	} else if (ssi->ssi_signo == SIGINT) {
		rl_replace_line("", 0);
		rl_crlf();
		rl_on_new_line();
		rl_redisplay();
	} else {
		cli_notice("caught signal %d, exiting..",
			   (int)ssi->ssi_signo);
		sd_event_exit(cli_event, 0);
	}

	return 0;
}

static void cli_destroy(void)
{
	unsigned int i;

	if (!cli_event)
		return;

	if (cli_rl) {
		cli_rl = false;

		rl_replace_line("", 0);
		rl_crlf();
		rl_on_new_line();
		rl_redisplay();

		rl_message("");
		rl_callback_handler_remove();
	}

	sd_event_source_unref(cli_stdin);
	cli_stdin = NULL;

	for (i = 0; cli_sigs[i]; ++i) {
		sd_event_source_unref(cli_sigs[i]);
		cli_sigs[i] = NULL;
	}

	sd_event_unref(cli_event);
	cli_event = NULL;
}

static int cli_init(void)
{
	static const int sigs[] = {
		SIGINT, SIGTERM, SIGQUIT, SIGHUP, SIGPIPE, SIGCHLD, 0
	};
	unsigned int i;
	sigset_t mask;
	int r;

	if (cli_event)
		return cli_EINVAL();

	r = sd_event_default(&cli_event);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	r = sd_bus_attach_event(bus, cli_event, 0);
	if (r < 0) {
		cli_vERR(r);
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
			cli_vERR(r);
			goto error;
		}
	}

	r = sd_event_add_io(cli_event,
			    fileno(stdin),
			    EPOLLHUP | EPOLLERR | EPOLLIN,
			    cli_stdin_fn,
			    NULL,
			    &cli_stdin);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	cli_rl = true;

	rl_erase_empty_line = 1;
	rl_callback_handler_install(NULL, cli_handler_fn);

	rl_set_prompt(CLI_PROMPT);
	printf("\r");
	rl_on_new_line();
	rl_redisplay();

	return 0;

error:
	cli_destroy();
	return r;
}

static int cli_run(void)
{
	if (!cli_event)
		return cli_EINVAL();

	return sd_event_loop(cli_event);
}

/*
 * cmd list
 */

static int cmd_list_link(sd_bus_message *m, const char *link)
{
	const char *obj, *name = "<unknown>";
	int r;

	r = sd_bus_message_enter_container(m, 'a', "{sa{sv}}");
	if (r < 0)
		return cli_log_parser(r);

	while ((r = sd_bus_message_enter_container(m,
						   'e',
						   "sa{sv}")) > 0) {
		r = sd_bus_message_read(m, "s", &obj);
		if (r < 0)
			return cli_log_parser(r);

		if (strcmp(obj, "org.freedesktop.miracle.Link")) {
			r = sd_bus_message_skip(m, "a{sv}");
			if (r < 0)
				return cli_log_parser(r);
			r = sd_bus_message_exit_container(m);
			if (r < 0)
				return cli_log_parser(r);
			continue;
		}

		r = sd_bus_message_enter_container(m, 'a', "{sv}");
		if (r < 0)
			return cli_log_parser(r);

		while ((r = sd_bus_message_enter_container(m,
							   'e',
							   "sv")) > 0) {
			r = sd_bus_message_read(m, "s", &obj);
			if (r < 0)
				return cli_log_parser(r);

			if (!strcmp(obj, "Name")) {
				r = bus_message_read_basic_variant(m, "s",
								   &name);
				if (r < 0)
					return cli_log_parser(r);
			} else {
				sd_bus_message_skip(m, "v");
			}

			r = sd_bus_message_exit_container(m);
			if (r < 0)
				return cli_log_parser(r);
		}
		if (r < 0)
			return cli_log_parser(r);

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return cli_log_parser(r);

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return cli_log_parser(r);
	}
	if (r < 0)
		return cli_log_parser(r);

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return cli_log_parser(r);

	cli_printf("%16s %-24s\n", link, name);

	return 0;
}

static int cmd_list_links(sd_bus_message *m)
{
	_shl_cleanup_free_ char *link = NULL;
	unsigned int link_cnt = 0;
	const char *obj;
	int r;

	cli_printf("%16s %-24s\n", "LINK-ID", "NAME");

	r = sd_bus_message_enter_container(m, 'a', "{oa{sa{sv}}}");
	if (r < 0)
		return cli_log_parser(r);

	while ((r = sd_bus_message_enter_container(m,
						   'e',
						   "oa{sa{sv}}")) > 0) {
		r = sd_bus_message_read(m, "o", &obj);
		if (r < 0)
			return cli_log_parser(r);

		obj = shl_startswith(obj, "/org/freedesktop/miracle/link/");
		if (!obj) {
			r = sd_bus_message_skip(m, "a{sa{sv}}");
			if (r < 0)
				return cli_log_parser(r);
			r = sd_bus_message_exit_container(m);
			if (r < 0)
				return cli_log_parser(r);
			continue;
		}

		free(link);
		link = sd_bus_label_unescape(obj);
		if (!link)
			return cli_ENOMEM();

		++link_cnt;
		r = cmd_list_link(m, link);
		if (r < 0)
			return r;

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return cli_log_parser(r);
	}
	if (r < 0)
		return cli_log_parser(r);

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return cli_log_parser(r);

	cli_printf("\n");

	return link_cnt;
}

static int cmd_list_peer(sd_bus_message *m,
			 const char *link_filter,
			 const char *peer)
{
	_shl_cleanup_free_ char *link = NULL;
	const char *obj, *name = "<unknown>";
	int r, connected = 0;

	r = sd_bus_message_enter_container(m, 'a', "{sa{sv}}");
	if (r < 0)
		return cli_log_parser(r);

	while ((r = sd_bus_message_enter_container(m,
						   'e',
						   "sa{sv}")) > 0) {
		r = sd_bus_message_read(m, "s", &obj);
		if (r < 0)
			return cli_log_parser(r);

		if (strcmp(obj, "org.freedesktop.miracle.Peer")) {
			r = sd_bus_message_skip(m, "a{sv}");
			if (r < 0)
				return cli_log_parser(r);
			r = sd_bus_message_exit_container(m);
			if (r < 0)
				return cli_log_parser(r);
			continue;
		}

		r = sd_bus_message_enter_container(m, 'a', "{sv}");
		if (r < 0)
			return cli_log_parser(r);

		while ((r = sd_bus_message_enter_container(m,
							   'e',
							   "sv")) > 0) {
			r = sd_bus_message_read(m, "s", &obj);
			if (r < 0)
				return cli_log_parser(r);

			if (!strcmp(obj, "Link")) {
				r = bus_message_read_basic_variant(m, "o",
								   &obj);
				if (r < 0)
					return cli_log_parser(r);

				obj = shl_startswith(obj,
					"/org/freedesktop/miracle/link/");
				if (obj) {
					free(link);
					link = sd_bus_label_unescape(obj);
					if (!link)
						return cli_ENOMEM();
				}
			} else if (!strcmp(obj, "Name")) {
				r = bus_message_read_basic_variant(m, "s",
								   &name);
				if (r < 0)
					return cli_log_parser(r);
			} else if (!strcmp(obj, "Connected")) {
				r = bus_message_read_basic_variant(m, "b",
								   &connected);
				if (r < 0)
					return cli_log_parser(r);
			} else {
				sd_bus_message_skip(m, "v");
			}

			r = sd_bus_message_exit_container(m);
			if (r < 0)
				return cli_log_parser(r);
		}
		if (r < 0)
			return cli_log_parser(r);

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return cli_log_parser(r);

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return cli_log_parser(r);
	}
	if (r < 0)
		return cli_log_parser(r);

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return cli_log_parser(r);

	if (!link_filter || !strcmp(link_filter, link))
		cli_printf("%16s %-9s %-24s %-10s\n",
			   link ? : "<none>",
			   peer,
			   name,
			   connected ? "yes" : "no");

	return 0;
}

static int cmd_list_peers(sd_bus_message *m, const char *link_filter)
{
	_shl_cleanup_free_ char *peer = NULL;
	unsigned int peer_cnt = 0;
	const char *obj;
	int r;

	cli_printf("%16s %-9s %-24s %-10s\n",
		   "LINK", "PEER-ID", "NAME", "CONNECTED");

	r = sd_bus_message_enter_container(m, 'a', "{oa{sa{sv}}}");
	if (r < 0)
		return cli_log_parser(r);

	while ((r = sd_bus_message_enter_container(m,
						   'e',
						   "oa{sa{sv}}")) > 0) {
		r = sd_bus_message_read(m, "o", &obj);
		if (r < 0)
			return cli_log_parser(r);

		obj = shl_startswith(obj, "/org/freedesktop/miracle/peer/");
		if (!obj) {
			r = sd_bus_message_skip(m, "a{sa{sv}}");
			if (r < 0)
				return cli_log_parser(r);
			r = sd_bus_message_exit_container(m);
			if (r < 0)
				return cli_log_parser(r);
			continue;
		}

		free(peer);
		peer = sd_bus_label_unescape(obj);
		if (!peer)
			return cli_ENOMEM();

		++peer_cnt;
		r = cmd_list_peer(m, link_filter, peer);
		if (r < 0)
			return r;

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return cli_log_parser(r);
	}
	if (r < 0)
		return cli_log_parser(r);

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return cli_log_parser(r);

	cli_printf("\n");

	return peer_cnt;
}

static int cmd_list(char **args, unsigned int n)
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
		cli_error("cannot retrieve objects: %s",
			  bus_error_message(&err, r));
		return r;
	}

	/* print links */

	r = cmd_list_links(m);
	if (r < 0)
		return r;
	link_cnt = r;

	sd_bus_message_rewind(m, true);

	/* print peers */

	r = cmd_list_peers(m, NULL);
	if (r < 0)
		return r;
	peer_cnt = r;

	/* print stats */

	cli_printf(" %u peers and %u links listed.\n", peer_cnt, link_cnt);

	return 0;
}

/*
 * cmd: select
 */

static int cmd_select(char **args, unsigned int n)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_shl_cleanup_free_ char *path = NULL, *name = NULL;
	int r;

	if (!n) {
		if (selected_link) {
			cli_printf("link %s deselected\n", selected_link);
			free(selected_link);
			selected_link = NULL;
		}

		return 0;
	}

	name = sd_bus_label_escape(args[0]);
	if (!name)
		return cli_ENOMEM();

	path = shl_strcat("/org/freedesktop/miracle/link/", name);
	if (!path)
		return cli_ENOMEM();

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       path,
			       "org.freedesktop.DBus.Properties",
			       "Get",
			       &err,
			       NULL,
			       "ss", "org.freedesktop.miracle.Link", "Type");
	if (r < 0) {
		cli_error("unknown link %s: %s",
			  args[0], bus_error_message(&err, r));
		return r;
	}

	free(selected_link);
	selected_link = strdup(args[0]);
	if (!selected_link)
		return cli_ENOMEM();

	cli_printf("link %s selected\n", selected_link);
	return 0;
}

/*
 * cmd: show-link
 */

static int cmd_show_link(char **args, unsigned int n)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_cleanup_sd_bus_message_ sd_bus_message *m = NULL;
	_shl_cleanup_free_ char *path = NULL, *name = NULL;
	_shl_cleanup_free_ char *type = NULL, *iface = NULL, *fname = NULL;
	const char *t, *arg_link;
	int r;

	if (n > 0)
		arg_link = args[0];
	else if (!(arg_link = selected_link))
		return log_error("no link selected"), -EINVAL;

	name = sd_bus_label_escape(arg_link);
	if (!name)
		return cli_ENOMEM();

	path = shl_strcat("/org/freedesktop/miracle/link/", name);
	if (!path)
		return cli_ENOMEM();

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       path,
			       "org.freedesktop.DBus.Properties",
			       "GetAll",
			       &err,
			       &m,
			       "s", "org.freedesktop.miracle.Link");
	if (r < 0) {
		cli_error("cannot retrieve link %s: %s",
			  arg_link, bus_error_message(&err, r));
		return r;
	}

	r = sd_bus_message_enter_container(m, 'a', "{sv}");
	if (r < 0)
		return cli_log_parser(r);

	while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
		r = sd_bus_message_read(m, "s", &t);
		if (r < 0)
			return cli_log_parser(r);

		if (!strcmp(t, "Type")) {
			r = bus_message_read_basic_variant(m, "s", &t);
			if (r < 0)
				return cli_log_parser(r);

			free(type);
			type = strdup(t);
			if (!type)
				return cli_ENOMEM();
		} else if (!strcmp(t, "Interface")) {
			r = bus_message_read_basic_variant(m, "s", &t);
			if (r < 0)
				return cli_log_parser(r);

			free(iface);
			iface = strdup(t);
			if (!iface)
				return cli_ENOMEM();
		} else if (!strcmp(t, "Name")) {
			r = bus_message_read_basic_variant(m, "s", &t);
			if (r < 0)
				return cli_log_parser(r);

			free(fname);
			fname = strdup(t);
			if (!fname)
				return cli_ENOMEM();
		} else {
			r = sd_bus_message_skip(m, "v");
			if (r < 0)
				return cli_log_parser(r);
		}

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return cli_log_parser(r);
	}
	if (r < 0)
		return cli_log_parser(r);

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return cli_log_parser(r);

	cli_printf("Link=%s\n", arg_link);
	if (type)
		cli_printf("Type=%s\n", type);
	if (iface)
		cli_printf("Interface=%s\n", iface);
	if (fname)
		cli_printf("Name=%s\n", fname);

	return 0;
}

/*
 * cmd: show-peer
 */

static int cmd_show_peer(char **args, unsigned int n)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_cleanup_sd_bus_message_ sd_bus_message *m = NULL;
	_shl_cleanup_free_ char *path = NULL, *name = NULL;
	_shl_cleanup_free_ char *link = NULL, *fname = NULL, *iface = NULL;
	_shl_cleanup_free_ char *laddr = NULL, *raddr = NULL;
	const char *t;
	int r, is_connected = false;

	name = sd_bus_label_escape(args[0]);
	if (!name)
		return cli_ENOMEM();

	path = shl_strcat("/org/freedesktop/miracle/peer/", name);
	if (!path)
		return cli_ENOMEM();

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       path,
			       "org.freedesktop.DBus.Properties",
			       "GetAll",
			       &err,
			       &m,
			       "s", "org.freedesktop.miracle.Peer");
	if (r < 0) {
		cli_error("cannot retrieve peer %s: %s",
			  args[0], bus_error_message(&err, r));
		return r;
	}

	r = sd_bus_message_enter_container(m, 'a', "{sv}");
	if (r < 0)
		return cli_log_parser(r);

	while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
		r = sd_bus_message_read(m, "s", &t);
		if (r < 0)
			return cli_log_parser(r);

		if (!strcmp(t, "Link")) {
			r = bus_message_read_basic_variant(m, "o", &t);
			if (r < 0)
				return cli_log_parser(r);

			t = shl_startswith(t,
					   "/org/freedesktop/miracle/link/");
			if (t) {
				free(link);
				link = sd_bus_label_unescape(t);
				if (!link)
					return cli_ENOMEM();
			}
		} else if (!strcmp(t, "Name")) {
			r = bus_message_read_basic_variant(m, "s", &t);
			if (r < 0)
				return cli_log_parser(r);

			free(fname);
			fname = strdup(t);
			if (!fname)
				return cli_ENOMEM();
		} else if (!strcmp(t, "Connected")) {
			r = bus_message_read_basic_variant(m, "b",
							   &is_connected);
			if (r < 0)
				return cli_log_parser(r);
		} else if (!strcmp(t, "Interface")) {
			r = bus_message_read_basic_variant(m, "s", &t);
			if (r < 0)
				return cli_log_parser(r);

			free(iface);
			iface = strdup(t);
			if (!iface)
				return cli_ENOMEM();
		} else if (!strcmp(t, "LocalAddress")) {
			r = bus_message_read_basic_variant(m, "s", &t);
			if (r < 0)
				return cli_log_parser(r);

			free(laddr);
			laddr = strdup(t);
			if (!laddr)
				return cli_ENOMEM();
		} else if (!strcmp(t, "RemoteAddress")) {
			r = bus_message_read_basic_variant(m, "s", &t);
			if (r < 0)
				return cli_log_parser(r);

			free(raddr);
			raddr = strdup(t);
			if (!raddr)
				return cli_ENOMEM();
		} else {
			r = sd_bus_message_skip(m, "v");
			if (r < 0)
				return cli_log_parser(r);
		}

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return cli_log_parser(r);
	}
	if (r < 0)
		return cli_log_parser(r);

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return cli_log_parser(r);

	cli_printf("Peer=%s\n", args[0]);
	if (link)
		cli_printf("Link=%s\n", link);
	if (fname)
		cli_printf("Name=%s\n", fname);
	cli_printf("Connected=%d\n", is_connected);
	if (iface)
		cli_printf("Interface=%s\n", iface);
	if (laddr)
		cli_printf("LocalAddress=%s\n", laddr);
	if (raddr)
		cli_printf("RemoteAddress=%s\n", raddr);

	return 0;
}

/*
 * cmd: add-link
 */

static int cmd_add_link(char **args, unsigned int n)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_cleanup_sd_bus_message_ sd_bus_message *m = NULL;
	_shl_cleanup_free_ char *link = NULL, *type = NULL;
	const char *name;
	char *t, *iface;
	int r;

	type = strdup(args[0]);
	if (!type)
		return cli_ENOMEM();

	t = strchr(type, ':');
	if (!t)
		return cli_EINVAL();

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
		cli_error("cannot add link %s:%s: %s",
			  type, iface, bus_error_message(&err, r));
		return r;
	}

	r = sd_bus_message_read(m, "s", &name);
	if (r < 0)
		return cli_log_parser(r);

	link = sd_bus_label_unescape(name);
	if (!link)
		return cli_ENOMEM();

	cli_printf("link %s added\n", link);
	return 0;
}

/*
 * cmd: remove-link
 */

static int cmd_remove_link(char **args, unsigned int n)
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
			       "s", args[0]);
	if (r < 0) {
		cli_error("cannot remove link %s: %s",
			  args[0], bus_error_message(&err, r));
		return r;
	}

	cli_printf("link %s removed\n", args[0]);
	return 0;
}

/*
 * cmd: set-link-name
 */

static int cmd_set_link_name(char **args, unsigned int n)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_cleanup_sd_bus_message_ sd_bus_message *m = NULL;
	_shl_cleanup_free_ char *path = NULL, *name = NULL;
	const char *arg_link, *arg_name;
	int r;

	arg_link = args[0];
	arg_name = args[1];
	if (n < 2) {
		if (!(arg_link = selected_link))
			return log_error("no link selected"), -EINVAL;

		arg_name = args[0];
	}

	name = sd_bus_label_escape(arg_link);
	if (!name)
		return cli_ENOMEM();

	path = shl_strcat("/org/freedesktop/miracle/link/", name);
	if (!path)
		return cli_ENOMEM();

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

	r = sd_bus_message_append(m, "s", arg_name);
	if (r < 0)
		return log_bus_create(r);

	r = sd_bus_message_close_container(m);
	if (r < 0)
		return log_bus_create(r);

	r = sd_bus_call(bus, m, 0, &err, NULL);
	if (r < 0) {
		cli_error("cannot set friendly-name to %s on link %s: %s",
			  arg_name, arg_link, bus_error_message(&err, r));
		return r;
	}

	cli_printf("Friendly-name set to %s on link %s\n",
		   arg_name, arg_link);
	return 0;
}

/*
 * cmd: start-scan
 */

static int cmd_start_scan(char **args, unsigned int n)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_shl_cleanup_free_ char *path = NULL, *name = NULL;
	const char *arg_link;
	int r;

	if (n > 0)
		arg_link = args[0];
	else if (!(arg_link = selected_link))
		return log_error("no link selected"), -EINVAL;

	name = sd_bus_label_escape(arg_link);
	if (!name)
		return cli_ENOMEM();

	path = shl_strcat("/org/freedesktop/miracle/link/", name);
	if (!path)
		return cli_ENOMEM();

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       path,
			       "org.freedesktop.miracle.Link",
			       "StartScan",
			       &err,
			       NULL,
			       NULL);
	if (r < 0) {
		cli_error("cannot start scan on link %s: %s",
			  arg_link, bus_error_message(&err, r));
		return r;
	}

	cli_printf("Scan started on link %s\n", arg_link);
	return 0;
}

/*
 * cmd: stop-scan
 */

static int cmd_stop_scan(char **args, unsigned int n)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_shl_cleanup_free_ char *path = NULL, *name = NULL;
	const char *arg_link;
	int r;

	if (n > 0)
		arg_link = args[0];
	else if (!(arg_link = selected_link))
		return log_error("no link selected"), -EINVAL;

	name = sd_bus_label_escape(arg_link);
	if (!name)
		return cli_ENOMEM();

	path = shl_strcat("/org/freedesktop/miracle/link/", name);
	if (!path)
		return cli_ENOMEM();

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       path,
			       "org.freedesktop.miracle.Link",
			       "StopScan",
			       &err,
			       NULL,
			       NULL);
	if (r < 0) {
		cli_error("cannot stop scan on link %s: %s",
			  arg_link, bus_error_message(&err, r));
		return r;
	}

	cli_printf("Scan stopped on link %s\n", arg_link);
	return 0;
}

/*
 * cmd: scan
 */

static char *scan_link;

static int cmd_scan_stop(bool async)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_shl_cleanup_free_ char *path = NULL, *name = NULL;
	int r;

	if (!scan_link)
		return 0;

	name = sd_bus_label_escape(scan_link);
	if (!name)
		return cli_ENOMEM();

	path = shl_strcat("/org/freedesktop/miracle/link/", name);
	if (!path)
		return cli_ENOMEM();

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       path,
			       "org.freedesktop.miracle.Link",
			       "StopScan",
			       &err,
			       NULL,
			       NULL);
	if (r >= 0)
		cli_printf("Scan stopped on link %s\n", scan_link);
	else if (async &&
		 sd_bus_error_has_name(&err, SD_BUS_ERROR_UNKNOWN_OBJECT))
		/* ignore */ ;
	else
		cli_error("cannot stop scan on link %s: %s",
			  scan_link, bus_error_message(&err, r));

	free(scan_link);
	scan_link = NULL;
	return 0;
}

static void cmd_scan_list(void)
{
	_cleanup_sd_bus_message_ sd_bus_message *m = NULL;
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
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
		cli_error("cannot retrieve objects: %s",
			  bus_error_message(&err, r));
		return;
	}

	cmd_list_peers(m, scan_link);
}

static int cmd_scan(char **args, unsigned int n)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_shl_cleanup_free_ char *path = NULL, *name = NULL;
	const char *arg_link;
	int r;

	if (n > 0 && !strcmp(args[0], "stop"))
		return cmd_scan_stop(false);

	if (scan_link) {
		log_error("another managed scan is already running on link %s",
			  scan_link);
		return -EINVAL;
	}

	if (n > 0)
		arg_link = args[0];
	else if (!(arg_link = selected_link))
		return log_error("no link selected"), -EINVAL;

	name = sd_bus_label_escape(arg_link);
	if (!name)
		return cli_ENOMEM();

	path = shl_strcat("/org/freedesktop/miracle/link/", name);
	if (!path)
		return cli_ENOMEM();

	scan_link = strdup(arg_link);
	if (!scan_link)
		return cli_ENOMEM();

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       path,
			       "org.freedesktop.miracle.Link",
			       "StartScan",
			       &err,
			       NULL,
			       NULL);
	if (r < 0) {
		cli_warning("cannot start scan on link %s (already running?): %s",
			    arg_link, bus_error_message(&err, r));
		return -EINVAL;
	}

	cli_printf("Scan started on link %s, listing peers..\n", arg_link);
	cmd_scan_list();

	return 0;
}

/*
 * cmd: quit/exit
 */

static int cmd_quit(char **args, unsigned int n)
{
	sd_event_exit(cli_event, 0);
	return 0;
}

/*
 * filters
 */

static int filters_show_peer(const char *peer)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_cleanup_sd_bus_message_ sd_bus_message *m = NULL;
	_shl_cleanup_free_ char *path = NULL, *name = NULL;
	_shl_cleanup_free_ char *link = NULL, *fname = NULL;
	const char *t;
	int r;

	name = sd_bus_label_escape(peer);
	if (!name)
		return cli_ENOMEM();

	path = shl_strcat("/org/freedesktop/miracle/peer/", name);
	if (!path)
		return cli_ENOMEM();

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       path,
			       "org.freedesktop.DBus.Properties",
			       "GetAll",
			       &err,
			       &m,
			       "s", "org.freedesktop.miracle.Peer");
	if (r < 0) {
		cli_error("cannot retrieve peer %s: %s",
			  peer, bus_error_message(&err, r));
		return r;
	}

	r = sd_bus_message_enter_container(m, 'a', "{sv}");
	if (r < 0)
		return cli_log_parser(r);

	while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
		r = sd_bus_message_read(m, "s", &t);
		if (r < 0)
			return cli_log_parser(r);

		if (!strcmp(t, "Link")) {
			r = bus_message_read_basic_variant(m, "o", &t);
			if (r < 0)
				return cli_log_parser(r);

			t = shl_startswith(t,
					   "/org/freedesktop/miracle/link/");
			if (t) {
				free(link);
				link = sd_bus_label_unescape(t);
				if (!link)
					return cli_ENOMEM();
			}
		} else if (!strcmp(t, "Name")) {
			r = bus_message_read_basic_variant(m, "s", &t);
			if (r < 0)
				return cli_log_parser(r);

			free(fname);
			fname = strdup(t);
			if (!fname)
				return cli_ENOMEM();
		} else {
			r = sd_bus_message_skip(m, "v");
			if (r < 0)
				return cli_log_parser(r);
		}

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return cli_log_parser(r);
	}
	if (r < 0)
		return cli_log_parser(r);

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return cli_log_parser(r);

	cli_printf("[" CLI_GREEN "ADD" CLI_DEFAULT  "] Peer %s@%s Name %s\n",
		   peer, link ? : "<none>", fname ? : "<unknown>");

	return 0;
}

static int filters_show_link(const char *link)
{
	_cleanup_sd_bus_error_ sd_bus_error err = SD_BUS_ERROR_NULL;
	_cleanup_sd_bus_message_ sd_bus_message *m = NULL;
	_shl_cleanup_free_ char *path = NULL, *name = NULL, *fname = NULL;
	const char *t;
	int r;

	name = sd_bus_label_escape(link);
	if (!name)
		return cli_ENOMEM();

	path = shl_strcat("/org/freedesktop/miracle/link/", name);
	if (!path)
		return cli_ENOMEM();

	r = sd_bus_call_method(bus,
			       "org.freedesktop.miracle",
			       path,
			       "org.freedesktop.DBus.Properties",
			       "GetAll",
			       &err,
			       &m,
			       "s", "org.freedesktop.miracle.Link");
	if (r < 0) {
		cli_error("cannot retrieve link %s: %s",
			  link, bus_error_message(&err, r));
		return r;
	}

	r = sd_bus_message_enter_container(m, 'a', "{sv}");
	if (r < 0)
		return cli_log_parser(r);

	while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
		r = sd_bus_message_read(m, "s", &t);
		if (r < 0)
			return cli_log_parser(r);

		if (!strcmp(t, "Name")) {
			r = bus_message_read_basic_variant(m, "s", &t);
			if (r < 0)
				return cli_log_parser(r);

			free(fname);
			fname = strdup(t);
			if (!fname)
				return cli_ENOMEM();
		} else {
			r = sd_bus_message_skip(m, "v");
			if (r < 0)
				return cli_log_parser(r);
		}

		r = sd_bus_message_exit_container(m);
		if (r < 0)
			return cli_log_parser(r);
	}
	if (r < 0)
		return cli_log_parser(r);

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return cli_log_parser(r);

	cli_printf("[" CLI_GREEN "ADD" CLI_DEFAULT  "] Link %s Name %s\n",
		   link, fname ? : "<unknown>");

	return 0;
}

static int filters_object_fn(sd_bus *bus,
			     sd_bus_message *m,
			     void *data,
			     sd_bus_error *err)
{
	_shl_cleanup_free_ char *peer = NULL, *link = NULL;
	const char *obj, *t;
	bool added;
	int r;

	added = !strcmp(sd_bus_message_get_member(m), "InterfacesAdded");

	r = sd_bus_message_read(m, "o", &obj);
	if (r < 0)
		return cli_log_parser(r);

	t = shl_startswith(obj, "/org/freedesktop/miracle/peer/");
	if (t) {
		peer = sd_bus_label_unescape(t);
		if (!peer)
			return cli_ENOMEM();

		if (added) {
			r = filters_show_peer(peer);
			if (r < 0)
				return r;
		} else {
			cli_printf("[" CLI_YELLOW "REMOVE" CLI_DEFAULT  "] Peer %s\n",
				   peer);
		}
	}

	t = shl_startswith(obj, "/org/freedesktop/miracle/link/");
	if (t) {
		link = sd_bus_label_unescape(t);
		if (!link)
			return cli_ENOMEM();

		if (added) {
			r = filters_show_link(link);
			if (r < 0)
				return r;
		} else {
			cli_printf("[" CLI_YELLOW "REMOVE" CLI_DEFAULT  "] Link %s\n",
				   link);
		}
	}

	return 0;
}

static void filters_init()
{
	int r;

	r = sd_bus_add_match(bus,
			     "type='signal',"
			     "sender='org.freedesktop.miracle',"
			     "interface='org.freedesktop.DBus.ObjectManager'",
			     filters_object_fn,
			     NULL);
	if (r < 0)
		cli_error("cannot add dbus match: %d", r);
}

static void filters_destroy()
{
	sd_bus_remove_match(bus,
			    "type='signal',"
			    "sender='org.freedesktop.miracle',"
			    "interface='org.freedesktop.DBus.ObjectManager'",
			    filters_object_fn,
			    NULL);
}

/*
 * main
 */

static const struct cli_cmd cli_cmds[] = {
	{ "list",		NULL,					CLI_M,	LESS,	0,	cmd_list,		"List links and peers" },
	{ "select",		"[link]",				CLI_Y,	LESS,	1,	cmd_select,		"Select default link" },
	{ "show-link",		"[link]",				CLI_M,	LESS,	1,	cmd_show_link,		"Show link information" },
	{ "show-peer",		"<peer>",				CLI_M,	EQUAL,	1,	cmd_show_peer,		"Show peer information" },
	{ "add-link",		"<link>",				CLI_M,	EQUAL,	1,	cmd_add_link,		"Add link" },
	{ "remove-link",	"<link>",				CLI_M,	EQUAL,	1,	cmd_remove_link,	"Remove link" },
	{ "scan",		"[link|stop]",				CLI_Y,	LESS,	1,	cmd_scan,		"Start/Stop managed scan" },
	{ "start-scan",		"[link]",				CLI_N,	LESS,	1,	cmd_start_scan,		"Start neighborhood scan" },
	{ "stop-scan",		"[link]",				CLI_M,	LESS,	1,	cmd_stop_scan,		"Stop neighborhood scan" },
	{ "set-link-name",	"[link] <name>",			CLI_M,	MORE,	1,	cmd_set_link_name,	"Set friendly name of link" },
	{ "quit",		NULL,					CLI_Y,	MORE,	0,	cmd_quit,		"Quit program" },
	{ "exit",		NULL,					CLI_Y,	MORE,	0,	cmd_quit,		NULL },
	{ "help",		NULL,					CLI_M,	MORE,	0,	NULL,			"Print help" },
	{ },
};

static int miraclectl_run(void)
{
	int r;

	filters_init();

	r = cli_run();

	cmd_scan_stop(true);
	filters_destroy();

	return r;
}

static int miraclectl_main(int argc, char *argv[])
{
	int r, left;

	left = argc - optind;
	if (left <= 0) {
		r = cli_init();
		if (r < 0)
			return r;

		r = miraclectl_run();
		cli_destroy();
	} else {
		r = cli_do(argv + optind, left);
		if (r == -EAGAIN)
			cli_error("unknown operation %s",
				  argv[optind]);
	}

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
			return cli_help();
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

	r = miraclectl_main(argc, argv);
	sd_bus_unref(bus);

	return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
