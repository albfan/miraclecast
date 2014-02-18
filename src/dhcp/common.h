/*
 *
 *  DHCP client library with GLib integration
 *
 *  Copyright (C) 2009-2012  Intel Corporation. All rights reserved.
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

#include <netinet/udp.h>
#include <netinet/ip.h>

#include <glib.h>

#include "unaligned.h"
#include "gdhcp.h"

#define CLIENT_PORT 68
#define SERVER_PORT 67

#define DHCPV6_CLIENT_PORT 546
#define DHCPV6_SERVER_PORT 547
#define MAX_DHCPV6_PKT_SIZE 1500

#define EXTEND_FOR_BUGGY_SERVERS 80

static const uint8_t MAC_BCAST_ADDR[ETH_ALEN] __attribute__((aligned(2))) = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

static const uint8_t MAC_ANY_ADDR[ETH_ALEN] __attribute__((aligned(2))) = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/* DHCP packet */
#define DHCP_MAGIC              0x63825363
#define DHCP_OPTIONS_BUFSIZE    308
#define BOOTREQUEST             1
#define BOOTREPLY               2

#define BROADCAST_FLAG		0x8000

/* See RFC 2131 */
struct dhcp_packet {
	uint8_t op;
	uint8_t htype;
	uint8_t hlen;
	uint8_t hops;
	uint32_t xid;
	uint16_t secs;
	uint16_t flags;
	uint32_t ciaddr;
	uint32_t yiaddr;
	uint32_t siaddr_nip;
	uint32_t gateway_nip;
	uint8_t chaddr[16];
	uint8_t sname[64];
	uint8_t file[128];
	uint32_t cookie;
	uint8_t options[DHCP_OPTIONS_BUFSIZE + EXTEND_FOR_BUGGY_SERVERS];
} __attribute__((packed));

struct ip_udp_dhcp_packet {
	struct iphdr ip;
	struct udphdr udp;
	struct dhcp_packet data;
} __attribute__((packed));

/* See RFC 3315 */
struct dhcpv6_packet {
	uint8_t message;
	uint8_t transaction_id[3];
	uint8_t options[];
} __attribute__((packed));


/* See RFC 2132 */
#define DHCP_PADDING		0x00
#define DHCP_SUBNET		0x01
#define DHCP_ROUTER		0x03
#define DHCP_TIME_SERVER	0x04
#define DHCP_NAME_SERVER	0x05
#define DHCP_DNS_SERVER		0x06
#define DHCP_HOST_NAME		0x0c
#define DHCP_DOMAIN_NAME	0x0f
#define DHCP_NTP_SERVER		0x2a
#define DHCP_REQUESTED_IP	0x32
#define DHCP_LEASE_TIME		0x33
#define DHCP_OPTION_OVERLOAD	0x34
#define DHCP_MESSAGE_TYPE	0x35
#define DHCP_SERVER_ID		0x36
#define DHCP_PARAM_REQ		0x37
#define DHCP_ERR_MESSAGE	0x38
#define DHCP_MAX_SIZE		0x39
#define DHCP_VENDOR		0x3c
#define DHCP_CLIENT_ID		0x3d
#define DHCP_END		0xff

#define OPT_CODE		0
#define OPT_LEN			1
#define OPT_DATA		2
#define OPTION_FIELD		0
#define FILE_FIELD		1
#define SNAME_FIELD		2

/* DHCP_MESSAGE_TYPE values */
#define DHCPDISCOVER		1
#define DHCPOFFER		2
#define DHCPREQUEST		3
#define DHCPDECLINE		4
#define DHCPACK			5
#define DHCPNAK			6
#define DHCPRELEASE		7
#define DHCPINFORM		8
#define DHCP_MINTYPE DHCPDISCOVER
#define DHCP_MAXTYPE DHCPINFORM

/* Message types for DHCPv6, RFC 3315 sec 5.3 */
#define DHCPV6_SOLICIT		1
#define DHCPV6_ADVERTISE	2
#define DHCPV6_REQUEST		3
#define DHCPV6_CONFIRM		4
#define DHCPV6_RENEW		5
#define DHCPV6_REBIND		6
#define DHCPV6_REPLY		7
#define DHCPV6_RELEASE		8
#define DHCPV6_DECLINE		9
#define DHCPV6_RECONFIGURE	10
#define DHCPV6_INFORMATION_REQ	11

/*
 * DUID time starts 2000-01-01.
 */
#define DUID_TIME_EPOCH 946684800

typedef enum {
	OPTION_UNKNOWN,
	OPTION_IP,
	OPTION_STRING,
	OPTION_U8,
	OPTION_U16,
	OPTION_U32,
	OPTION_TYPE_MASK = 0x0f,
	OPTION_LIST = 0x10,
} GDHCPOptionType;

typedef struct dhcp_option {
	GDHCPOptionType type;
	uint8_t code;
} DHCPOption;

/* Length of the option types in binary form */
static const uint8_t dhcp_option_lengths[] = {
	[OPTION_IP]	= 4,
	[OPTION_STRING]	= 1,
	[OPTION_U8]	= 1,
	[OPTION_U16]	= 2,
	[OPTION_U32]	= 4,
};

uint8_t *dhcp_get_option(struct dhcp_packet *packet, int code);
uint8_t *dhcpv6_get_option(struct dhcpv6_packet *packet, uint16_t pkt_len,
			int code, uint16_t *option_len, int *option_count);
uint8_t *dhcpv6_get_sub_option(unsigned char *option, uint16_t max_len,
			uint16_t *code, uint16_t *option_len);
int dhcp_end_option(uint8_t *optionptr);
void dhcp_add_binary_option(struct dhcp_packet *packet, uint8_t *addopt);
void dhcpv6_add_binary_option(struct dhcpv6_packet *packet, uint16_t max_len,
				uint16_t *pkt_len, uint8_t *addopt);
void dhcp_add_option_uint8(struct dhcp_packet *packet,
				uint8_t code, uint8_t data);
void dhcp_add_option_uint16(struct dhcp_packet *packet,
				uint8_t code, uint16_t data);
void dhcp_add_option_uint32(struct dhcp_packet *packet,
				uint8_t code, uint32_t data);
GDHCPOptionType dhcp_get_code_type(uint8_t code);
GDHCPOptionType dhcpv6_get_code_type(uint16_t code);

uint16_t dhcp_checksum(void *addr, int count);

void dhcp_init_header(struct dhcp_packet *packet, char type);
void dhcpv6_init_header(struct dhcpv6_packet *packet, uint8_t type);

int dhcp_send_raw_packet(struct dhcp_packet *dhcp_pkt,
			uint32_t source_ip, int source_port,
			uint32_t dest_ip, int dest_port,
			const uint8_t *dest_arp, int ifindex);
int dhcpv6_send_packet(int index, struct dhcpv6_packet *dhcp_pkt, int len);
int dhcp_send_kernel_packet(struct dhcp_packet *dhcp_pkt,
			uint32_t source_ip, int source_port,
			uint32_t dest_ip, int dest_port);
int dhcp_l3_socket(int port, const char *interface, int family);
int dhcp_recv_l3_packet(struct dhcp_packet *packet, int fd);
int dhcpv6_recv_l3_packet(struct dhcpv6_packet **packet, unsigned char *buf,
			int buf_len, int fd);
int dhcp_l3_socket_send(int index, int port, int family);

char *get_interface_name(int index);
bool interface_is_up(int index);
