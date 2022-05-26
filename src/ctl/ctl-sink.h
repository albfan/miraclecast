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

#ifndef CTL_SINK_H
#define CTL_SINK_H

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
#include <glib.h>
#include "ctl.h"

#include "rtsp.h"
#include "shl_macro.h"
#include "shl_util.h"
#include "wfd.h"

#define WFD_VIDEO_FORMATS "wfd_video_formats"
#define WFD_AUDIO_CODECS "wfd_audio_codecs"
#define WFD_UIBC_CAPABILITY "wfd_uibc_capability"

extern int rstp_port;
extern bool uibc_option;
extern bool uibc_enabled;
extern int uibc_port;

struct ctl_sink {
    sd_event *event;

    char *target;
    char *session;
    char *url;
    char *uibc_config;
    char *uibc_setting;
    struct sockaddr_storage addr;
    size_t addr_size;
    int fd;
    sd_event_source *fd_source;

    struct rtsp *rtsp;

    bool connected : 1;
    bool hup : 1;

    uint32_t resolutions_cea;
    uint32_t resolutions_vesa;
    uint32_t resolutions_hh;

    int hres;
    int vres;

    GHashTable* protocol_extensions;
};

bool check_rtsp_option(struct rtsp_message *m, char *option);

#define check_and_response_option(option, response) \
    if (check_rtsp_option(m, option)) { \
        char option_response[512]; \
        sprintf(option_response, "%s: %s", option, response); \
        r = rtsp_message_append(rep, "{&}", option_response); \
        if (r < 0) {\
            return cli_vERR(r); \
        } \
    }

#endif /* CTL_SINK_H */
