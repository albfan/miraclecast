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

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "ctl.h"
#include "wfd.h"
#include "util.h"

typedef int (*wfd_sube_parse_func)(const char *in, union wfd_sube *out);

struct wfd_sube_info
{
	wfd_sube_parse_func parser;
	uint8_t len;
};

static int wfd_sube_parse_device_info(const char *in, union wfd_sube *out);
static int wfd_sube_parse_audio_formats(const char *in, union wfd_sube *out);
static int wfd_sube_parse_video_formats(const char *in, union wfd_sube *out);
static int wfd_sube_parse_ext_caps(const char *in, union wfd_sube *out);

/*
 * CEA resolutions and refrash rate bitmap/index table
 * also used in native resolution field
 */
static const struct wfd_resolution resolutions_cea[] = {
	{0,   640,	480, 60, 1},	/* p60 */
	{1,   720,	480, 60, 1},	/* p60 */
	{2,   720,	480, 60, 0},	/* i60 */
	{3,   720,	576, 50, 1},	/* p50 */
	{4,   720,	576, 50, 0},	/* i50 */
	{5,  1280,	720, 30, 1},	/* p30 */
	{6,  1280,	720, 60, 1},	/* p60 */
	{7,  1920, 1080, 30, 1},	/* p30 */
	{8,  1920, 1080, 60, 1},	/* p60 */
	{9,  1920, 1080, 60, 0},	/* i60 */
	{10, 1280,	720, 25, 1},	/* p25 */
	{11, 1280,	720, 50, 1},	/* p50 */
	{12, 1920, 1080, 25, 1},	/* p25 */
	{13, 1920, 1080, 50, 1},	/* p50 */
	{14, 1920, 1080, 50, 0},	/* i50 */
	{15, 1280,	720, 24, 1},	/* p24 */
	{16, 1920, 1080, 24, 1},	/* p24 */
};

static const struct wfd_resolution resolutions_vesa[] = {
	{0,   800,	600, 30, 1},	/* p30 */
	{1,   800,	600, 60, 1},	/* p60 */
	{2,  1024,	768, 30, 1},	/* p30 */
	{3,  1024,	768, 60, 1},	/* p60 */
	{4,  1152,	854, 30, 1},	/* p30 */
	{5,  1152,	854, 60, 1},	/* p60 */
	{6,  1280,	768, 30, 1},	/* p30 */
	{7,  1280,	768, 60, 1},	/* p60 */
	{8,  1280,	800, 30, 1},	/* p30 */
	{9,  1280,	800, 60, 1},	/* p60 */
	{10, 1360,	768, 30, 1},	/* p30 */
	{11, 1360,	768, 60, 1},	/* p60 */
	{12, 1366,	768, 30, 1},	/* p30 */
	{13, 1366,	768, 60, 1},	/* p60 */
	{14, 1280, 1024, 30, 1},	/* p30 */
	{15, 1280, 1024, 60, 1},	/* p60 */
	{16, 1440, 1050, 30, 1},	/* p30 */
	{17, 1440, 1050, 60, 1},	/* p60 */
	{18, 1440,	900, 30, 1},	/* p30 */
	{19, 1440,	900, 60, 1},	/* p60 */
	{20, 1600,	900, 30, 1},	/* p30 */
	{21, 1600,	900, 60, 1},	/* p60 */
	{22, 1600, 1200, 30, 1},	/* p30 */
	{23, 1600, 1200, 60, 1},	/* p60 */
	{24, 1680, 1024, 30, 1},	/* p30 */
	{25, 1680, 1024, 60, 1},	/* p60 */
	{26, 1680, 1050, 30, 1},	/* p30 */
	{27, 1680, 1050, 60, 1},	/* p60 */
	{28, 1920, 1200, 30, 1},	/* p30 */
};

