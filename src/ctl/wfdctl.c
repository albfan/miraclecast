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
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/signalfd.h>
#include <sys/time.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include <systemd/sd-journal.h>
#include <time.h>
#include <unistd.h>
#include "ctl.h"
#include "wfd.h"
#include "shl_macro.h"
#include "shl_util.h"
#include "config.h"

struct ctl_wfd
{
	sd_event *loop;
	sd_bus *bus;
	struct ctl_wifi *wifi;
	struct ctl_src *src;
};

static struct ctl_wfd *ctl_wfd;

void ctl_wfd_free(struct ctl_wfd *wfd);

int ctl_wfd_new(struct ctl_wfd **out)
{
	int r;
	struct ctl_wfd *wfd = calloc(1, sizeof(struct ctl_wfd));
	if(!wfd) {
		r = -ENOMEM;
		goto end;
	}

	r = sd_event_default(&wfd->loop);
	if(0 > r) {
		goto free_wfd;
	}

	r = sd_bus_default_system(&wfd->bus);
	if (r < 0) {
		goto free_wfd;
	}

	*out = wfd;

	r = 0;

	goto end;

free_wfd:
	ctl_wfd_free(wfd);
end:
	return r;
}

void ctl_wfd_free(struct ctl_wfd *wfd)
{
	if(wfd->bus) {
		sd_bus_unref(wfd->bus);
	}

	free(wfd);
}

int ctl_wfd_run(struct ctl_wfd *wfd)
{
	return sd_event_loop(wfd->loop);
}

/* Callbacks from ctl-src */
void ctl_fn_src_connected(struct ctl_src *s)
{
}

void ctl_fn_src_disconnected(struct ctl_src *s)
{
}

void ctl_fn_src_setup(struct ctl_src *s)
{
}

void ctl_fn_src_playing(struct ctl_src *s)
{
}

void ctl_fn_peer_new(struct ctl_peer *p)
{
}

void ctl_fn_peer_free(struct ctl_peer *p)
{
}

void ctl_fn_peer_provision_discovery(struct ctl_peer *p,
					 const char *prov,
					 const char *pin)
{
}

void ctl_fn_peer_go_neg_request(struct ctl_peer *p,
					 const char *prov,
					 const char *pin)
{
}

void ctl_fn_peer_formation_failure(struct ctl_peer *p, const char *reason)
{
}

void ctl_fn_peer_connected(struct ctl_peer *p)
{
}

void ctl_fn_peer_disconnected(struct ctl_peer *p)
{
}

void ctl_fn_link_new(struct ctl_link *l)
{
}

void ctl_fn_link_free(struct ctl_link *l)
{
}

void cli_fn_help()
{
}

static int ctl_wfd_enum(sd_bus *bus,
				const char *path,
				void *userdata,
				char ***out,
				sd_bus_error *out_error)
{
	int r = 0, i = 0;
	char **nodes;

	if(strcmp("/org/freedesktop/miracle/wfd", path)) {
		return 0;
	}
   
	nodes = malloc(sizeof(char *) * 3);
	if(!nodes) {
		r = -ENOMEM;
		goto end;
	}

	nodes[i] = strdup("/org/freedesktop/miracle/wfd/sink");
	if(!nodes[i ++]) {
		r = -ENOMEM;
		goto end;
	}

	nodes[i] = strdup("/org/freedesktop/miracle/wfd/session");
	if(!nodes[i ++]) {
		r = -ENOMEM;
		goto end;
	}

	nodes[i ++] = NULL;
	*out = nodes;

	return 0;

free_nodes:
	while(i --) {
		free(nodes[i]);
	}
	free(nodes);
end:
	return r;
}

static int ctl_wfd_find_sink(sd_bus *bus,
				const char *path,
				const char *interface,
				void *userdata,
				void **ret_found,
				sd_bus_error *ret_error)
{
	return 0;
}

static int ctl_wfd_find_session(sd_bus *bus,
				const char *path,
				const char *interface,
				void *userdata,
				void **ret_found,
				sd_bus_error *ret_error)
{
	return 0;
}

