/*
 * MiracleCast - Wifi-Display/Miracast Implementation
 *
 * Copyright (c) 2014 Andrey Gusakov <andrey.gusakov@cogentembedded.com>
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

#ifndef WFD_H
#define WFD_H

#include <assert.h>

#define wfd_sube_is_device_info(w) (WFD_SUBE_ID_DEVICE_INFO == (w)->id)

#define WFD_DEVINFO_DEV_TYPE_MASK			(0x3 << 0)
#define WFD_DEVINFO_SRC_COUPLED_SINK_MASK	(0x1 << 2)
#define WFD_DEVINFO_SINK_COUPLED_SINK_MASK	(0x1 << 3)
#define WFD_DEVINFO_SESSION_MASK			(0x3 << 4)
#define WFD_DEVINFO_WSD_MASK				(0x1 << 6)
#define WFD_DEVINFO_PC_MASK					(0x1 << 7)
#define WFD_DEVINFO_CP_MASK					(0x1 << 8)
#define WFD_DEVINFO_TIME_SYNC_MASK			(0x1 << 9)
#define WFD_DEVINFO_PRI_SINK_AUDIO_MASK		(0x1 << 10)
#define WFD_DEVINFO_SRC_AUDIO_ONLY_MASK		(0x1 << 11)

enum wfd_sube_id {
	WFD_SUBE_ID_DEVICE_INFO,
	WFD_SUBE_ID_ASSOCIATED_BSSID,
	WFD_SUBE_ID_AUDIO_FORMATS,
	WFD_SUBE_ID_VIDEO_FORMATS,
	WFD_SUBE_ID_3D_VIDEO_FORMATS,
	WFD_SUBE_ID_CONTENT_PROTECTION,
	WFD_SUBE_ID_COUPLED_SINK_INFO,
	WFD_SUBE_ID_WFD_EXT_CAPS,
	WFD_SUBE_ID_LOCAL_IP_ADDR,
	WFD_SUBE_ID_RESERVED,
};

enum wfd_resolution_standard
{
	WFD_RESOLUTION_STANDARD_CEA,
	WFD_RESOLUTION_STANDARD_VESA,
	WFD_RESOLUTION_STANDARD_HH,
};

union wfd_sube
{
	enum wfd_sube_id id;

	struct {
		enum wfd_sube_id id;
		uint16_t dev_info;
		uint16_t rtsp_port;
		uint16_t max_throughput;
	} dev_info;

	struct {
		enum wfd_sube_id id;
		uint32_t cea;
		uint32_t vesa;
		uint32_t hh;
		uint8_t native;
		uint8_t profiles;
		uint8_t levels;
		uint8_t latency;
		uint16_t min_slice_size;
		uint16_t slice_enc_params;
		uint8_t video_frame_rate_ctl;
	} video_formats;

	struct {
		enum wfd_sube_id id;
		uint32_t lpcm_modes;
		uint8_t lpcm_dec_latency;
		uint32_t aac_modes;
		uint8_t aac_dec_latency;
		uint32_t ac3_modes;
		uint8_t ac3_dec_latency;
	} audio_formats;

	struct {
		enum wfd_sube_id id;
		uint16_t caps;
	} extended_caps;
};

struct wfd_resolution
{
	uint16_t index;
	uint16_t hres;
	uint16_t vres;
	uint16_t fps;
	bool progressive: 1;
};

void wfd_print_resolutions(char * prefix);
int wfd_get_resolutions(enum wfd_resolution_standard std,
				int index,
				struct wfd_resolution *out);
int vfd_get_cea_resolution(uint32_t mask, int *hres, int *vres);
int vfd_get_vesa_resolution(uint32_t mask, int *hres, int *vres);
int vfd_get_hh_resolution(uint32_t mask, int *hres, int *vres);
int wfd_sube_parse(const char *in, union wfd_sube *out);

static inline int wfd_sube_device_get_type(const union wfd_sube *sube)
{
	assert(wfd_sube_is_device_info(sube));

	return (WFD_DEVINFO_DEV_TYPE_MASK & sube->dev_info.dev_info);
}

static inline bool wfd_sube_device_is_source(const union wfd_sube *sube)
{
	switch(wfd_sube_device_get_type(sube)) {
		case 0:
		case 3:
			return true;
	}

	return false;
}

static inline bool wfd_sube_device_is_sink(const union wfd_sube *sube)
{
	switch(wfd_sube_device_get_type(sube)) {
		case 1:
		case 2:
		case 3:
			return true;
	}

	return false;
}

static inline int wfd_sube_src_support_coupled_sink(const union wfd_sube *sube)
{
	assert(WFD_SUBE_ID_DEVICE_INFO == sube->id);

	return !!(WFD_DEVINFO_SRC_COUPLED_SINK_MASK & sube->dev_info.dev_info);
}

static inline uint16_t wfd_sube_device_get_rtsp_port(const union wfd_sube *sube)
{
	assert(wfd_sube_is_device_info(sube));

	return sube->dev_info.rtsp_port;
}

#endif /* WFD_H */