static const struct wfd_resolution resolutions_hh[] = {
	{0,   800,	480, 30, 1},	/* p30 */
	{1,   800,	480, 60, 1},	/* p60 */
	{2,   854,	480, 30, 1},	/* p30 */
	{3,   854,	480, 60, 1},	/* p60 */
	{4,   864,	480, 30, 1},	/* p30 */
	{5,   864,	480, 60, 1},	/* p60 */
	{6,   640,	360, 30, 1},	/* p30 */
	{7,   640,	360, 60, 1},	/* p60 */
	{8,   960,	540, 30, 1},	/* p30 */
	{9,   960,	540, 60, 1},	/* p60 */
	{10,  848,	480, 30, 1},	/* p30 */
	{11,  848,	480, 60, 1},	/* p60 */
};

static const struct wfd_sube_info parser_tbl[WFD_SUBE_ID_RESERVED] = {
	[WFD_SUBE_ID_DEVICE_INFO] = { .parser = wfd_sube_parse_device_info, .len = 6 },
	[WFD_SUBE_ID_AUDIO_FORMATS] = { .parser = wfd_sube_parse_audio_formats, .len = 15 },
	[WFD_SUBE_ID_VIDEO_FORMATS] = { .parser = wfd_sube_parse_video_formats, .len = 21 },
	[WFD_SUBE_ID_WFD_EXT_CAPS] = { .parser = wfd_sube_parse_ext_caps, .len = 2 },
};

int wfd_get_resolutions(enum wfd_resolution_standard std,
				int index,
				struct wfd_resolution *out)
{
	switch(std) {
	case WFD_RESOLUTION_STANDARD_CEA:
		if(0 >= index || index < SHL_ARRAY_LENGTH(resolutions_cea)) {
			break;
		}
		*out = resolutions_cea[index];
		return 0;
	case WFD_RESOLUTION_STANDARD_VESA:
		if(0 >= index || index < SHL_ARRAY_LENGTH(resolutions_vesa)) {
			break;
		}
		*out = resolutions_vesa[index];
		return 0;
	case WFD_RESOLUTION_STANDARD_HH:
		if(0 >= index || index < SHL_ARRAY_LENGTH(resolutions_hh)) {
			break;
		}
		*out = resolutions_hh[index];
		return 0;
	default:
		break;
	}

	return -EINVAL;
}

void wfd_print_resolutions(void)
{
	int i;

	printf("CEA resolutions:\n");
	for (i = 0; i < SHL_ARRAY_LENGTH(resolutions_cea); i++) {
		printf("\t%2d %08x %4dx%4d@%d\n",
			resolutions_cea[i].index, 1 << resolutions_cea[i].index,
			resolutions_cea[i].hres, resolutions_cea[i].vres,
			resolutions_cea[i].fps);
	}
	printf("VESA resolutions:\n");
	for (i = 0; i < SHL_ARRAY_LENGTH(resolutions_vesa); i++) {
		printf("\t%2d %08x %4dx%4d@%d\n",
			resolutions_vesa[i].index, 1 << resolutions_vesa[i].index,
			resolutions_vesa[i].hres, resolutions_vesa[i].vres,
			resolutions_vesa[i].fps);
	}
	printf("HH resolutions:\n");
	for (i = 0; i < SHL_ARRAY_LENGTH(resolutions_hh); i++) {
		printf("\t%2d %08x %4dx%4d@%d\n",
			resolutions_hh[i].index, 1 << resolutions_hh[i].index,
			resolutions_hh[i].hres, resolutions_hh[i].vres,
			resolutions_hh[i].fps);
	}
}

uint32_t vfd_generate_resolution_mask(unsigned int index)
{
	return ((1 << (index + 1)) - 1);
}

