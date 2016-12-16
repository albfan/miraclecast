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

#include "ctl-src.h"
#include "util.h"

#define DEFAULT_RTSP_PORT		(7236)

/*
 * RTSP Session
 */

static void src_handle_options(struct ctl_src *s,
				struct rtsp_message *m)
{
	_rtsp_message_unref_ struct rtsp_message *rep = NULL;
	int r;

	cli_debug("INCOMING (M2): %s\n", rtsp_message_get_raw(m));

	r = rtsp_message_new_reply_for(m, &rep, RTSP_CODE_OK, NULL);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	r = rtsp_message_append(rep, "<s>",
				"Public",
				"org.wfa.wfd1.0, GET_PARAMETER, SET_PARAMETER, "
				"SETUP, PLAY, PAUSE, TEARDOWN");
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	rtsp_message_seal(rep);
	cli_debug("OUTGOING (M2): %s\n", rtsp_message_get_raw(rep));

	r = rtsp_send(s->rtsp, rep);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	return;

error:
	ctl_src_close(s);
	ctl_fn_src_disconnected(s);
}

static int src_trigger_play_rep_fn(struct rtsp *bus,
				struct rtsp_message *m,
				void *data)
{
	cli_debug("INCOMING (M5): %s\n", rtsp_message_get_raw(m));
}

static void src_handle_setup(struct ctl_src *s,
				struct rtsp_message *m)
{
	_rtsp_message_unref_ struct rtsp_message *rep = NULL;
	_rtsp_message_unref_ struct rtsp_message *req = NULL;
	int r;

	cli_debug("INCOMING (M6): %s\n", rtsp_message_get_raw(m));

	r = rtsp_message_new_reply_for(m, &rep, RTSP_CODE_OK, NULL);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	r = rtsp_message_append(rep, "<s>",
			"Session", "0;timeout=30");
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	char buf[256];
	snprintf(buf, sizeof(buf), "RTP/AVP/UDP;unicast;client_port=%d", s->sink.rtp_ports.port0);
	r = rtsp_message_append(rep, "<s>",
			"Transport", buf);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	rtsp_message_seal(rep);
	cli_debug("OUTGOING (M6): %s\n", rtsp_message_get_raw(rep));

	r = rtsp_send(s->rtsp, rep);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	r = rtsp_message_new_request(s->rtsp,
					 &req,
					 "SET_PARAMETER",
					 s->url);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	r = rtsp_message_append(req, "{&}", "wfd_trigger_method: PLAY");
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	rtsp_message_seal(req);
	cli_debug("OUTGOING (M5): %s\n", rtsp_message_get_raw(req));

	r = rtsp_call_async(s->rtsp, req, src_trigger_play_rep_fn, s, 0, NULL);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	ctl_fn_src_setup(s);

	return;

error:
	ctl_src_close(s);
	ctl_fn_src_disconnected(s);
}

static void src_handle_play(struct ctl_src *s,
				struct rtsp_message *m)
{
	_rtsp_message_unref_ struct rtsp_message *rep = NULL;
	_rtsp_message_unref_ struct rtsp_message *req = NULL;
	int r;

	cli_debug("INCOMING (M7): %s\n", rtsp_message_get_raw(m));

	r = rtsp_message_new_reply_for(m, &rep, RTSP_CODE_OK, NULL);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	r = rtsp_message_append(rep, "<s>",
			"Session", "0;timeout=30");
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	r = rtsp_message_append(rep, "<s>",
			"Range", "ntp=now-");
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	rtsp_message_seal(rep);
	cli_debug("OUTGOING (M7): %s\n", rtsp_message_get_raw(rep));

	r = rtsp_send(s->rtsp, rep);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	ctl_fn_src_playing(s);

	return;

error:
	ctl_src_close(s);
	ctl_fn_src_disconnected(s);
}

static void src_handle_pause(struct ctl_src *s,
				struct rtsp_message *m)
{
	cli_debug("INCOMING (M9): %s\n", rtsp_message_get_raw(m));
}

static void src_handle_teardown(struct ctl_src *s,
				struct rtsp_message *m)
{
	cli_debug("INCOMING (M8): %s\n", rtsp_message_get_raw(m));
}

