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

#include <systemd/sd-bus.h>

#ifndef CTL_WFD_DBUS_H
#define CTL_WFD_DBUS_H

#define dispd_fn_sink_properties_changed(s, namev...)	({				\
				char *names[] = { namev, NULL };					\
				_dispd_fn_sink_properties_changed((s), names);		\
})

#define dispd_fn_session_properties_changed(s, namev...)	({			\
				char *names[] = { namev, NULL };					\
				_dispd_fn_session_properties_changed((s), names);		\
})

struct dispd_dbus;
struct dispd_session;
struct dispd_sink;

struct dispd_dbus * dispd_dbus_get();
int dispd_dbus_new(struct dispd_dbus **out, sd_event *loop, sd_bus *bus);
void dispd_dbus_free(struct dispd_dbus *dispd_dbus);
int dispd_dbus_expose(struct dispd_dbus *dispd_dbus);
int _dispd_fn_sink_properties_changed(struct dispd_sink *s, char **names);
int _dispd_fn_session_properties_changed(struct dispd_session *s, char **names);

#endif

