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
#define LOG_SUBSYSTEM "dispd-encoder"

#include <systemd/sd-event.h>
#include <systemd/sd-bus.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include "dispd-encoder.h"
#include "shl_macro.h"
#include "shl_log.h"
#include "wfd-session.h"
#include "disp.h"
#include "util.h"

struct dispd_encoder
{
	int ref;

	sd_event *loop;
	sd_event_source *child_source;
	sd_event_source *pipe_source;

	sd_bus *bus;

	char *name;

	enum dispd_encoder_state state;
	dispd_encoder_state_change_handler handler;
	void *userdata;
};

static int dispd_encoder_new(struct dispd_encoder **out);
static int dispd_encoder_on_unique_name(sd_event_source *source,
				int fd,
				uint32_t events,
				void *userdata);
static void dispd_encoder_set_state(struct dispd_encoder *e,
				enum dispd_encoder_state state);

static int dispd_encoder_exec(const char *cmd, int fd, struct wfd_session *s)
{
	int r;
	sigset_t mask;
	char disp[16], auth[256];

	log_info("child forked with pid %d", getpid());

	/* restore to default signal handler */
	sigemptyset(&mask);
	sigprocmask(SIG_SETMASK, &mask, NULL);

	snprintf(disp, sizeof(disp), "DISPLAY=%s", wfd_session_get_disp_name(s));
	snprintf(auth, sizeof(auth), "XAUTHORITY=%s", wfd_session_get_disp_auth(s));

	/* after encoder connected to DBus, write unique name to fd 3,
	 * so we can controll it through DBus
	 */
	r = dup2(fd, 3);
	if(0 > r) {
		return r;
	}

	if(fd != 3) {
		close(fd);
	}

	// TODO run encoder as normal user instead of root
	r = execvpe(cmd,
					(char *[]){ (char *) cmd, NULL },
					(char *[]){ disp,
						auth,
						"GST_DEBUG=3",
						"G_MESSAGES_DEBUG=all",
						NULL
					});
	_exit(1);
	return 0;
}

static void dispd_encoder_close_pipe(struct dispd_encoder *e)
{
	if(!e->pipe_source) {
		return;
	}

	close(sd_event_source_get_io_fd(e->pipe_source));
	sd_event_source_set_enabled(e->pipe_source, false);
	sd_event_source_unref(e->pipe_source);
	e->pipe_source = NULL;
}

static void dispd_encoder_kill_child(struct dispd_encoder *e)
{
	pid_t pid;

	if(!e->child_source) {
		return;
	}

	sd_event_source_get_child_pid(e->child_source, &pid);
	kill(pid, SIGKILL);
	sd_event_source_set_enabled(e->child_source, false);
	sd_event_source_unref(e->child_source);
}

static void dispd_encoder_notify_state_change(struct dispd_encoder *e,
				enum dispd_encoder_state state)
{
	assert(e);

	if(!e->handler) {
		return;
	}

	dispd_encoder_ref(e);
	(*e->handler)(e, state, e->userdata);
	dispd_encoder_unref(e);
}

static int dispd_encoder_on_terminated(sd_event_source *source,
				const siginfo_t *si,
				void *userdata)
{
	struct dispd_encoder *e = userdata;

	log_info("encoder %d terminated", si->si_pid);

	if(e) {
		dispd_encoder_set_state(e, DISPD_ENCODER_STATE_TERMINATED);
	}

	return 0;
}

