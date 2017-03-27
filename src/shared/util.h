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

#ifndef MIRACLE_UTIL_H
#define MIRACLE_UTIL_H

#include <alloca.h>
#include <errno.h>
#include <stdio.h>
#include <libudev.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/time.h>
#include <systemd/sd-bus.h>
#include <time.h>
#include "shl_macro.h"
#include <glib.h>

static inline GKeyFile* load_ini_file() {
   GKeyFile* gkf = NULL;
   gchar* config_file;

   gkf = g_key_file_new();
   
   config_file = g_build_filename(g_get_home_dir(), ".config", "miraclecastrc", NULL);
   if (!g_key_file_load_from_file(gkf, config_file, G_KEY_FILE_NONE, NULL)) {
      g_free(config_file);
      config_file = g_build_filename(g_get_home_dir(), ".miraclecast", NULL);
      if (!g_key_file_load_from_file(gkf, config_file, G_KEY_FILE_NONE, NULL)) {
         g_key_file_free(gkf);
         gkf = NULL;
      }
   }
   g_free(config_file);
   return gkf;
}

static inline void cleanup_sd_bus_message(sd_bus_message **ptr)
{
	sd_bus_message_unref(*ptr);
}

static inline void cleanup_udev_device(struct udev_device **ptr)
{
	udev_device_unref(*ptr);
}

static inline void cleanup_udev_enumerate(struct udev_enumerate **ptr)
{
	udev_enumerate_unref(*ptr);
}

#define _cleanup_sd_bus_error_ _shl_cleanup_(sd_bus_error_free)
#define _sd_bus_error_free_ _shl_cleanup_(sd_bus_error_free)
#define _cleanup_sd_bus_message_ _shl_cleanup_(cleanup_sd_bus_message)
#define _sd_bus_message_unref_ _shl_cleanup_(cleanup_sd_bus_message)
#define _cleanup_udev_device_ _shl_cleanup_(cleanup_udev_device)
#define _cleanup_udev_enumerate_ _shl_cleanup_(cleanup_udev_enumerate)

#define strv_from_stdarg_alloca(first) ({ \
		char **_l; \
		if (!first) { \
			_l = (char**)&first; \
		} else { \
			unsigned _n; \
			va_list _ap; \
			_n = 1; \
			va_start(_ap, first); \
			while (va_arg(_ap, char*)) \
				_n++; \
			va_end(_ap); \
			_l = alloca(sizeof(char*) * (_n + 1)); \
			_l[_n = 0] = (char*)first; \
			va_start(_ap, first); \
			for (;;) { \
				_l[++_n] = va_arg(_ap, char*); \
				if (!_l[_n]) \
					break; \
			} \
			va_end(_ap); \
		} \
		_l; \
	})

static inline unsigned int ifindex_from_udev_device(struct udev_device *d)
{
	const char *val;

	val = udev_device_get_property_value(d, "IFINDEX");
	if (!val)
		return 0;

	return (unsigned int)atoi(val);
}

static inline int64_t now(clockid_t clock_id)
{
	struct timespec ts;

	clock_gettime(clock_id, &ts);

	return (int64_t)ts.tv_sec * 1000000LL +
	       (int64_t)ts.tv_nsec / 1000LL;
}

#define MAC_STRLEN 18

static inline void reformat_mac(char *dst, const char *src)
{
	unsigned char x1 = 0, x2 = 0, x3 = 0, x4 = 0, x5 = 0, x6 = 0;

	sscanf(src, "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
	       &x1, &x2, &x3, &x4, &x5, &x6);
	sprintf(dst, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
		x1, x2, x3, x4, x5, x6);
}

static inline const char *bus_error_message(const sd_bus_error *e, int error)
{
	if (e) {
		if (sd_bus_error_has_name(e, SD_BUS_ERROR_ACCESS_DENIED))
			return "Access denied";
		if (e->message)
			return e->message;
	}

	return strerror(error < 0 ? -error : error);
}

static inline int bus_message_read_basic_variant(sd_bus_message *m,
						 const char *sig,
						 void *ptr)
{
	int r;

	if (!sig || !*sig || sig[1] || !ptr)
		return -EINVAL;

	r = sd_bus_message_enter_container(m, 'v', sig);
	if (r < 0)
		return r;

	r = sd_bus_message_read(m, sig, ptr);
	if (r < 0)
		return r;

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return r;

	return 0;
}

#endif /* MIRACLE_UTIL_H */
