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
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <netinet/if_ether.h>

#include <arpa/inet.h>

#include <glib.h>
#include "ipv4ll.h"

/**
 * Return a random link local IP (in host byte order)
 */
uint32_t ipv4ll_random_ip(int seed)
{
	unsigned tmp;

	if (seed)
		srand(seed);
	else {
		struct timeval tv;
		gettimeofday(&tv, NULL);
		srand(tv.tv_usec);
	}
	do {
		tmp = rand();
		tmp = tmp & IN_CLASSB_HOST;
	} while (tmp > (IN_CLASSB_HOST - 0x0200));
	return ((LINKLOCAL_ADDR + 0x0100) + tmp);
}

/**
 * Return a random delay in range of zero to secs*1000
 */
guint ipv4ll_random_delay_ms(guint secs)
{
	struct timeval tv;
	guint tmp;

	gettimeofday(&tv, NULL);
	srand(tv.tv_usec);
	tmp = rand();
	return tmp % (secs * 1000);
}

int ipv4ll_send_arp_packet(uint8_t* source_eth, uint32_t source_ip,
		    uint32_t target_ip, int ifindex)
{
	struct sockaddr_ll dest;
	struct ether_arp p;
	uint32_t ip_source;
	uint32_t ip_target;
	int fd, n;

	fd = socket(PF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC, htons(ETH_P_ARP));
	if (fd < 0)
		return -errno;

	memset(&dest, 0, sizeof(dest));
	memset(&p, 0, sizeof(p));

	dest.sll_family = AF_PACKET;
	dest.sll_protocol = htons(ETH_P_ARP);
	dest.sll_ifindex = ifindex;
	dest.sll_halen = ETH_ALEN;
	memset(dest.sll_addr, 0xFF, ETH_ALEN);
	if (bind(fd, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
		close(fd);
		return -errno;
	}

	ip_source = htonl(source_ip);
	ip_target = htonl(target_ip);
	p.arp_hrd = htons(ARPHRD_ETHER);
	p.arp_pro = htons(ETHERTYPE_IP);
	p.arp_hln = ETH_ALEN;
	p.arp_pln = 4;
	p.arp_op = htons(ARPOP_REQUEST);

	memcpy(&p.arp_sha, source_eth, ETH_ALEN);
	memcpy(&p.arp_spa, &ip_source, sizeof(p.arp_spa));
	memcpy(&p.arp_tpa, &ip_target, sizeof(p.arp_tpa));

	n = sendto(fd, &p, sizeof(p), 0,
	       (struct sockaddr*) &dest, sizeof(dest));
	if (n < 0)
		n = -errno;

	close(fd);

	return n;
}

int ipv4ll_arp_socket(int ifindex)
{
	int fd;
	struct sockaddr_ll sock;

	fd = socket(PF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC, htons(ETH_P_ARP));
	if (fd < 0)
		return fd;

	memset(&sock, 0, sizeof(sock));

	sock.sll_family = AF_PACKET;
	sock.sll_protocol = htons(ETH_P_ARP);
	sock.sll_ifindex = ifindex;

	if (bind(fd, (struct sockaddr *) &sock, sizeof(sock)) != 0) {
		close(fd);
		return -errno;
	}

	return fd;
}
