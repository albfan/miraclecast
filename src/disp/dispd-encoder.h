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

#include "shl_macro.h"

#ifndef DISPD_ENCODER_H
#define DISPD_ENCODER_H

#define _dispd_encoder_unref_ _shl_cleanup_(dispd_encoder_unrefp)

enum wfd_encoder_config
{
	WFD_ENCODER_CONFIG_DISPLAY_TYPE,		/* string */
	WFD_ENCODER_CONFIG_DISPLAY_NAME,		/* string */
	WFD_ENCODER_CONFIG_MONITOR_NUM,			/* uint32 */
	WFD_ENCODER_CONFIG_X,					/* uint32 */
	WFD_ENCODER_CONFIG_Y,					/* uint32 */
	WFD_ENCODER_CONFIG_WIDTH,				/* uint32 */
	WFD_ENCODER_CONFIG_HEIGHT,				/* uint32 */
	WFD_ENCODER_CONFIG_WINDOW_ID,			/* uint32 */
	WFD_ENCODER_CONFIG_FRAMERATE,			/* uint32 */
	WFD_ENCODER_CONFIG_SCALE_WIDTH,			/* uint32 */
	WFD_ENCODER_CONFIG_SCALE_HEIGHT,		/* uint32 */
	WFD_ENCODER_CONFIG_AUDIO_TYPE,			/* string */
	WFD_ENCODER_CONFIG_AUDIO_DEV,			/* string */
	WFD_ENCODER_CONFIG_PEER_ADDRESS,		/* string */
	WFD_ENCODER_CONFIG_RTP_PORT0,			/* uint32 */
	WFD_ENCODER_CONFIG_RTP_PORT1,			/* uint32 */
	WFD_ENCODER_CONFIG_PEER_RTCP_PORT,		/* uint32 */
	WFD_ENCODER_CONFIG_LOCAL_ADDRESS,		/* uint32 */
	WFD_ENCODER_CONFIG_LOCAL_RTCP_PORT,		/* uint32 */
	WFD_ENCODER_CONFIG_H264_PROFILE,
	WFD_ENCODER_CONFIG_H264_LEVEL,
	WFD_ENCODER_CONFIG_DEBUG_LEVEL,
};

enum dispd_encoder_state
{
	DISPD_ENCODER_STATE_NULL = 0,
	DISPD_ENCODER_STATE_SPAWNED,
	DISPD_ENCODER_STATE_CONFIGURED,
	DISPD_ENCODER_STATE_READY,
	DISPD_ENCODER_STATE_STARTED,
	DISPD_ENCODER_STATE_PAUSED,
	DISPD_ENCODER_STATE_TERMINATED,
};

struct wfd_session;
struct dispd_encoder;

typedef void (*dispd_encoder_state_change_handler)(struct dispd_encoder *e,
				enum dispd_encoder_state state,
				void *userdata);

int dispd_encoder_spawn(struct dispd_encoder **out, struct wfd_session *s);
struct dispd_encoder * dispd_encoder_ref(struct dispd_encoder *e);
void dispd_encoder_unref(struct dispd_encoder *e);
void dispd_encoder_unrefp(struct dispd_encoder **e);

int dispd_encoder_configure(struct dispd_encoder *e, struct wfd_session *s);
int dispd_encoder_start(struct dispd_encoder *e);
int dispd_encoder_pause(struct dispd_encoder *e);
int dispd_encoder_stop(struct dispd_encoder *e);

void dispd_encoder_set_handler(struct dispd_encoder *e,
				dispd_encoder_state_change_handler handler,
				void *userdata);
dispd_encoder_state_change_handler dispd_encoder_get_handler(struct dispd_encoder *e);
enum dispd_encoder_state dispd_encoder_get_state(struct dispd_encoder *e);

#endif /* DISPD_ENCODER_H */