static int ctl_wfd_sink_start_session(sd_bus_message *m,
				void *userdata,
				sd_bus_error *ret_error)
{
	return 0;
}

static int ctl_wfd_sink_get_audio_formats(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *ret_error)
{
	return 0;
}

static int ctl_wfd_sink_get_video_formats(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *ret_error)
{
	return 0;
}

static int ctl_wfd_sink_has_video(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *ret_error)
{
	return 0;
}

static int ctl_wfd_sink_get_peer(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *ret_error)
{
	return 0;
}

static int ctl_wfd_sink_has_audio(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *ret_error)
{
	return 0;
}

static int ctl_wfd_session_end(sd_bus_message *m,
				void *userdata,
				sd_bus_error *ret_error)
{
	return 0;
}

static int ctl_wfd_session_get_sink(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *ret_error)
{
	return 0;
}

static int ctl_wfd_session_get_state(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *ret_error)
{
	return 0;
}

static const sd_bus_vtable ctl_wfd_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_VTABLE_END,
};

static const sd_bus_vtable ctl_wfd_sink_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("StartSession", NULL, "o", ctl_wfd_sink_start_session, 0),
	SD_BUS_PROPERTY("AudioFormats", "a{sv}", ctl_wfd_sink_get_audio_formats, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("VideoFormats", "a{sv}", ctl_wfd_sink_get_video_formats, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("HasAudio", "b", ctl_wfd_sink_has_audio, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("HasVideo", "b", ctl_wfd_sink_has_video, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Peer", "o", ctl_wfd_sink_get_peer, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_VTABLE_END,
};

static const sd_bus_vtable ctl_wfd_session_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_METHOD("End", NULL, NULL, ctl_wfd_session_end, 0),
	SD_BUS_PROPERTY("Sink", "o", ctl_wfd_session_get_sink, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("State", "i", ctl_wfd_session_get_state, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_VTABLE_END,
};

int ctl_wfd_expose(struct ctl_wfd *wfd)
{
	int r;

	r = sd_bus_add_object_vtable(wfd->bus,
					NULL,
					"/org/freedesktop/miracle/wfd",
					"org.freedesktop.miracle.wfd.Display",
					ctl_wfd_vtable,
					NULL);
	if(0 > r) {
		goto end;
	}

	r = sd_bus_add_fallback_vtable(wfd->bus,
					NULL,
					"/org/freedesktop/miracle/wfd/sink",
					"org.freedesktop.miracle.wfd.Sink",
					ctl_wfd_sink_vtable,
					ctl_wfd_find_sink,
					NULL);
	if(0 > r) {
		goto end;
	}

	r = sd_bus_add_fallback_vtable(wfd->bus,
					NULL,
					"/org/freedesktop/miracle/wfd/session",
					"org.freedesktop.miracle.wfd.Session",
					ctl_wfd_session_vtable,
					ctl_wfd_find_session,
					NULL);
	if(0 > r) {
		goto end;
	}

	r = sd_bus_add_node_enumerator(wfd->bus,
					NULL,
					"/org/freedesktop/miracle/wfd",
					ctl_wfd_enum,
					NULL);
	if(0 > r) {
		goto end;
	}

	r = sd_bus_add_object_manager(wfd->bus, NULL, "/org/freedesktop/miracle/wfd");
	if(0 > r) {
		goto end;
	}

	r = sd_bus_attach_event(wfd->bus, wfd->loop, SD_EVENT_PRIORITY_NORMAL);
	if(0 > r) {
		goto end;
	}

	r = sd_bus_request_name(wfd->bus, "org.freedesktop.miracle.wfd", 0);

end:
	return r;
}

int main(int argc, char **argv)
{
	int r;

	setlocale(LC_ALL, "");

	r = ctl_wfd_new(&ctl_wfd);
	if(0 > r) {
		goto end;
	}

	r = ctl_wfd_expose(ctl_wfd);
	if(0 > r) {
		goto free_wfd;
	}

	r = ctl_wfd_run(ctl_wfd);

free_wfd:
	ctl_wfd_free(ctl_wfd);
end:
	return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

/* vim: set tabstop=4 softtabstop=4 shiftwidth=4 noexpandtab : */
