/*
 *
 *  IPV4 Local Link library with GLib integration
 *
 *  Copyright (C) 2009-2010  Aldebaran Robotics. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef __G_IPV4LL_H
#define __G_IPV4LL_H

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 169.254.0.0 */
#define LINKLOCAL_ADDR 0xa9fe0000

/* See RFC 3927 */
#define PROBE_WAIT	     1
#define PROBE_NUM	     3
#define PROBE_MIN	     1
#define PROBE_MAX	     2
#define ANNOUNCE_WAIT	     2
#define ANNOUNCE_NUM	     2
#define ANNOUNCE_INTERVAL    2
#define MAX_CONFLICTS	    10
#define RATE_LIMIT_INTERVAL 60
#define DEFEND_INTERVAL	    10

uint32_t ipv4ll_random_ip(int seed);
guint ipv4ll_random_delay_ms(guint secs);
int ipv4ll_send_arp_packet(uint8_t* source_eth, uint32_t source_ip,
		    uint32_t target_ip, int ifindex);
int ipv4ll_arp_socket(int ifindex);

#ifdef __cplusplus
}
#endif
#endif	    /* !IPV4LL_H_ */
