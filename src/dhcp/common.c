/*
 *  DHCP library with GLib integration
 *
 *  Copyright (C) 2007-2013  Intel Corporation. All rights reserved.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <string.h>
#include <endian.h>
#include <net/if_arp.h>
#include <linux/if.h>
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "gdhcp.h"
#include "common.h"

static const DHCPOption client_options[] = {
	{ OPTION_IP,			0x01 }, /* subnet-mask */
	{ OPTION_IP | OPTION_LIST,	0x03 }, /* routers */
	{ OPTION_IP | OPTION_LIST,	0x06 }, /* domain-name-servers */
	{ OPTION_STRING,		0x0c }, /* hostname */
	{ OPTION_STRING,		0x0f }, /* domain-name */
	{ OPTION_IP | OPTION_LIST,	0x2a }, /* ntp-servers */
	{ OPTION_U32,			0x33 }, /* dhcp-lease-time */
	/* Options below will not be exposed to user */
	{ OPTION_IP,			0x32 }, /* requested-ip */
	{ OPTION_U8,			0x35 }, /* message-type */
	{ OPTION_U32,			0x36 }, /* server-id */
	{ OPTION_U16,			0x39 }, /* max-size */
	{ OPTION_STRING,		0x3c }, /* vendor */
	{ OPTION_STRING,		0x3d }, /* client-id */
	{ OPTION_STRING,		0xfc }, /* UNOFFICIAL proxy-pac */
	{ OPTION_UNKNOWN,		0x00 },
};

GDHCPOptionType dhcp_get_code_type(uint8_t code)
{
	int i;

	for (i = 0; client_options[i].code; i++) {
		if (client_options[i].code == code)
			return client_options[i].type;
	}

	return OPTION_UNKNOWN;
}

uint8_t *dhcp_get_option(struct dhcp_packet *packet, int code)
{
	int len, rem;
	uint8_t *optionptr;
	uint8_t overload = 0;

	/* option bytes: [code][len][data1][data2]..[dataLEN] */
	optionptr = packet->options;
	rem = sizeof(packet->options);

	while (1) {
		if (rem <= 0)
			/* Bad packet, malformed option field */
			return NULL;

		if (optionptr[OPT_CODE] == DHCP_PADDING) {
			rem--;
			optionptr++;

			continue;
		}

		if (optionptr[OPT_CODE] == DHCP_END) {
			if (overload & FILE_FIELD) {
				overload &= ~FILE_FIELD;

				optionptr = packet->file;
				rem = sizeof(packet->file);

				continue;
			} else if (overload & SNAME_FIELD) {
				overload &= ~SNAME_FIELD;

				optionptr = packet->sname;
				rem = sizeof(packet->sname);

				continue;
			}

			break;
		}

		len = 2 + optionptr[OPT_LEN];

		rem -= len;
		if (rem < 0)
			continue; /* complain and return NULL */

		if (optionptr[OPT_CODE] == code)
			return optionptr + OPT_DATA;

		if (optionptr[OPT_CODE] == DHCP_OPTION_OVERLOAD)
			overload |= optionptr[OPT_DATA];

		optionptr += len;
	}

	return NULL;
}

int dhcp_end_option(uint8_t *optionptr)
{
	int i = 0;

	while (optionptr[i] != DHCP_END) {
		if (optionptr[i] != DHCP_PADDING)
			i += optionptr[i + OPT_LEN] + OPT_DATA - 1;

		i++;
	}

	return i;
}