int dispd_encoder_spawn(struct dispd_encoder **out, struct wfd_session *s)
{
	pid_t pid;
	_dispd_encoder_unref_ struct dispd_encoder *e = NULL;
	int fds[2] = { -1, -1 };
	int r;

	assert(out);
	assert(s);

	r = dispd_encoder_new(&e);
	if(0 > r) {
		goto end;
	}

	r = pipe(fds);
	if(0 > r) {
		goto end;
	}

	pid = fork();
	if(0 > pid) {
		r = pid;
		goto kill_encoder;
	}
	else if(!pid) {
		close(fds[0]);

		r = dispd_encoder_exec("gstencoder", fds[1], s);
		if(0 > r) {
			log_warning("failed to exec encoder: %s", strerror(errno));
		}
		_exit(1);
	}

	r = sd_event_add_child(ctl_wfd_get_loop(),
					&e->child_source,
					pid,
					WEXITED,
					dispd_encoder_on_terminated,
					e);
	if(0 > r) {
		goto close_pipe;
	}

	r = sd_event_add_io(ctl_wfd_get_loop(),
					&e->pipe_source,
					fds[0],
					EPOLLIN,
					dispd_encoder_on_unique_name,
					e);
	if(0 > r) {
		goto close_pipe;
	}

	close(fds[1]);
	*out = dispd_encoder_ref(e);

	goto end;

close_pipe:
	close(fds[0]);
	close(fds[1]);
kill_encoder:
	// dispd will do the cleanup
	kill(pid, SIGKILL);
end:
	return r;
}

static int dispd_encoder_new(struct dispd_encoder **out)
{
	_shl_free_ struct dispd_encoder *e = NULL;

	assert(out);
   
	e = calloc(1, sizeof(struct dispd_encoder));
	if(!e) {
		return -ENOMEM;
	}

	e->ref = 1;
	*out = e;
	e = NULL;

	return 0;
}

struct dispd_encoder * dispd_encoder_ref(struct dispd_encoder *e)
{
	assert(e);
	assert(0 < e->ref);

	++ e->ref;

	return e;
}

void dispd_encoder_unrefp(struct dispd_encoder **e)
{
	if(*e) {
		dispd_encoder_unref(*e);
	}
}

void dispd_encoder_unref(struct dispd_encoder *e)
{
	assert(e);
	assert(0 < e->ref);

	--e->ref;
	if(e->ref) {
		return;
	}

	if(e->bus) {
		sd_bus_unref(e->bus);
	}

	if(e->name) {
		free(e->name);
	}

	dispd_encoder_close_pipe(e);
	dispd_encoder_kill_child(e);

	free(e);
}

void dispd_encoder_set_handler(struct dispd_encoder *e,
				dispd_encoder_state_change_handler handler,
				void *userdata)
{
	assert(e);

	e->handler = handler;
	e->userdata = userdata;
}

dispd_encoder_state_change_handler dispd_encoder_get_handler(struct dispd_encoder *e)
{
	assert(e);

	return e->handler;
}

enum dispd_encoder_state dispd_encoder_get_state(struct dispd_encoder *e)
{
	assert(e);
	
	return e->state;
}

static const char * state_to_name(enum dispd_encoder_state s)
{
	const char *names[] = {
		"NULL",
		"SPAWNED",
		"CONFIGURED",
		"READY",
		"STARTED",
		"PAUSED",
		"TERMINATED"
	};

	if(0 > s || DISPD_ENCODER_STATE_TERMINATED < s) {
		return "unknown encoder state";
	}

	return names[s];
}

static void dispd_encoder_set_state(struct dispd_encoder *e,
				enum dispd_encoder_state state)
{
	assert(e);
	
	if(e->state == state) {
		return;
	}

	log_debug("state change from %s to %s",
					state_to_name(e->state),
					state_to_name(state));

	e->state = state;
	dispd_encoder_notify_state_change(e, state);
}

static int on_encoder_properties_changed(sd_bus_message *m,
				void *userdata,
				sd_bus_error *ret_error)
{
	struct dispd_encoder *e = userdata;
	const char *name;
	int value;
	enum dispd_encoder_state s;
	int r;

	r = sd_bus_message_skip(m, "s");
	if(0 > r) {
		return log_ERRNO();
	}

	r = sd_bus_message_enter_container(m, 'a', "{sv}");
	if(0 > r) {
		return log_ERRNO();
	}

	while(!sd_bus_message_at_end(m, true)) {
		r = sd_bus_message_read(m, "{sv}", &name, "i", &value);
		if(0 > r) {
			return log_ERRNO();
		}

		if(strcmp("State", name)) {
			continue;
		}

		switch(value) {
			case 0:
				s = DISPD_ENCODER_STATE_NULL;
				break;
			case 1:
				s = DISPD_ENCODER_STATE_CONFIGURED;
				break;
			case 2:
				s = DISPD_ENCODER_STATE_READY;
				break;
			case 3:
				s = DISPD_ENCODER_STATE_STARTED;
				break;
			case 4:
				s = DISPD_ENCODER_STATE_PAUSED;
				break;
			case 5:
				s = DISPD_ENCODER_STATE_TERMINATED;
				break;
			default:
				log_error("encoder enter unknown state: %d", value);
				return 0;
		}

		dispd_encoder_set_state(e, s);
		break;
	}

	return 0;
}

