/*
 * MiracleCast - Wifi-Display/Miracast Implementation
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

#ifndef CTL_SRC_H
#define CTL_SRC_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <systemd/sd-event.h>
#include <time.h>
#include <unistd.h>
#include "ctl.h"

#include "rtsp.h"
#include "shl_macro.h"
#include "shl_util.h"
#include "wfd.h"

enum audio_format {
	AUDIO_FORMAT_UNKNOWN,
	AUDIO_FORMAT_LPCM,
	AUDIO_FORMAT_AAC,
	AUDIO_FORMAT_AC3,
};

struct video_formats {
	uint8_t native_disp_mode;
	uint8_t pref_disp_mode;
	uint8_t codec_profile;
	uint8_t codec_level;
	unsigned int resolutions_cea;
	unsigned int resolutions_vesa;
	unsigned int resolutions_hh;
	uint8_t latency;
	unsigned short min_slice_size;
	unsigned short slice_enc_params;
	uint8_t frame_rate_control;
	int hres;
	int vres;
};

struct audio_codecs {
	enum audio_format format;
	unsigned int modes;
	uint8_t latency;
};

struct client_rtp_ports {
	char *profile;
	unsigned short port0;
	unsigned short port1;
};

struct wfd_src {
	sd_event *event;

	char *local;
	char *session;
	char url[256];
	struct sockaddr_storage addr;
	size_t addr_size;
	int fd;
	sd_event_source *fd_source;

	sd_event_source *req_source;

	struct rtsp *rtsp;

	struct {
		struct video_formats video_formats;
		struct audio_codecs audio_codecs;
		struct client_rtp_ports rtp_ports;

		bool has_video_formats : 1;
		bool has_audio_codecs : 1;
		bool has_rtp_ports : 1;
	} sink;

	bool connected : 1;
	bool hup : 1;
};

#endif /* CTL_SRC_H */