uint8_t *dhcpv6_get_option(struct dhcpv6_packet *packet, uint16_t pkt_len,
			int code, uint16_t *option_len, int *option_count)
{
	int rem, count = 0;
	uint8_t *optionptr, *found = NULL;
	uint16_t opt_code, opt_len, len;

	optionptr = packet->options;
	rem = pkt_len - 1 - 3;

	if (rem <= 0)
		goto bad_packet;

	while (1) {
		opt_code = optionptr[0] << 8 | optionptr[1];
		opt_len = len = optionptr[2] << 8 | optionptr[3];
		len += 2 + 2; /* skip code and len */

		if (len < 4)
			goto bad_packet;

		rem -= len;
		if (rem < 0)
			break;

		if (opt_code == code) {
			if (option_len)
				*option_len = opt_len;
			found = optionptr + 2 + 2;
			count++;
		}

		if (rem == 0)
			break;

		optionptr += len;
	}

	if (option_count)
		*option_count = count;

	return found;

bad_packet:
	if (option_len)
		*option_len = 0;
	if (option_count)
		*option_count = 0;
	return NULL;
}

uint8_t *dhcpv6_get_sub_option(unsigned char *option, uint16_t max_len,
			uint16_t *option_code, uint16_t *option_len)
{
	int rem;
	uint16_t code, len;

	rem = max_len - 2 - 2;

	if (rem <= 0)
		/* Bad option */
		return NULL;

	code = option[0] << 8 | option[1];
	len = option[2] << 8 | option[3];

	rem -= len;
	if (rem < 0)
		return NULL;

	*option_code = code;
	*option_len = len;

	return &option[4];
}

/*
 * Add an option (supplied in binary form) to the options.
 * Option format: [code][len][data1][data2]..[dataLEN]
 */
void dhcp_add_binary_option(struct dhcp_packet *packet, uint8_t *addopt)
{
	unsigned len;
	uint8_t *optionptr = packet->options;
	unsigned end = dhcp_end_option(optionptr);

	len = OPT_DATA + addopt[OPT_LEN];

	/* end position + (option code/length + addopt length) + end option */
	if (end + len + 1 >= DHCP_OPTIONS_BUFSIZE)
		/* option did not fit into the packet */
		return;

	memcpy(optionptr + end, addopt, len);

	optionptr[end + len] = DHCP_END;
}

/*
 * Add an option (supplied in binary form) to the options.
 * Option format: [code][len][data1][data2]..[dataLEN]
 */
void dhcpv6_add_binary_option(struct dhcpv6_packet *packet, uint16_t max_len,
				uint16_t *pkt_len, uint8_t *addopt)
{
	unsigned len;
	uint8_t *optionptr = packet->options;

	len = 2 + 2 + (addopt[2] << 8 | addopt[3]);

	/* end position + (option code/length + addopt length) */
	if (*pkt_len + len >= max_len)
		/* option did not fit into the packet */
		return;

	memcpy(optionptr + *pkt_len, addopt, len);
	*pkt_len += len;
}

static GDHCPOptionType check_option(uint8_t code, uint8_t data_len)
{
	GDHCPOptionType type = dhcp_get_code_type(code);
	uint8_t len;

	if (type == OPTION_UNKNOWN)
		return type;

	len = dhcp_option_lengths[type & OPTION_TYPE_MASK];
	if (len != data_len) {
		printf("Invalid option len %d (expecting %d) for code 0x%x\n",
			data_len, len, code);
		return OPTION_UNKNOWN;
	}

	return type;
}

void dhcp_add_option_uint32(struct dhcp_packet *packet, uint8_t code,
							uint32_t data)
{
	uint8_t option[6];

	if (check_option(code, sizeof(data)) == OPTION_UNKNOWN)
		return;

	option[OPT_CODE] = code;
	option[OPT_LEN] = sizeof(data);
	put_be32(data, option + OPT_DATA);

	dhcp_add_binary_option(packet, option);

	return;
}

void dhcp_add_option_uint16(struct dhcp_packet *packet, uint8_t code,
							uint16_t data)
{
	uint8_t option[6];

	if (check_option(code, sizeof(data)) == OPTION_UNKNOWN)
		return;

	option[OPT_CODE] = code;
	option[OPT_LEN] = sizeof(data);
	put_be16(data, option + OPT_DATA);

	dhcp_add_binary_option(packet, option);

	return;
}

