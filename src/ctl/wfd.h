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

void wfd_print_resolutions(char * prefix);
int vfd_get_cea_resolution(uint32_t mask, int *hres, int *vres);
int vfd_get_vesa_resolution(uint32_t mask, int *hres, int *vres);
int vfd_get_hh_resolution(uint32_t mask, int *hres, int *vres);

#endif /* WFD_H */
