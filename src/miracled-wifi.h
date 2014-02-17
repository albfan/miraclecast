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
#include <stdbool.h>
#include <stdlib.h>
#include <systemd/sd-event.h>
#include "miracle.h"

#ifndef MIRACLED_WIFI_H
#define MIRACLED_WIFI_H

struct wifi;
struct wifi_dev;

/* wifi */

struct wifi;
struct wifi_dev;

enum wifi_event_type {
	WIFI_HUP,
	WIFI_SCAN_STOPPED,
	WIFI_DEV_FOUND,
	WIFI_DEV_LOST,
	WIFI_DEV_PROVISION,
	WIFI_DEV_CONNECT,
	WIFI_DEV_DISCONNECT,
};

enum wifi_provision_type {
	WIFI_PROVISION_PBC,
	WIFI_PROVISION_DISPLAY,
	WIFI_PROVISION_PIN,
	WIFI_PROVISION_CNT,
};

/* WPS pins are fixed to 8 chars (+1 terminating zero) */
#define WIFI_PIN_STRLEN (8 + 1)

struct wifi_event {
	unsigned int type;

	union {
		struct wifi_event_dev_found {
			struct wifi_dev *dev;
		} dev_found;

		struct wifi_event_dev_lost {
			struct wifi_dev *dev;
		} dev_lost;

		struct wifi_event_dev_provision {
			struct wifi_dev *dev;
			unsigned int type;
			char pin[WIFI_PIN_STRLEN];
		} dev_provision;

		struct wifi_event_dev_connect {
			struct wifi_dev *dev;
		} dev_connect;

		struct wifi_event_dev_disconnect {
			struct wifi_dev *dev;
		} dev_disconnect;
	};
};

typedef void (*wifi_event_t) (struct wifi *w, void *data,
			      struct wifi_event *ev);

int wifi_new(sd_event *event, wifi_event_t event_fn, void *data,
	     struct wifi **out);
void wifi_free(struct wifi *w);
void wifi_set_data(struct wifi *w, void *data);
void *wifi_get_data(struct wifi *w);

pid_t wifi_get_supplicant_pid(struct wifi *w);
int wifi_spawn_supplicant(struct wifi *w,
			  const char *rundir,
			  const char *binary,
			  const char *ifname);

bool wifi_is_open(struct wifi *w);
int wifi_open(struct wifi *w, const char *wpa_path);
void wifi_close(struct wifi *w);

int wifi_set_discoverable(struct wifi *w, bool on);
int wifi_set_name(struct wifi *w, const char *name);

struct wifi_dev *wifi_get_devs(struct wifi *w);
struct wifi_dev *wifi_dev_next(struct wifi_dev *d);

/* wifi device */

void wifi_dev_ref(struct wifi_dev *d);
void wifi_dev_unref(struct wifi_dev *d);
void wifi_dev_set_data(struct wifi_dev *d, void *data);
void *wifi_dev_get_data(struct wifi_dev *d);

bool wifi_dev_is_available(struct wifi_dev *d);
bool wifi_dev_is_running(struct wifi_dev *d);
bool wifi_dev_is_ready(struct wifi_dev *d);

void wifi_dev_allow(struct wifi_dev *d, const char *pin);
void wifi_dev_reject(struct wifi_dev *d);
int wifi_dev_connect(struct wifi_dev *d, unsigned int provision,
		     const char *pin);
void wifi_dev_disconnect(struct wifi_dev *d);

const char *wifi_dev_get_name(struct wifi_dev *d);
const char *wifi_dev_get_interface(struct wifi_dev *d);
const char *wifi_dev_get_local_address(struct wifi_dev *d);
const char *wifi_dev_get_remote_address(struct wifi_dev *d);

#endif /* MIRACLED_WIFI_H */