void dhcp_add_option_uint8(struct dhcp_packet *packet, uint8_t code,
							uint8_t data)
{
	uint8_t option[6];

	if (check_option(code, sizeof(data)) == OPTION_UNKNOWN)
		return;

	option[OPT_CODE] = code;
	option[OPT_LEN] = sizeof(data);
	option[OPT_DATA] = data;

	dhcp_add_binary_option(packet, option);

	return;
}

void dhcp_init_header(struct dhcp_packet *packet, char type)
{
	memset(packet, 0, sizeof(*packet));

	packet->op = BOOTREQUEST;

	switch (type) {
	case DHCPOFFER:
	case DHCPACK:
	case DHCPNAK:
		packet->op = BOOTREPLY;
	}

	packet->htype = 1;
	packet->hlen = 6;
	packet->cookie = htonl(DHCP_MAGIC);
	packet->options[0] = DHCP_END;

	dhcp_add_option_uint8(packet, DHCP_MESSAGE_TYPE, type);
}

void dhcpv6_init_header(struct dhcpv6_packet *packet, uint8_t type)
{
	int id;

	memset(packet, 0, sizeof(*packet));

	packet->message = type;

	id = random();

	packet->transaction_id[0] = (id >> 16) & 0xff;
	packet->transaction_id[1] = (id >> 8) & 0xff;
	packet->transaction_id[2] = id & 0xff;
}

int dhcp_recv_l3_packet(struct dhcp_packet *packet, int fd)
{
	int n;

	memset(packet, 0, sizeof(*packet));

	n = read(fd, packet, sizeof(*packet));
	if (n < 0)
		return -errno;

	if (packet->cookie != htonl(DHCP_MAGIC))
		return -EPROTO;

	return n;
}

int dhcpv6_recv_l3_packet(struct dhcpv6_packet **packet, unsigned char *buf,
			int buf_len, int fd)
{
	int n;

	n = read(fd, buf, buf_len);
	if (n < 0)
		return -errno;

	*packet = (struct dhcpv6_packet *)buf;

	return n;
}

/* TODO: Use glib checksum */
uint16_t dhcp_checksum(void *addr, int count)
{
	/*
	 * Compute Internet Checksum for "count" bytes
	 * beginning at location "addr".
	 */
	int32_t sum = 0;
	uint16_t *source = (uint16_t *) addr;

	while (count > 1)  {
		/*  This is the inner loop */
		sum += *source++;
		count -= 2;
	}

	/*  Add left-over byte, if any */
	if (count > 0) {
		/* Make sure that the left-over byte is added correctly both
		 * with little and big endian hosts */
		uint16_t tmp = 0;
		*(uint8_t *) &tmp = *(uint8_t *) source;
		sum += tmp;
	}
	/*  Fold 32-bit sum to 16 bits */
	while (sum >> 16)
		sum = (sum & 0xffff) + (sum >> 16);

	return ~sum;
}

#define IN6ADDR_ALL_DHCP_RELAY_AGENTS_AND_SERVERS_MC_INIT \
	{ { { 0xff,0x02,0,0,0,0,0,0,0,0,0,0,0,0x1,0,0x2 } } } /* ff02::1:2 */
static const struct in6_addr in6addr_all_dhcp_relay_agents_and_servers_mc =
	IN6ADDR_ALL_DHCP_RELAY_AGENTS_AND_SERVERS_MC_INIT;

