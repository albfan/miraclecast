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
#include <sys/wait.h>
#include <systemd/sd-bus.h>
#include "ctl.h"
#include "shl_macro.h"
#include "shl_util.h"

/* *sigh* readline doesn't include all their deps, so put them last */
#include <readline/history.h>
#include <readline/readline.h>

/*
 * Helpers for interactive commands
 */

sd_event *cli_event;
sd_bus *cli_bus;
static sd_event_source *cli_sigs[_NSIG];
static sd_event_source *cli_stdin;
static bool cli_rl;
static const struct cli_cmd *cli_cmds;
unsigned int cli_max_sev = LOG_NOTICE;

static bool is_cli(void)
{
	return cli_rl;
}

void cli_printv(const char *fmt, va_list args)
{
	SHL_PROTECT_ERRNO;
	_shl_free_ char *line = NULL;
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

void cli_printf(const char *fmt, ...)
{
	SHL_PROTECT_ERRNO;
	va_list args;

	va_start(args, fmt);
	cli_printv(fmt, args);
	va_end(args);
}

int cli_help(const struct cli_cmd *cmds, int whitespace)
{
	unsigned int i;

	cli_printf("Available commands:\n");

	for (i = 0; cmds[i].cmd; ++i) {
		if (!cmds[i].desc)
			continue;
		if (is_cli() && cmds[i].cli_cmp == CLI_N)
			continue;
		if (!is_cli() && cmds[i].cli_cmp == CLI_Y)
			continue;

		cli_printf("  %s %-*s %s\n",
			   cmds[i].cmd,
			   (int)(whitespace - strlen(cmds[i].cmd)),
			   cmds[i].args ? : "",
			   cmds[i].desc ? : "");
	}

	return 0;
}

int cli_do(const struct cli_cmd *cmds, char **args, unsigned int n)
{
	unsigned int i;
	const char *cmd;
	int r;

	if (!n)
		return -EAGAIN;

	cmd = *args++;
	--n;

	for (i = 0; cmds[i].cmd; ++i) {
		if (strcmp(cmd, cmds[i].cmd))
			continue;
		if (is_cli() && cmds[i].cli_cmp == CLI_N)
			continue;
		if (!is_cli() && cmds[i].cli_cmp == CLI_Y)
			continue;

		switch (cmds[i].argc_cmp) {
		case CLI_EQUAL:
			if (n != cmds[i].argc) {
				cli_printf("Invalid number of arguments\n");
				return -EINVAL;
			}

			break;
		case CLI_MORE:
			if (n < cmds[i].argc) {
				cli_printf("too few arguments\n");
				return -EINVAL;
			}

			break;
		case CLI_LESS:
			if (n > cmds[i].argc) {
				cli_printf("too many arguments\n");
				return -EINVAL;
			}

			break;
		}

		if (cmds[i].fn) {
			r = cmds[i].fn(args, n);
			return (r == -EAGAIN) ? -EINVAL : r;
		}

		break;
	}

	if (!strcmp(cmd, "help"))
		return cli_help(cmds, 40);

	return -EAGAIN;
}

static void cli_handler_fn(char *input)
{
	_shl_free_ char *original = input;
	_shl_strv_free_ char **args = NULL;
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
	r = cli_do(cli_cmds, args, r);
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
		waitid(P_PID, ssi->ssi_pid, NULL, WNOHANG|WEXITED);
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

void cli_destroy(void)
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

	cli_cmds = NULL;
	sd_bus_detach_event(cli_bus);
	cli_bus = NULL;
	sd_event_unref(cli_event);
	cli_event = NULL;
}

int cli_init(sd_bus *bus, const struct cli_cmd *cmds)
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

	cli_cmds = cmds;
	cli_bus = bus;

	r = sd_bus_attach_event(cli_bus, cli_event, 0);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	for (i = 0; sigs[i]; ++i) {
		sigemptyset(&mask);
		sigaddset(&mask, sigs[i]);
		sigprocmask(SIG_BLOCK, &mask, NULL);

		r = sd_event_add_signal(cli_event,
					&cli_sigs[i],
					sigs[i],
					cli_signal_fn,
					NULL);
		if (r < 0) {
			cli_vERR(r);
			goto error;
		}
	}

	if (isatty(fileno(stdin))) {
		r = sd_event_add_io(cli_event,
			    &cli_stdin,
			    fileno(stdin),
			    EPOLLHUP | EPOLLERR | EPOLLIN,
			    cli_stdin_fn,
			    NULL);
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
	}

	return 0;

error:
	cli_destroy();
	return r;
}

int cli_run(void)
{
	if (!cli_event)
		return cli_EINVAL();

	return sd_event_loop(cli_event);
}

void cli_exit(void)
{
	if (!cli_event)
		return cli_vEINVAL();

	sd_event_exit(cli_event, 0);
}

bool cli_running(void)
{
	return is_cli();
}