static bool parse_video_formats(struct rtsp_message *m,
				struct video_formats *formats)
{
	const char *param;
	int r;
   
	r = rtsp_message_read(m, "{<&>}", "wfd_video_formats", &param);
	if(r < 0) {
		goto error;
	}
	else if(!strncmp("none", param, 4) || strlen(param) < 55) {
		return false;
	}

	r = sscanf(param, "%hhx %hhx %hhx %hhx %x %x %x %hhx %hx %hx %hhx",
			&formats->native_disp_mode,
			&formats->pref_disp_mode,
			&formats->codec_profile,
			&formats->codec_level,
			&formats->resolutions_cea,
			&formats->resolutions_vesa,
			&formats->resolutions_hh,
			&formats->latency,
			&formats->min_slice_size,
			&formats->slice_enc_params,
			&formats->frame_rate_control);
	if(r < 11) {
		goto error;
	}

	formats->hres = -1;
	formats->vres = -1;
	sscanf(param + 55, "%hx %hx", &formats->hres, &formats->vres);

	return true;

error:
	cli_printf("[" CLI_RED "ERROR" CLI_DEFAULT "] Invalid video formats\n");
	return false;
}

static bool parse_audio_codecs(struct rtsp_message *m,
				struct audio_codecs *codecs)
{
	const char *param;
	int r;
   
	r = rtsp_message_read(m, "{<&>}", "wfd_audio_codecs", &param);
	if(r < 0) {
		goto error;
	}
	else if(!strncmp("none", param, 4) || strlen(param) < 4) {
		return false;
	}

	cli_printf("audio codecs: %s\n", param);

	if(!strncmp("LPCM", param, 4)) {
		codecs->format = AUDIO_FORMAT_LPCM;
	}
	else if(!strncmp("AAC", param, 3)) {
		codecs->format = AUDIO_FORMAT_AAC;
	}
	else if(!strncmp("AC3", param, 3)) {
		codecs->format = AUDIO_FORMAT_AC3;
	}
	else {
		goto error;
	}

	r = sscanf(param, "%x %hhx",
			&codecs->modes,
			&codecs->latency);
	if(r < 2) {
		goto error;
	}

	return true;

error:
	cli_printf("[" CLI_RED "ERROR" CLI_DEFAULT "] Invalid audio codecs\n");
	return false;
}

static bool parse_client_rtp_ports(struct rtsp_message *m,
				struct client_rtp_ports *ports)
{
	const char *param;
	int r;
	char mode[10] = "";
   
	r = rtsp_message_read(m, "{<&>}", "wfd_client_rtp_ports", &param);
	if(r < 0) {
		goto error;
	}

	r = sscanf(param, "%ms %hu %hu %9s",
			&ports->profile,
			&ports->port0,
			&ports->port1,
			mode);
	if(r < 4 || strcmp("mode=play", mode)) {
		goto error;
	}

	return true;

error:
	cli_printf("[" CLI_RED "ERROR" CLI_DEFAULT "] Invalid client RTP ports\n");
	return false;
}

static int src_trigger_setup_rep_fn(struct rtsp *bus,
				struct rtsp_message *m,
				void *data)
{
	struct ctl_src *s = data;
	_rtsp_message_unref_ struct rtsp_message *req = NULL;
	int r;

	cli_debug("INCOMING (M5): %s\n", rtsp_message_get_raw(m));

	if(rtsp_message_is_reply(m, RTSP_CODE_OK, NULL)) {
		return 0;
	}

	cli_printf("[" CLI_RED "ERROR" CLI_DEFAULT "] Sink failed to SETUP\n");

	ctl_src_close(s);
	ctl_fn_src_disconnected(s);

	return r;
}