static int on_encoder_disappeared(sd_bus_message *m,
				void *userdata,
				sd_bus_error *ret_error)
{
	struct dispd_encoder *e = userdata;

	log_info("encoder disappered");
	
	dispd_encoder_set_state(e, DISPD_ENCODER_STATE_TERMINATED);

	return 0;
}

static int dispd_encoder_on_unique_name(sd_event_source *source,
				int fd,
				uint32_t events,
				void *userdata)
{
	struct dispd_encoder *e = userdata;
	char buf[1024];
	ssize_t r;

	r = read(fd, buf, sizeof(buf) - 1);
	if(0 > r) {
		if(EAGAIN == errno) {
			return 0;
		}

		goto error;
	}
	else if(!r) {
		log_warning("no bus name returned from encoder: %s",
						strerror(errno));
		goto error;
	}

	// TODO remove heading and trailing speces from buf before strdup()
	buf[r] = '\0';
	log_info("got bus name from encoder: %s", buf);

	e->name = strdup(buf);
	if(!e->name) {
		goto error;
	}

	// TODO connect to encoder through user session bus
	r = sd_bus_default_system(&e->bus);
	if(0 > r) {
		goto error;
	}

	snprintf(buf, sizeof(buf), 
					"type='signal',"
						"sender='%s',"
							"path='/org/freedesktop/miracle/encoder',"
						"interface='org.freedesktop.DBus.Properties',"
							"member='PropertiesChanged',"
							"arg0='org.freedesktop.miracle.encoder'",
					e->name);
	r = sd_bus_add_match(e->bus, NULL,
						 buf,
						 on_encoder_properties_changed,
						 e);
	if(0 > r) {
		goto error;
	}

	snprintf(buf, sizeof(buf), 
					"type='signal',"
						"sender='org.freedesktop.DBus',"
							"path='/org/freedesktop/DBus',"
						"interface='org.freedesktop.DBus',"
							"member='NameOwnerChanged',"
							"arg0namespace='%s'",
					e->name);
	r = sd_bus_add_match(e->bus, NULL,
						 buf,
						 on_encoder_disappeared,
						 e);


	dispd_encoder_set_state(e, DISPD_ENCODER_STATE_SPAWNED);

	goto end;

error:
	dispd_encoder_kill_child(e);
end:
	dispd_encoder_close_pipe(e);

	return r;
}

static int config_append(sd_bus_message *m,
				enum wfd_encoder_config k,
				const char *t,
				...)
{
	int r;
	va_list argv;

	assert(m);
	assert(t);

	r = sd_bus_message_open_container(m, 'e', "iv");
	if(0 > r) {
		return r;
	}

	r = sd_bus_message_append(m, "i", k);
	if(0 > r) {
		return r;
	}

	r = sd_bus_message_open_container(m, 'v', t);
	if(0 > r) {
		return r;
	}

	va_start(argv, t);
	switch(*t) {
		case 's':
			r = sd_bus_message_append(m, t, va_arg(argv, char *));
			break;
		case 'u':
			r = sd_bus_message_append(m, t, va_arg(argv, uint32_t));
			break;
		default:
			abort();
	}
	va_end(argv);

	if(0 > r) {
		return r;
	}

	r = sd_bus_message_close_container(m);
	if(0 > r) {
		return r;
	}

	return sd_bus_message_close_container(m);
}