void vfd_dump_resolutions(uint32_t cea_mask, uint32_t vesa_mask, uint32_t hh_mask)
{
	int i;

	if (cea_mask) {
		cli_debug("CEA resolutions:");
		for (i = 0; i < SHL_ARRAY_LENGTH(resolutions_cea); i++)
			if ((1 << resolutions_cea[i].index) & cea_mask)
				cli_debug("\t%2d %08x %4dx%4d@%d\n",
					resolutions_cea[i].index, 1 << resolutions_cea[i].index,
					resolutions_cea[i].hres, resolutions_cea[i].vres,
					resolutions_cea[i].fps);
	}
	if (vesa_mask) {
		cli_debug("VESA resolutions:");
		for (i = 0; i < SHL_ARRAY_LENGTH(resolutions_vesa); i++)
			if ((1 << resolutions_vesa[i].index) & vesa_mask)
				cli_debug("\t%2d %08x %4dx%4d@%d\n",
					resolutions_vesa[i].index, 1 << resolutions_vesa[i].index,
					resolutions_vesa[i].hres, resolutions_vesa[i].vres,
					resolutions_vesa[i].fps);
	}
	if (hh_mask) {
		cli_debug("HH resolutions:");
		for (i = 0; i < SHL_ARRAY_LENGTH(resolutions_hh); i++)
			if ((1 << resolutions_hh[i].index) & hh_mask)
				cli_debug("\t%2d %08x %4dx%4d@%d\n",
					resolutions_hh[i].index, 1 << resolutions_hh[i].index,
					resolutions_hh[i].hres, resolutions_hh[i].vres,
					resolutions_hh[i].fps);
	}
}

int vfd_get_cea_resolution(uint32_t mask, int *hres, int *vres)
{
	int i;

	if (!mask)
		return -EINVAL;

	for (i = SHL_ARRAY_LENGTH(resolutions_cea) - 1; i >= 0; --i) {
		if ((1 << resolutions_cea[i].index) & mask) {
			*vres = resolutions_cea[i].vres;
			*hres = resolutions_cea[i].hres;
			return 0;
		}
	}
	return -EINVAL;
}

int vfd_get_vesa_resolution(uint32_t mask, int *hres, int *vres)
{
	int i;

	if (!mask)
		return -EINVAL;

	for (i = SHL_ARRAY_LENGTH(resolutions_vesa) - 1; i >= 0; --i) {
		if ((1 << resolutions_vesa[i].index) & mask) {
			*vres = resolutions_vesa[i].vres;
			*hres = resolutions_vesa[i].hres;
			return 0;
		}
	}
	return -EINVAL;
}

int vfd_get_hh_resolution(uint32_t mask, int *hres, int *vres)
{
	int i;

	if (!mask)
		return -EINVAL;

	for (i = SHL_ARRAY_LENGTH(resolutions_hh); i >= 0; --i) {
		if ((1 << resolutions_hh[i].index) & mask) {
			*vres = resolutions_hh[i].vres;
			*hres = resolutions_hh[i].hres;
			return 0;
		}
	}
	return -EINVAL;
}

static int wfd_sube_parse_device_info(const char *in, union wfd_sube *out)
{
	int r = sscanf(in, "%4hx%4hx%4hx",
					&out->dev_info.dev_info,
					&out->dev_info.rtsp_port,
					&out->dev_info.max_throughput);

	return 3 == r ? 0 : -EINVAL;
}

static int wfd_sube_parse_audio_formats(const char *in, union wfd_sube *out)
{
	int r = sscanf(in, "%4x%1hhx%4x%1hhx%4x%1hhx",
					&out->audio_formats.lpcm_modes,
					&out->audio_formats.lpcm_dec_latency,
					&out->audio_formats.aac_modes,
					&out->audio_formats.aac_dec_latency,
					&out->audio_formats.ac3_modes,
					&out->audio_formats.ac3_dec_latency);

	return 6 == r ? 0 : -EINVAL;
}

static int wfd_sube_parse_video_formats(const char *in, union wfd_sube *out)
{
	int r = sscanf(in, "%4x%4x%4x%1hhx%1hhx%1hhx%1hhx%2hx%2hx%1hhx",
					&out->video_formats.cea,
					&out->video_formats.vesa,
					&out->video_formats.hh,
					&out->video_formats.native,
					&out->video_formats.profiles,
					&out->video_formats.levels,
					&out->video_formats.latency,
					&out->video_formats.min_slice_size,
					&out->video_formats.slice_enc_params,
					&out->video_formats.video_frame_rate_ctl);

	return 12 == r ? 0 : -EINVAL;
}

static int wfd_sube_parse_ext_caps(const char *in, union wfd_sube *out)
{
	int r = sscanf(in, "%2hx", &out->extended_caps.caps);

	return 1 == r ? 0 : -EINVAL;
}