static int src_set_parameter_rep_fn(struct rtsp *bus,
				struct rtsp_message *m,
				void *data)
{
	struct ctl_src *s = data;
	_rtsp_message_unref_ struct rtsp_message *req = NULL;
	int r;

	cli_debug("INCOMING (M4): %s\n", rtsp_message_get_raw(m));

	if(!rtsp_message_is_reply(m, RTSP_CODE_OK, NULL)) {
		r = -1;
		goto error;
	}

	r = rtsp_message_new_request(s->rtsp,
					 &req,
					 "SET_PARAMETER",
					 s->url);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	r = rtsp_message_append(req, "{&}", "wfd_trigger_method: SETUP");
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	rtsp_message_seal(req);
	cli_debug("OUTGOING (M5): %s\n", rtsp_message_get_raw(req));

	r = rtsp_call_async(s->rtsp, req, src_trigger_setup_rep_fn, s, 0, NULL);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	return 0;

error:
	cli_printf("[" CLI_RED "ERROR" CLI_DEFAULT "] SETUP failed\n");
	return r;
}

char buf[1024];

static int src_send_set_parameter(struct ctl_src *s)
{
	_rtsp_message_unref_ struct rtsp_message *req;
	int r;
	const static char tmp[] =
			"wfd_video_formats: 38 00 02 10 00000080 00000000 00000000 00 0000 0000 11 none none\n"
			//"wfd_audio_codecs: AAC 00000001 00\n"
			//"wfd_uibc_capability: input_category_list=GENERIC\n;generic_cap_list=SingleTouch;hidc_cap_list=none;port=5100\n"
			//"wfd_uibc_setting: disable\n"
			"wfd_presentation_URL: %s/streamid=0 none\n"
			"wfd_client_rtp_ports: %s %d %d mode=play";

	r = rtsp_message_new_request(s->rtsp,
					 &req,
					 "SET_PARAMETER",
					 s->url);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	snprintf(buf, sizeof(buf), tmp, s->url, s->sink.rtp_ports.profile,
			s->sink.rtp_ports.port0, s->sink.rtp_ports.port1);

	r = rtsp_message_append(req, "{&}", buf);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	rtsp_message_seal(req);
	cli_debug("OUTGOING (M4): %s\n", rtsp_message_get_raw(req));

	r = rtsp_call_async(s->rtsp, req, src_set_parameter_rep_fn, s, 0, NULL);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	return 0;

error:
	ctl_src_close(s);
	ctl_fn_src_disconnected(s);

	return r;
}

static int src_get_parameter_rep_fn(struct rtsp *bus,
				struct rtsp_message *m,
				void *data)
{
	struct ctl_src *s = data;
	int r;
	const char *param;
	int n_params;

	cli_debug("INCOMING (M3): %s\n", rtsp_message_get_raw(m));

	if (!rtsp_message_is_reply(m, RTSP_CODE_OK, NULL)) {
		cli_printf("[" CLI_RED "ERROR" CLI_DEFAULT "] GET_PARAMETER failed\n");
		r = -1;
		goto error;
	}

	free(s->sink.rtp_ports.profile);
	s->sink.rtp_ports.profile = NULL;

	s->sink.has_video_formats = parse_video_formats(m, &s->sink.video_formats);
	//s->sink.has_audio_codecs = parse_audio_codecs(m, &s->sink.audio_codecs);
	s->sink.has_rtp_ports = parse_client_rtp_ports(m, &s->sink.rtp_ports);

	r = src_send_set_parameter(s);

	return 0;

error:
	ctl_src_close(s);
	ctl_fn_src_disconnected(s);

	return -EINVAL;
}

static int src_options_rep_fn(struct rtsp *bus,
				struct rtsp_message *m,
				void *data)
{
	struct ctl_src *s = data;
	_rtsp_message_unref_ struct rtsp_message *req = NULL;
	int r;

	cli_debug("INCOMING (M1): %s\n", rtsp_message_get_raw(m));

	if(!rtsp_message_is_reply(m, RTSP_CODE_OK, NULL)) {
		cli_printf("[" CLI_RED "ERROR" CLI_DEFAULT "] Failed to get OPTIONS from sink\n");
		goto error;
	}

	r = rtsp_message_new_request(s->rtsp,
					 &req,
					 "GET_PARAMETER",
					 s->url);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	r = rtsp_message_append(req, "{&}",
			"wfd_video_formats\n"
			//"wfd_audio_codecs\n"
			"wfd_client_rtp_ports\n"
			//"wfd_uibc_capability"
	);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	rtsp_message_seal(req);
	cli_debug("OUTGOING (M3): %s\n", rtsp_message_get_raw(req));

	r = rtsp_call_async(s->rtsp, req, src_get_parameter_rep_fn, s, 0, NULL);
	if (r < 0) {
		cli_vERR(r);
		goto error;
	}

	return 0;

error:
	ctl_src_close(s);
	ctl_fn_src_disconnected(s);

	return r;
}