int dispd_encoder_configure(struct dispd_encoder *e, struct wfd_session *s)
{
	_cleanup_sd_bus_message_ sd_bus_message *call = NULL;
	_cleanup_sd_bus_message_ sd_bus_message *reply = NULL;
	_cleanup_sd_bus_error_ sd_bus_error error = SD_BUS_ERROR_NULL;
	const struct wfd_rectangle *rect;
	struct wfd_sink *sink;
	int r;

	assert(e);
	assert(s);
	assert(wfd_is_out_session(s));

	r = sd_bus_message_new_method_call(e->bus,
					&call,
					e->name,
					"/org/freedesktop/miracle/encoder",
					"org.freedesktop.miracle.encoder",
					"Configure");
	if(0 > r) {
		return r;
	}

	r = sd_bus_message_open_container(call, 'a', "{iv}");
	if(0 > r) {
		return r;
	}

	sink = wfd_out_session_get_sink(s);
	r = config_append(call,
					WFD_ENCODER_CONFIG_PEER_ADDRESS,
					"s",
					sink->peer->remote_address);
	if(0 > r) {
		return r;
	}

	r = config_append(call,
					WFD_ENCODER_CONFIG_RTP_PORT0,
					"u",
					s->stream.rtp_port);
	if(0 > r) {
		return r;
	}

	if(s->stream.rtcp_port) {
		r = config_append(call,
						WFD_ENCODER_CONFIG_PEER_RTCP_PORT,
						"u",
						s->stream.rtcp_port);
		if(0 > r) {
			return r;
		}
	}

	r = config_append(call,
					WFD_ENCODER_CONFIG_LOCAL_ADDRESS,
					"s",
					sink->peer->local_address);
	if(0 > r) {
		return r;
	}

	if(s->stream.rtcp_port) {
		r = config_append(call,
						WFD_ENCODER_CONFIG_LOCAL_RTCP_PORT,
						"u",
						s->stream.rtcp_port);
		if(0 > r) {
			return r;
		}
	}

	rect = wfd_session_get_disp_dimension(s);
	if(rect) {
		r = config_append(call,
						WFD_ENCODER_CONFIG_X,
						"u",
						rect->x);
		if(0 > r) {
			return r;
		}

		r = config_append(call,
						WFD_ENCODER_CONFIG_Y,
						"u",
						rect->y);
		if(0 > r) {
			return r;
		}

		r = config_append(call,
						WFD_ENCODER_CONFIG_WIDTH,
						"u",
						rect->width);
		if(0 > r) {
			return r;
		}

		r = config_append(call,
						WFD_ENCODER_CONFIG_HEIGHT,
						"u",
						rect->height);
		if(0 > r) {
			return r;
		}
	}

	r = sd_bus_message_close_container(call);
	if(0 > r) {
		return r;
	}

	r = sd_bus_call(e->bus, call, 0, &error, &reply);
	if(0 > r) {
		log_warning("%s: %s", error.name, error.message);
		sd_bus_error_free(&error);
	}

	return r;
}

static int dispd_encoder_call(struct dispd_encoder *e, const char *method)
{
	_cleanup_sd_bus_message_ sd_bus_message *call = NULL;
	_cleanup_sd_bus_message_ sd_bus_message *reply = NULL;
	sd_bus_error error = { 0 };
	int r = sd_bus_message_new_method_call(e->bus,
					&call,
					e->name,
					"/org/freedesktop/miracle/encoder",
					"org.freedesktop.miracle.encoder",
					method);
	if(0 > r) {
		return r;
	}

	r = sd_bus_call(e->bus, call, 0, &error, &reply);
	if(0 > r) {
		log_warning("error invoke method %s: %s, %s",
						method,
						error.name,
						error.message);
		sd_bus_error_free(&error);
	}

	return r;
}

int dispd_encoder_start(struct dispd_encoder *e)
{
	return dispd_encoder_call(e, "Start");
}

int dispd_encoder_pause(struct dispd_encoder *e)
{
	return dispd_encoder_call(e, "Pause");
}

int dispd_encoder_stop(struct dispd_encoder *e)
{
	return dispd_encoder_call(e, "Stop");
}
