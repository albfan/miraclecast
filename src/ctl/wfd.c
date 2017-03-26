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
#include "ctl.h"

struct resolution_bitmap {
	int index;
	int hres;
	int vres;
	int fps;
};

/*
 * CEA resolutions and refrash rate bitmap/index table
 * also used in native resolution field
 */
struct resolution_bitmap resolutions_cea[] = {
	{0,   640,  480, 60},	/* p60 */
	{1,   720,  480, 60},	/* p60 */
	{2,   720,  480, 60},	/* i60 */
	{3,   720,  576, 50},	/* p50 */
	{4,   720,  576, 50},	/* i50 */
	{5,  1280,  720, 30},	/* p30 */
	{6,  1280,  720, 60},	/* p60 */
	{7,  1920, 1080, 30},	/* p30 */
	{8,  1920, 1080, 60},	/* p60 */
	{9,  1920, 1080, 60},	/* i60 */
	{10, 1280,  720, 25},	/* p25 */
	{11, 1280,  720, 50},	/* p50 */
	{12, 1920, 1080, 25},	/* p25 */
	{13, 1920, 1080, 50},	/* p50 */
	{14, 1920, 1080, 50},	/* i50 */
	{15, 1280,  720, 24},	/* p24 */
	{16, 1920, 1080, 24},	/* p24 */
	{0, 0, 0, 0},
};

struct resolution_bitmap resolutions_vesa[] = {
	{0,   800,  600, 30},	/* p30 */
	{1,   800,  600, 60},	/* p60 */
	{2,  1024,  768, 30},	/* p30 */
	{3,  1024,  768, 60},	/* p60 */
	{4,  1152,  854, 30},	/* p30 */
	{5,  1152,  854, 60},	/* p60 */
	{6,  1280,  768, 30},	/* p30 */
	{7,  1280,  768, 60},	/* p60 */
	{8,  1280,  800, 30},	/* p30 */
	{9,  1280,  800, 60},	/* p60 */
	{10, 1360,  768, 30},	/* p30 */
	{11, 1360,  768, 60},	/* p60 */
	{12, 1366,  768, 30},	/* p30 */
	{13, 1366,  768, 60},	/* p60 */
	{14, 1280, 1024, 30},	/* p30 */
	{15, 1280, 1024, 60},	/* p60 */
	{16, 1440, 1050, 30},	/* p30 */
	{17, 1440, 1050, 60},	/* p60 */
	{18, 1440,  900, 30},	/* p30 */
	{19, 1440,  900, 60},	/* p60 */
	{20, 1600,  900, 30},	/* p30 */
	{21, 1600,  900, 60},	/* p60 */
	{22, 1600, 1200, 30},	/* p30 */
	{23, 1600, 1200, 60},	/* p60 */
	{24, 1680, 1024, 30},	/* p30 */
	{25, 1680, 1024, 60},	/* p60 */
	{26, 1680, 1050, 30},	/* p30 */
	{27, 1680, 1050, 60},	/* p60 */
	{28, 1920, 1200, 30},	/* p30 */
	{0, 0, 0, 0},
};

struct resolution_bitmap resolutions_hh[] = {
	{0,   800,  480, 30},	/* p30 */
	{1,   800,  480, 60},	/* p60 */
	{2,   854,  480, 30},	/* p30 */
	{3,   854,  480, 60},	/* p60 */
	{4,   864,  480, 30},	/* p30 */
	{5,   864,  480, 60},	/* p60 */
	{6,   640,  360, 30},	/* p30 */
	{7,   640,  360, 60},	/* p60 */
	{8,   960,  540, 30},	/* p30 */
	{9,   960,  540, 60},	/* p60 */
	{10,  848,  480, 30},	/* p30 */
	{11,  848,  480, 60},	/* p60 */
	{0, 0, 0, 0},
};

void wfd_print_resolutions(char * prefix)
{
	int i;

	printf("%sCEA resolutions:\n", prefix);
	for (i = 0; resolutions_cea[i].hres != 0; i++) {
		printf("%s\t%2d %08x %4dx%4d@%d\n", prefix,
			resolutions_cea[i].index, 1 << resolutions_cea[i].index,
			resolutions_cea[i].hres, resolutions_cea[i].vres,
			resolutions_cea[i].fps);
	}
	printf("%sVESA resolutions:\n", prefix);
	for (i = 0; resolutions_vesa[i].hres != 0; i++) {
		printf("%s\t%2d %08x %4dx%4d@%d\n", prefix,
			resolutions_vesa[i].index, 1 << resolutions_vesa[i].index,
			resolutions_vesa[i].hres, resolutions_vesa[i].vres,
			resolutions_vesa[i].fps);
	}
	printf("%sHH resolutions:\n", prefix);
	for (i = 0; resolutions_hh[i].hres != 0; i++) {
		printf("%s\t%2d %08x %4dx%4d@%d\n", prefix,
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
		for (i = 0; resolutions_cea[i].hres != 0; i++)
			if ((1 << resolutions_cea[i].index) & cea_mask)
				cli_debug("\t%2d %08x %4dx%4d@%d\n",
					resolutions_cea[i].index, 1 << resolutions_cea[i].index,
					resolutions_cea[i].hres, resolutions_cea[i].vres,
					resolutions_cea[i].fps);
	}
	if (vesa_mask) {
		cli_debug("VESA resolutions:");
		for (i = 0; resolutions_vesa[i].hres != 0; i++)
			if ((1 << resolutions_vesa[i].index) & vesa_mask)
				cli_debug("\t%2d %08x %4dx%4d@%d\n",
					resolutions_vesa[i].index, 1 << resolutions_vesa[i].index,
					resolutions_vesa[i].hres, resolutions_vesa[i].vres,
					resolutions_vesa[i].fps);
	}
	if (hh_mask) {
		cli_debug("HH resolutions:");
		for (i = 0; resolutions_hh[i].hres != 0; i++)
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

	for (i = 0; resolutions_cea[i].hres != 0; i++) {
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

	for (i = 0; resolutions_vesa[i].hres != 0; i++) {
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

	for (i = 0; resolutions_hh[i].hres != 0; i++) {
		if ((1 << resolutions_hh[i].index) & mask) {
			*vres = resolutions_hh[i].vres;
			*hres = resolutions_hh[i].hres;
			return 0;
		}
	}
	return -EINVAL;
}