static void src_handle(struct ctl_src *s,
			struct rtsp_message *m)
{
	const char *method;

	if(!m) {
		ctl_src_close(s);
		ctl_fn_src_disconnected(s);
		return;
	}

	method = rtsp_message_get_method(m);
	if(!method) {
		cli_debug("INCOMING: Unexpected message (%d): %s\n",
					rtsp_message_get_type(m),
					rtsp_message_get_raw(m));
	}
	else if (!strcmp(method, "OPTIONS")) {
		src_handle_options(s, m);
	} else if (!strcmp(method, "SETUP")) {
		src_handle_setup(s, m);
	} else if (!strcmp(method, "PLAY")) {
		src_handle_play(s, m);
	} else if (!strcmp(method, "PAUSE")) {
		src_handle_pause(s, m);
	} else if (!strcmp(method, "TEARDOWN")) {
		src_handle_teardown(s, m);
	}
}

static int src_rtsp_fn(struct rtsp *bus,
			struct rtsp_message *m,
			void *data)
{
	struct ctl_src *s = data;

	if (!m)
		s->hup = true;
	else
		src_handle(s, m);

	if (s->hup) {
		ctl_src_close(s);
		ctl_fn_src_disconnected(s);
	}

	return 0;
}

static void src_send_options(struct ctl_src *s)
{
	_rtsp_message_unref_ struct rtsp_message *req = NULL;
	int r;

	r = rtsp_message_new_request(s->rtsp,
					 &req,
					 "OPTIONS",
					 "*");
	if (r < 0)
		return cli_vERR(r);

	r = rtsp_message_append(req, "<s>",
				"Require",
				"org.wfa.wfd1.0");
	if (r < 0)
		return cli_vERR(r);

	rtsp_message_seal(req);

	r = rtsp_call_async(s->rtsp, req, src_options_rep_fn, s, 0, NULL);
	if (r < 0)
		return cli_vERR(r);

	cli_debug("OUTGOING (M1): %s\n", rtsp_message_get_raw(req));
}

/*
 * Source I/O
 */

static void src_connected(struct ctl_src *s)
{
	int r, val;
	struct sockaddr_storage addr;
	socklen_t len;
	char buf[64];

	cli_printf("got incomming connection request\n");

	if (s->connected || s->hup)
		return;

	sd_event_source_set_enabled(s->fd_source, SD_EVENT_OFF);

	len = sizeof(addr);
	int fd = accept4(s->fd, (struct sockaddr *) &addr, &len, SOCK_CLOEXEC);

	r = getsockopt(fd, SOL_SOCKET, SO_ERROR, &val, &len);
	if (r < 0) {
		s->hup = true;
		cli_vERRNO();
		return;
	} else if (val) {
		s->hup = true;
		errno = val;
		cli_error("cannot connect to remote host (%d): %m", errno);
		return;
	}

	cli_debug("connection established");

	close(s->fd);
	s->fd = fd;
	s->addr = addr;
	s->addr_size = len;

	r = rtsp_open(&s->rtsp, s->fd);
	if (r < 0)
		goto error;

	r = rtsp_attach_event(s->rtsp, s->event, 0);
	if (r < 0)
		goto error;

	r = rtsp_add_match(s->rtsp, src_rtsp_fn, s);
	if (r < 0)
		goto error;

	s->connected = true;
	ctl_fn_src_connected(s);

	src_send_options(s);

	return;

error:
	s->hup = true;
	cli_vERR(r);
}