int dhcpv6_send_packet(int index, struct dhcpv6_packet *dhcp_pkt, int len)
{
	struct msghdr m;
	struct iovec v;
	struct in6_pktinfo *pktinfo;
	struct cmsghdr *cmsg;
	int fd, ret;
	struct sockaddr_in6 dst;
	void *control_buf;
	size_t control_buf_len;

	fd = socket(PF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
	if (fd < 0)
		return -errno;

	memset(&dst, 0, sizeof(dst));
	dst.sin6_family = AF_INET6;
	dst.sin6_port = htons(DHCPV6_SERVER_PORT);

	dst.sin6_addr = in6addr_all_dhcp_relay_agents_and_servers_mc;

	control_buf_len = CMSG_SPACE(sizeof(struct in6_pktinfo));
	control_buf = g_try_malloc0(control_buf_len);
	if (!control_buf) {
		close(fd);
		return -ENOMEM;
	}

	memset(&m, 0, sizeof(m));
	memset(&v, 0, sizeof(v));

	m.msg_name = &dst;
	m.msg_namelen = sizeof(dst);

	v.iov_base = (char *)dhcp_pkt;
	v.iov_len = len;
	m.msg_iov = &v;
	m.msg_iovlen = 1;

	m.msg_control = control_buf;
	m.msg_controllen = control_buf_len;
	cmsg = CMSG_FIRSTHDR(&m);
	cmsg->cmsg_level = IPPROTO_IPV6;
	cmsg->cmsg_type = IPV6_PKTINFO;
	cmsg->cmsg_len = CMSG_LEN(sizeof(*pktinfo));

	pktinfo = (struct in6_pktinfo *)CMSG_DATA(cmsg);
	memset(pktinfo, 0, sizeof(*pktinfo));
	pktinfo->ipi6_ifindex = index;
	m.msg_controllen = cmsg->cmsg_len;

	ret = sendmsg(fd, &m, 0);
	if (ret < 0) {
		char *msg = "DHCPv6 msg send failed";

		if (errno == EADDRNOTAVAIL) {
			char *str = g_strdup_printf("%s (index %d)",
					msg, index);
			perror(str);
			g_free(str);
		} else
			perror(msg);
	}

	g_free(control_buf);
	close(fd);

	return ret;
}

int dhcp_send_raw_packet(struct dhcp_packet *dhcp_pkt,
		uint32_t source_ip, int source_port, uint32_t dest_ip,
			int dest_port, const uint8_t *dest_arp, int ifindex)
{
	struct sockaddr_ll dest;
	struct ip_udp_dhcp_packet packet;
	int fd, n;

	enum {
		IP_UPD_DHCP_SIZE = sizeof(struct ip_udp_dhcp_packet) -
						EXTEND_FOR_BUGGY_SERVERS,
		UPD_DHCP_SIZE = IP_UPD_DHCP_SIZE -
				offsetof(struct ip_udp_dhcp_packet, udp),
	};

	fd = socket(PF_PACKET, SOCK_DGRAM | SOCK_CLOEXEC, htons(ETH_P_IP));
	if (fd < 0)
		return -errno;

	dhcp_pkt->flags |= htons(BROADCAST_FLAG);

	memset(&dest, 0, sizeof(dest));
	memset(&packet, 0, sizeof(packet));
	packet.data = *dhcp_pkt;

	dest.sll_family = AF_PACKET;
	dest.sll_protocol = htons(ETH_P_IP);
	dest.sll_ifindex = ifindex;
	dest.sll_halen = 6;
	memcpy(dest.sll_addr, dest_arp, 6);
	if (bind(fd, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
		close(fd);
		return -errno;
	}

	packet.ip.protocol = IPPROTO_UDP;
	packet.ip.saddr = source_ip;
	packet.ip.daddr = dest_ip;
	packet.udp.source = htons(source_port);
	packet.udp.dest = htons(dest_port);
	/* size, excluding IP header: */
	packet.udp.len = htons(UPD_DHCP_SIZE);
	/* for UDP checksumming, ip.len is set to UDP packet len */
	packet.ip.tot_len = packet.udp.len;
	packet.udp.check = dhcp_checksum(&packet, IP_UPD_DHCP_SIZE);
	/* but for sending, it is set to IP packet len */
	packet.ip.tot_len = htons(IP_UPD_DHCP_SIZE);
	packet.ip.ihl = sizeof(packet.ip) >> 2;
	packet.ip.version = IPVERSION;
	packet.ip.ttl = IPDEFTTL;
	packet.ip.check = dhcp_checksum(&packet.ip, sizeof(packet.ip));

	/*
	 * Currently we send full-sized DHCP packets (zero padded).
	 * If you need to change this: last byte of the packet is
	 * packet.data.options[dhcp_end_option(packet.data.options)]
	 */
	n = sendto(fd, &packet, IP_UPD_DHCP_SIZE, 0,
			(struct sockaddr *) &dest, sizeof(dest));
	close(fd);

	if (n < 0)
		return -errno;

	return n;
}

int dhcp_send_kernel_packet(struct dhcp_packet *dhcp_pkt,
				uint32_t source_ip, int source_port,
				uint32_t dest_ip, int dest_port)
{
	struct sockaddr_in client;
	int fd, n, opt = 1;

	enum {
		DHCP_SIZE = sizeof(struct dhcp_packet) -
					EXTEND_FOR_BUGGY_SERVERS,
	};

	fd = socket(PF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
	if (fd < 0)
		return -errno;

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	memset(&client, 0, sizeof(client));
	client.sin_family = AF_INET;
	client.sin_port = htons(source_port);
	client.sin_addr.s_addr = htonl(source_ip);
	if (bind(fd, (struct sockaddr *) &client, sizeof(client)) < 0) {
		close(fd);
		return -errno;
	}

	memset(&client, 0, sizeof(client));
	client.sin_family = AF_INET;
	client.sin_port = htons(dest_port);
	client.sin_addr.s_addr = htonl(dest_ip);
	if (connect(fd, (struct sockaddr *) &client, sizeof(client)) < 0) {
		close(fd);
		return -errno;
	}

	n = write(fd, dhcp_pkt, DHCP_SIZE);

	close(fd);

	if (n < 0)
		return -errno;

	return n;
}

int dhcp_l3_socket(int port, const char *interface, int family)
{
	int fd, opt = 1, len;
	struct sockaddr_in addr4;
	struct sockaddr_in6 addr6;
	struct sockaddr *addr;

	fd = socket(family, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
	if (fd < 0)
		return -errno;

	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE,
				interface, strlen(interface) + 1) < 0) {
		close(fd);
		return -1;
	}

	if (family == AF_INET) {
		memset(&addr4, 0, sizeof(addr4));
		addr4.sin_family = family;
		addr4.sin_port = htons(port);
		addr = (struct sockaddr *)&addr4;
		len = sizeof(addr4);
	} else if (family == AF_INET6) {
		memset(&addr6, 0, sizeof(addr6));
		addr6.sin6_family = family;
		addr6.sin6_port = htons(port);
		addr = (struct sockaddr *)&addr6;
		len = sizeof(addr6);
	} else {
		close(fd);
		return -EINVAL;
	}

	if (bind(fd, addr, len) != 0) {
		close(fd);
		return -1;
	}

	return fd;
}

char *get_interface_name(int index)
{
	struct ifreq ifr;
	int sk, err;

	if (index < 0)
		return NULL;

	sk = socket(PF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (sk < 0) {
		perror("Open socket error");
		return NULL;
	}

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	err = ioctl(sk, SIOCGIFNAME, &ifr);
	if (err < 0) {
		perror("Get interface name error");
		close(sk);
		return NULL;
	}

	close(sk);

	return g_strdup(ifr.ifr_name);
}

bool interface_is_up(int index)
{
	int sk, err;
	struct ifreq ifr;
	bool ret = false;

	sk = socket(PF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (sk < 0) {
		perror("Open socket error");
		return false;
	}

	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_ifindex = index;

	err = ioctl(sk, SIOCGIFNAME, &ifr);
	if (err < 0) {
		perror("Get interface name error");
		goto done;
	}

	err = ioctl(sk, SIOCGIFFLAGS, &ifr);
	if (err < 0) {
		perror("Get interface flags error");
		goto done;
	}

	if (ifr.ifr_flags & IFF_UP)
		ret = true;

done:
	close(sk);

	return ret;
}