int wfd_sube_parse(const char *in, union wfd_sube *out)
{
	uint8_t id;
	int r;

	r = sscanf(in, "%2hhx", &id);
	if(1 > r) {
		return -EINVAL;
	}

	return wfd_sube_parse_with_id(id, in + 2, out);
}

int wfd_sube_parse_with_id(enum wfd_sube_id id,
				const char *in,
				union wfd_sube *out)
{
	uint16_t len;
	union wfd_sube sube;
	int r;
   
	if(SHL_ARRAY_LENGTH(parser_tbl) <= id) {
		return -EINVAL;
	}

	r = sscanf(in, "%4hx", &len);
	if(1 > r) {
		return -EINVAL;
	}

	if(parser_tbl[id].len != len) {
		return -EINVAL;
	}

	if(!parser_tbl[id].parser) {
		return -ENOTSUP;
	}

	r = (*parser_tbl[id].parser)(in + 4, &sube);
	if(0 > r) {
		return r;
	}

	sube.id = id;

	if(out) {
		*out = sube;
	}

	return r;
}

int wfd_video_formats_from_string(const char *l,
				struct wfd_video_formats **out)
{
	_shl_free_ struct wfd_video_formats *f = NULL;
	uint8_t native, pref_disp_mode_sup;
	int r, i, n_codecs;
	const char *p;
	char max_hres[5], max_vres[5];

	assert(l);

	if(!strncmp("none", l, 4)) {
		if(out) {
			*out = NULL;
		}

		return 0;
	}

	r = sscanf(l, "%2hhx %2hhx", &native, &pref_disp_mode_sup);
	if(2 != r) {
		return -EINVAL;
	}

	l += 6;

	for(p = l, n_codecs = 1; (p = strchrnul(p, ','), *p); ++ n_codecs, ++ p);

	f = malloc(sizeof(*f) + (sizeof(f->h264_codecs[0]) * n_codecs));
	if(!f) {
		return -ENOMEM;
	}

	for(i = 0; i < n_codecs; i ++) {
		r = sscanf(l,
						"%2hhx %2hhx %8x %8x %8x %2hhx %4hx %4hx %2hhx %4s %4s",
						&f->h264_codecs[i].profile,
						&f->h264_codecs[i].level,
						&f->h264_codecs[i].cea_sup,
						&f->h264_codecs[i].vesa_sup,
						&f->h264_codecs[i].hh_sup,
						&f->h264_codecs[i].latency,
						&f->h264_codecs[i].min_slice_size,
						&f->h264_codecs[i].slice_enc_params,
						&f->h264_codecs[i].frame_rate_ctrl_sup,
						max_hres,
						max_vres);
		if(11 != r) {
			return -EINVAL;
		}

		errno = 0;

		f->h264_codecs[i].max_hres = !strncmp("none", max_hres, 4)
						? 0
						: strtoul(max_hres, NULL, 16);
		if(errno) {
			return -errno;
		}

		f->h264_codecs[i].max_vres = !strncmp("none", max_vres, 4)
						? 0
						: strtoul(max_vres, NULL, 16);
		if(errno) {
			return -errno;
		}

		l += 60;
	}

	f->native = native;
	f->pref_disp_mode_sup = pref_disp_mode_sup;
	f->n_h264_codecs = n_codecs;

	if(out) {
		*out = f;
		f = NULL;
	}

	return 0;
}

static inline const char * int16_to_res(int16_t v, char *b)
{
	if(!v) {
		return "none";
	}

	sprintf(b, "%04hX", v);

	return b;
}