static void src_io(struct ctl_src *s, uint32_t mask)
{
	cli_notice("src_io: %u", mask);

	if (mask & EPOLLIN) {
		src_connected(s);
	}

	if (mask & EPOLLERR) {
		cli_notice("ERR on socket");
		s->hup = true;
	}

	if (s->hup) {
		ctl_src_close(s);
		ctl_fn_src_disconnected(s);
	}
}

static int src_io_fn(sd_event_source *source,
			  int fd,
			  uint32_t mask,
			  void *data)
{
	src_io(data, mask);
	return 0;
}

static int src_listen(struct ctl_src *s)
{
	int fd, r, enable = 1;

	if (!s)
		return cli_EINVAL();
	if (s->fd >= 0)
		return 0;
	if (!s->addr.ss_family || !s->addr_size)
		return cli_EINVAL();

	fd = socket(s->addr.ss_family,
			SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
			0);
	if (fd < 0)
		return cli_ERRNO();

	r = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(enable));
	if(r < 0) {
		r = -errno;
		cli_vERR(r);
		goto err_close;
	}

	r = bind(fd, (struct sockaddr*)&s->addr, s->addr_size);
	if (r < 0) {
		r = -errno;
		cli_vERR(r);
		goto err_close;
	}

	r = listen(fd, 1);
	if (r < 0) {
		r = -errno;
		if (r != -EINPROGRESS) {
			cli_vERR(r);
			goto err_close;
		}
	}

	cli_printf("Wait for RTSP connection request from sink...\n");

	r = sd_event_add_io(s->event,
				&s->fd_source,
				fd,
				EPOLLERR | EPOLLIN | EPOLLET,
				src_io_fn,
				s);
	if (r < 0) {
		cli_vERR(r);
		goto err_close;
	}

	s->fd = fd;
	return 0;

err_close:
	close(fd);
	return r;
}

static void src_close(struct ctl_src *s)
{
	if (!s || s->fd < 0)
		return;

	free(s->sink.rtp_ports.profile);
	s->sink.rtp_ports.profile = NULL;
	rtsp_remove_match(s->rtsp, src_rtsp_fn, s);
	rtsp_detach_event(s->rtsp);
	rtsp_unref(s->rtsp);
	s->rtsp = NULL;
	sd_event_source_unref(s->fd_source);
	s->fd_source = NULL;
	close(s->fd);
	s->fd = -1;
	s->connected = false;
	s->hup = false;
}

/*
 * Source Management
 */

int ctl_src_new(struct ctl_src **out,
		 sd_event *event)
{
	struct ctl_src *s;

	if (!out || !event)
		return cli_EINVAL();

	s = calloc(1, sizeof(*s));
	if (!s)
		return cli_ENOMEM();

	s->event = sd_event_ref(event);
	s->fd = -1;

	*out = s;
	return 0;
}

void ctl_src_free(struct ctl_src *s)
{
	if (!s)
		return;

	ctl_src_close(s);
	free(s->local);
	free(s->session);
	sd_event_unref(s->event);
	free(s);
}

int ctl_src_listen(struct ctl_src *s, const char *local)
{
	struct sockaddr_in addr = { };
	char *l;
	int r;

	if (!s || !local || s->fd >= 0)
		return cli_EINVAL();

	addr.sin_family = AF_INET;
	addr.sin_port = htons(DEFAULT_RTSP_PORT);
	r = inet_pton(AF_INET, local, &addr.sin_addr);
	if (r != 1)
		return cli_EINVAL();

	l = strdup(local);
	if (!l)
		return cli_ENOMEM();

	free(s->local);
	s->local = l;

	memcpy(&s->addr, &addr, sizeof(addr));
	s->addr_size = sizeof(addr);

	snprintf(s->url, sizeof(s->url), "rtsp://%s/wfd1.0", local);

	return src_listen(s);
}

void ctl_src_close(struct ctl_src *s)
{
	if (!s)
		return;

	src_close(s);
}

bool ctl_src_is_connecting(struct ctl_src *s)
{
	return s && s->fd >= 0 && !s->connected;
}

bool ctl_src_is_connected(struct ctl_src *s)
{
	return s && s->connected;
}

bool ctl_src_is_closed(struct ctl_src *s)
{
	return !s || s->fd < 0;
}
