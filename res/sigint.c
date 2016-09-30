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
#include <sys/signalfd.h>
#include "sigint.h"

typedef struct _SigintDelegate SigintDelegate;

struct _SigintDelegate
{
	SigintHandler handler;
	gpointer user_data;
};

static gboolean sigint_on_signal(GIOChannel *c, GIOCondition e, gpointer d)
{
	struct signalfd_siginfo siginfo;
	g_io_channel_read_chars(c, (gchar *) &siginfo, sizeof(siginfo), NULL, NULL);

	SigintDelegate *delegate = d;
	(*delegate->handler)(delegate->user_data);

	return FALSE;
}

void sigint_add_watch(SigintHandler handler, gpointer user_data)
{
	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigprocmask(SIG_BLOCK, &mask, NULL);

	SigintDelegate *d = g_malloc(sizeof(SigintDelegate));
	*d = (SigintDelegate) {
		handler = handler,
		user_data = user_data
	};

	int fd = signalfd(-1, &mask, SFD_CLOEXEC);
	GIOChannel *c = g_io_channel_unix_new(fd);
	g_io_channel_set_encoding(c, NULL, NULL);
	g_io_add_watch_full(c,
					G_PRIORITY_DEFAULT,
					G_IO_IN | G_IO_ERR | G_IO_HUP,
					sigint_on_signal,
					d,
					g_free);
	g_io_channel_unref(c);
}