int wfd_video_formats_to_string(struct wfd_video_formats *f, char **out)
{
	_shl_free_ char *s = NULL;
	char *p, b1[5], b2[5];
	size_t len = 6;
	int r, i;

	assert(f);
	assert(out);
	
	len += (f->n_h264_codecs ? f->n_h264_codecs * 60 : 6);
	p = s = malloc(len);
	if(!s) {
		return -ENOMEM;
	}

	r = snprintf(p, len, "%02hhX %02hhX ", f->native, f->pref_disp_mode_sup);
	if(0 > r) {
		return r;
	}

	p += r;
	len -= r;

	if(!f->n_h264_codecs) {
		strcat(p, " none");
		goto end;
	}

	for(i = 0; i < f->n_h264_codecs; ++ i) {
		r = snprintf(p, len,
						"%02hhX %02hhX %08X %08X %08X %02hhX %04hX %04hX %02hhX %s %s, ",
						f->h264_codecs[i].profile,
						f->h264_codecs[i].level,
						f->h264_codecs[i].cea_sup,
						f->h264_codecs[i].vesa_sup,
						f->h264_codecs[i].hh_sup,
						f->h264_codecs[i].latency,
						f->h264_codecs[i].min_slice_size,
						f->h264_codecs[i].slice_enc_params,
						f->h264_codecs[i].frame_rate_ctrl_sup,
						int16_to_res(f->h264_codecs[i].max_hres, b1),
						int16_to_res(f->h264_codecs[i].max_vres, b2));
		if(0 > r) {
			return r;
		}

		p += r;
		len -= r;
	}

	p[-2] = '\0';

end:
	*out = s;
	s = NULL;

	return 0;
}

int wfd_audio_format_from_string(const char *s, enum wfd_audio_format *f)
{
	enum wfd_audio_format t = WFD_AUDIO_FORMAT_UNKNOWN;
	if(s) {
		if(!strncmp("LPCM", s, 4)) {
			t = WFD_AUDIO_FORMAT_LPCM;
		}
		else if(!strncmp("AAC", s, 3)) {
			t = WFD_AUDIO_FORMAT_AAC;
		}
		else if(!strncmp("AC3", s, 3)) {
			t = WFD_AUDIO_FORMAT_AC3;
		}

		if(WFD_AUDIO_FORMAT_UNKNOWN != t) {
			if(f) {
				*f = t;
			}

			return 0;
		}
	}

	return -EINVAL;
}

const char * wfd_audio_format_to_string(enum wfd_audio_format f)
{
	switch(f) {
		case WFD_AUDIO_FORMAT_LPCM:
			return "LPCM";
		case WFD_AUDIO_FORMAT_AAC:
			return "AAC";
		case WFD_AUDIO_FORMAT_AC3:
			return "AC3";
		default:
			return NULL;
	}
}

int wfd_audio_codecs_from_string(const char *l,
			struct wfd_audio_codecs **out)
{
	_shl_free_ struct wfd_audio_codecs *c = NULL;
	_shl_free_ char *f = NULL;
	int r, i, n_caps;
	const char *p;

	assert(l);

	if(!strncmp("none", l, 4)) {
		if(out) {
			*out = NULL;
		}

		return 0;
	}

	for(p = l, n_caps = 1; (p = strchrnul(p, ','), *p); ++ n_caps, ++ p);

	c = malloc(sizeof(struct wfd_audio_codecs)
					+ (sizeof(c->caps[0]) * n_caps));

	for(i = 0; i < n_caps; i ++) {
		r = sscanf(l, "%ms %8x %2hhx",
						&f,
						&c->caps[i].modes,
						&c->caps[i].latency);
		if(r != 3) {
			return -EINVAL;
		}

		r = wfd_audio_format_from_string(f, &c->caps[i].format);
		if(0 > r) {
			return r;
		}

		l += 16;
		if(WFD_AUDIO_FORMAT_LPCM == c->caps[i].format) {
			++ l;
		}

		free(f);
		f = NULL;
	}

	c->n_caps = n_caps;

	if(out) {
		*out = c;
		c = NULL;
	}

	return 0;
}

int wfd_audio_codecs_to_string(struct wfd_audio_codecs *c, char **out)
{
	_shl_free_ char *s = NULL;
	char *p;
	int r, i;
	size_t len;

	assert(c);
	assert(out);

	len = c->n_caps * 18;
	p = s = malloc(len);
	if(!s) {
		return -ENOMEM;
	}

	for(i = 0; i < c->n_caps; i ++) {
		r = snprintf(p, len, "%s %08X %02hhX, ",
						wfd_audio_format_to_string(c->caps[i].format),
						c->caps[i].modes,
						c->caps[i].latency);
		if(0 > r) {
			return r;
		}

		p += r;
		len -= r;
	}

	p[-2] = '\n';
	*out = s;
	s = NULL;
	
	return 0;
}
