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

/*
 * Small DHCP Client/Server
 * Wifi-P2P requires us to use DHCP to set up a private P2P network. As all
 * DHCP daemons available have horrible interfaces for ad-hoc setups, we have
 * this small replacement for all DHCP operations. Once sd-dhcp is public, we
 * will switch to it instead of this helper binary. However, that also requires
 * DHCP-server support in sd-dhcp.
 *
 * This program implements a DHCP server and daemon. See --help for usage
 * information. We build on gdhcp from connman as the underlying DHCP protocol
 * implementation. To configure network devices, we actually invoke the "ip"
 * binary.
 *
 * Note that this is a gross hack! We don't intend to provide a fully functional
 * DHCP server or client here. This is only a replacement for the current lack
 * of Wifi-P2P support in common network managers. Once they gain proper
 * support, we will drop this helper!
 *
 * The "ip" invokation is quite fragile and ugly. However, performing these
 * steps directly involves netlink operations and more. As no-one came up with
 * patches, yet, we keep the hack. To anyone trying to fix it: Please, spend
 * this time hacking on NetworkManager, connman and friends instead! If they
 * gain Wifi-P2P support, this whole thing will get trashed.
 */

#define LOG_SUBSYSTEM "dhcp"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <glib.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include "gdhcp.h"
#include "shl_log.h"
#include "config.h"

static const char *arg_netdev;
static const char *arg_ip_binary = "/bin/ip";
static bool arg_server;
static char arg_local[INET_ADDRSTRLEN];
static char arg_gateway[INET_ADDRSTRLEN];
static char arg_dns[INET_ADDRSTRLEN];
static char arg_subnet[INET_ADDRSTRLEN];
static char arg_from[INET_ADDRSTRLEN];
static char arg_to[INET_ADDRSTRLEN];
static int arg_comm = -1;

struct manager {
	int ifindex;
	GMainLoop *loop;

	int sfd;
	GIOChannel *sfd_chan;
	guint sfd_id;

	GDHCPClient *client;
	char *client_addr;

	GDHCPServer *server;
	char *server_addr;
};

/*
 * We send prefixed messages via @comm. You should use a packet-based
 * socket-type so boundaries are preserved. Following packets are sent:
 *   sent on local lease:
 *     L:<addr>   # local iface addr
 *     S:<addr>   # subnet mask
 *     D:<addr>   # primary DNS server
 *     G:<addr>   # primary gateway
 *   sent on remote lease:
 *     R:<mac> <addr>   # addr given to remote device
 */
static void write_comm(const void *msg, size_t size)
{
	static bool warned;
	int r;

	if (arg_comm < 0)
		return;

	r = send(arg_comm, msg, size, MSG_NOSIGNAL);
	if (r < 0 && !warned) {
		warned = true;
		arg_comm = -1;
		log_error("cannot write to comm-socket, disabling it: %m");
	}
}

static void writef_comm(const void *format, ...)
{
	va_list args;
	char *msg;
	int r;

	va_start(args, format);
	r = vasprintf(&msg, format, args);
	va_end(args);
	if (r < 0)
		return log_vENOMEM();

	write_comm(msg, r);
	free(msg);
}

static int flush_if_addr(void)
{
	char *argv[64];
	int i, r;
	pid_t pid, rp;
	sigset_t mask;

	pid = fork();
	if (pid < 0) {
		return log_ERRNO();
	} else if (!pid) {
		/* child */

		sigemptyset(&mask);
		sigprocmask(SIG_SETMASK, &mask, NULL);

		/* redirect stdout to stderr */
		dup2(2, 1);

		i = 0;
		argv[i++] = (char*)arg_ip_binary;
		argv[i++] = "addr";
		argv[i++] = "flush";
		argv[i++] = "dev";
		argv[i++] = (char*)arg_netdev;
		argv[i] = NULL;

		execve(argv[0], argv, environ);
		_exit(1);
	}

	log_info("flushing local if-addr");
	rp = waitpid(pid, &r, 0);
	if (rp != pid) {
		log_error("cannot flush local if-addr via '%s'",
			  arg_ip_binary);
		return -EFAULT;
	} else if (!WIFEXITED(r)) {
		log_error("flushing local if-addr via '%s' failed",
			  arg_ip_binary);
		return -EFAULT;
	} else if (WEXITSTATUS(r)) {
		log_error("flushing local if-addr via '%s' failed with: %d",
			  arg_ip_binary, WEXITSTATUS(r));
		return -EFAULT;
	}

	log_debug("successfully flushed local if-addr via %s",
		  arg_ip_binary);

	return 0;
}

static int add_if_addr(const char *addr)
{
	char *argv[64];
	int i, r;
	pid_t pid, rp;
	sigset_t mask;

	pid = fork();
	if (pid < 0) {
		return log_ERRNO();
	} else if (!pid) {
		/* child */

		sigemptyset(&mask);
		sigprocmask(SIG_SETMASK, &mask, NULL);

		/* redirect stdout to stderr */
		dup2(2, 1);

		i = 0;
		argv[i++] = (char*)arg_ip_binary;
		argv[i++] = "addr";
		argv[i++] = "add";
		argv[i++] = (char*)addr;
		argv[i++] = "dev";
		argv[i++] = (char*)arg_netdev;
		argv[i] = NULL;

		execve(argv[0], argv, environ);
		_exit(1);
	}

	log_info("adding local if-addr %s", addr);
	rp = waitpid(pid, &r, 0);
	if (rp != pid) {
		log_error("cannot set local if-addr %s via '%s'",
			  addr, arg_ip_binary);
		return -EFAULT;
	} else if (!WIFEXITED(r)) {
		log_error("setting local if-addr %s via '%s' failed",
			  addr, arg_ip_binary);
		return -EFAULT;
	} else if (WEXITSTATUS(r)) {
		log_error("setting local if-addr %s via '%s' failed with: %d",
			  addr, arg_ip_binary, WEXITSTATUS(r));
		return -EFAULT;
	}

	log_debug("successfully set local if-addr %s via %s",
		  addr, arg_ip_binary);

	return 0;
}

int if_name_to_index(const char *name)
{
	struct ifreq ifr;
	int fd, r;

	if (strlen(name) > sizeof(ifr.ifr_name))
		return -EINVAL;

	fd = socket(PF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -errno;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, name, sizeof(ifr.ifr_name));

	r = ioctl(fd, SIOCGIFINDEX, &ifr);
	if (r < 0)
		r = -errno;
	else
		r = ifr.ifr_ifindex;

	close(fd);
	return r;
}

static void sig_dummy(int sig)
{
}

static void client_lease_fn(GDHCPClient *client, gpointer data)
{
	struct manager *m = data;
	char *addr = NULL, *a, *subnet = NULL, *gateway = NULL, *dns = NULL;
	GList *l;
	int r;

	log_info("lease available");

	addr = g_dhcp_client_get_address(client);
	log_info("lease: address: %s", addr);

	l = g_dhcp_client_get_option(client, G_DHCP_SUBNET);
	for ( ; l; l = l->next) {
		subnet = subnet ? : (char*)l->data;
		log_info("lease: subnet: %s", (char*)l->data);
	}

	l = g_dhcp_client_get_option(client, G_DHCP_DNS_SERVER);
	for ( ; l; l = l->next) {
		dns = dns ? : (char*)l->data;
		log_info("lease: dns-server: %s", (char*)l->data);
	}

	l = g_dhcp_client_get_option(client, G_DHCP_ROUTER);
	for ( ; l; l = l->next) {
		gateway = gateway ? : (char*)l->data;
		log_info("lease: router: %s", (char*)l->data);
	}

	if (!addr) {
		log_error("lease without IP address");
		goto error;
	}
	if (!subnet) {
		log_warning("lease without subnet mask, using 24");
		subnet = "24";
	}

	r = asprintf(&a, "%s/%s", addr, subnet);
	if (r < 0) {
		log_vENOMEM();
		goto error;
	}

	if (m->client_addr && !strcmp(m->client_addr, a)) {
		log_info("given address already set");
		free(a);
	} else {
		free(m->client_addr);
		m->client_addr = a;

		r = flush_if_addr();
		if (r < 0) {
			log_error("cannot flush addr on local interface %s",
				  arg_netdev);
			goto error;
		}

		r = add_if_addr(m->client_addr);
		if (r < 0) {
			log_error("cannot set parameters on local interface %s",
				  arg_netdev);
			goto error;
		}

		writef_comm("L:%s", addr);
		writef_comm("S:%s", subnet);
		if (dns)
			writef_comm("D:%s", dns);
		if (gateway)
			writef_comm("G:%s", gateway);
	}

	g_free(addr);
	return;

error:
	g_free(addr);
	g_main_loop_quit(m->loop);
}

static void client_no_lease_fn(GDHCPClient *client, gpointer data)
{
	struct manager *m = data;

	log_error("no lease available");
	g_main_loop_quit(m->loop);
}

static void server_log_fn(const char *str, void *data)
{
	log_format(NULL, 0, NULL, "gdhcp", LOG_DEBUG, "%s", str);
}

static void server_event_fn(const char *mac, const char *lease, void *data)
{
	log_debug("remote lease: %s %s", mac, lease);
	writef_comm("R:%s %s", mac, lease);
}

static gboolean manager_signal_fn(GIOChannel *chan, GIOCondition mask,
				  gpointer data)
{
	struct manager *m = data;
	ssize_t l;
	struct signalfd_siginfo info;

	if (mask & (G_IO_HUP | G_IO_ERR)) {
		log_vEPIPE();
		g_main_loop_quit(m->loop);
		return FALSE;
	}

	l = read(m->sfd, &info, sizeof(info));
	if (l < 0) {
		log_vERRNO();
		g_main_loop_quit(m->loop);
		return FALSE;
	} else if (l != sizeof(info)) {
		log_vEFAULT();
		return TRUE;
	}

	log_notice("received signal %d: %s",
		   info.ssi_signo, strsignal(info.ssi_signo));

	g_main_loop_quit(m->loop);
	return FALSE;
}

static void manager_free(struct manager *m)
{
	if (!m)
		return;

	if (!arg_server) {
		if (m->client) {
			g_dhcp_client_stop(m->client);

			if (m->client_addr) {
				flush_if_addr();
				free(m->client_addr);
			}

			g_dhcp_client_unref(m->client);
		}
	} else {
		if (m->server) {
			g_dhcp_server_stop(m->server);

			g_dhcp_server_unref(m->server);
		}

		if (m->server_addr) {
			flush_if_addr();
			free(m->server_addr);
		}
	}

	if (m->sfd >= 0) {
		g_source_remove(m->sfd_id);
		g_io_channel_unref(m->sfd_chan);
		close(m->sfd);
	}

	if (m->loop)
		g_main_loop_unref(m->loop);

	free(m);
}

static int manager_new(struct manager **out)
{
	static const int sigs[] = {
		SIGINT,
		SIGTERM,
		SIGQUIT,
		SIGHUP,
		SIGPIPE,
		0
	};
	int r, i;
	sigset_t mask;
	struct sigaction sig;
	GDHCPClientError cerr;
	GDHCPServerError serr;
	struct manager *m;

	m = calloc(1, sizeof(*m));
	if (!m)
		return log_ENOMEM();

	m->sfd = -1;

	if (geteuid())
		log_warning("not running as uid=0, dhcp might not work");

	m->ifindex = if_name_to_index(arg_netdev);
	if (m->ifindex < 0) {
		r = -EINVAL;
		log_error("cannot find interface %s (%d)",
			  arg_netdev, m->ifindex);
		goto error;
	}

	m->loop = g_main_loop_new(NULL, FALSE);

	sigemptyset(&mask);
	memset(&sig, 0, sizeof(sig));
	sig.sa_handler = sig_dummy;
	sig.sa_flags = SA_RESTART;

	for (i = 0; sigs[i]; ++i) {
		sigaddset(&mask, sigs[i]);
		r = sigaction(sigs[i], &sig, NULL);
		if (r < 0) {
			r = log_ERRNO();
			goto error;
		}
	}

	r = sigprocmask(SIG_BLOCK, &mask, NULL);
	if (r < 0) {
		r = log_ERRNO();
		goto error;
	}

	m->sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
	if (m->sfd < 0) {
		r = log_ERRNO();
		goto error;
	}

	m->sfd_chan = g_io_channel_unix_new(m->sfd);
	m->sfd_id = g_io_add_watch(m->sfd_chan,
				   G_IO_HUP | G_IO_ERR | G_IO_IN,
				   manager_signal_fn,
				   m);

	if (!arg_server) {
		m->client = g_dhcp_client_new(G_DHCP_IPV4, m->ifindex,
					      &cerr);
		if (!m->client) {
			r = -EINVAL;

			switch (cerr) {
			case G_DHCP_CLIENT_ERROR_INTERFACE_UNAVAILABLE:
				log_error("cannot create GDHCP client: interface %s unavailable",
					  arg_netdev);
				break;
			case G_DHCP_CLIENT_ERROR_INTERFACE_IN_USE:
				log_error("cannot create GDHCP client: interface %s in use",
					  arg_netdev);
				break;
			case G_DHCP_CLIENT_ERROR_INTERFACE_DOWN:
				log_error("cannot create GDHCP client: interface %s down",
					  arg_netdev);
				break;
			case G_DHCP_CLIENT_ERROR_NOMEM:
				r = log_ENOMEM();
				break;
			case G_DHCP_CLIENT_ERROR_INVALID_INDEX:
				log_error("cannot create GDHCP client: invalid interface %s",
					  arg_netdev);
				break;
			case G_DHCP_CLIENT_ERROR_INVALID_OPTION:
				log_error("cannot create GDHCP client: invalid options");
				break;
			default:
				log_error("cannot create GDHCP client (%d)",
					  cerr);
				break;
			}

			goto error;
		}

		g_dhcp_client_set_send(m->client, G_DHCP_HOST_NAME,
				       "<hostname>");

		g_dhcp_client_set_request(m->client, G_DHCP_SUBNET);
		g_dhcp_client_set_request(m->client, G_DHCP_DNS_SERVER);
		g_dhcp_client_set_request(m->client, G_DHCP_ROUTER);

		g_dhcp_client_register_event(m->client,
					     G_DHCP_CLIENT_EVENT_LEASE_AVAILABLE,
					     client_lease_fn, m);
		g_dhcp_client_register_event(m->client,
					     G_DHCP_CLIENT_EVENT_NO_LEASE,
					     client_no_lease_fn, m);
	} else {
		r = asprintf(&m->server_addr, "%s/%s", arg_local, arg_subnet);
		if (r < 0) {
			r = log_ENOMEM();
			goto error;
		}

		r = flush_if_addr();
		if (r < 0) {
			log_error("cannot flush addr on local interface %s",
				  arg_netdev);
			goto error;
		}

		r = add_if_addr(m->server_addr);
		if (r < 0) {
			log_error("cannot set parameters on local interface %s",
				  arg_netdev);
			goto error;
		}

		m->server = g_dhcp_server_new(G_DHCP_IPV4, m->ifindex,
					      &serr, server_event_fn, m);
		if (!m->server) {
			r = -EINVAL;

			switch(serr) {
			case G_DHCP_SERVER_ERROR_INTERFACE_UNAVAILABLE:
				log_error("cannot create GDHCP server: interface %s unavailable",
					  arg_netdev);
				break;
			case G_DHCP_SERVER_ERROR_INTERFACE_IN_USE:
				log_error("cannot create GDHCP server: interface %s in use",
					  arg_netdev);
				break;
			case G_DHCP_SERVER_ERROR_INTERFACE_DOWN:
				log_error("cannot create GDHCP server: interface %s down",
					  arg_netdev);
				break;
			case G_DHCP_SERVER_ERROR_NOMEM:
				r = log_ENOMEM();
				break;
			case G_DHCP_SERVER_ERROR_INVALID_INDEX:
				log_error("cannot create GDHCP server: invalid interface %s",
					  arg_netdev);
				break;
			case G_DHCP_SERVER_ERROR_INVALID_OPTION:
				log_error("cannot create GDHCP server: invalid options");
				break;
			case G_DHCP_SERVER_ERROR_IP_ADDRESS_INVALID:
				log_error("cannot create GDHCP server: invalid ip address");
				break;
			default:
				log_error("cannot create GDHCP server (%d)",
					  serr);
				break;
			}

			goto error;
		}

		g_dhcp_server_set_debug(m->server, server_log_fn, NULL);
		g_dhcp_server_set_lease_time(m->server, 60 * 60);

		r = g_dhcp_server_set_option(m->server, G_DHCP_SUBNET,
					     arg_subnet);
		if (r != 0) {
			log_vERR(r);
			goto error;
		}

		r = g_dhcp_server_set_option(m->server, G_DHCP_ROUTER,
					     arg_gateway);
		if (r != 0) {
			log_vERR(r);
			goto error;
		}

		r = g_dhcp_server_set_option(m->server, G_DHCP_DNS_SERVER,
					     arg_dns);
		if (r != 0) {
			log_vERR(r);
			goto error;
		}

		r = g_dhcp_server_set_ip_range(m->server, arg_from, arg_to);
		if (r != 0) {
			log_vERR(r);
			goto error;
		}
	}

	*out = m;
	return 0;

error:
	manager_free(m);
	return r;
}

static int manager_run(struct manager *m)
{
	int r;

	if (!arg_server) {
		log_info("running dhcp client on %s via '%s'",
			 arg_netdev, arg_ip_binary);

		r = g_dhcp_client_start(m->client, NULL);
		if (r != 0) {
			log_error("cannot start DHCP client: %d", r);
			return -EFAULT;
		}
	} else {
		log_info("running dhcp server on %s via '%s'",
			 arg_netdev, arg_ip_binary);

		r = g_dhcp_server_start(m->server);
		if (r != 0) {
			log_error("cannot start DHCP server: %d", r);
			return -EFAULT;
		}

		writef_comm("L:%s", arg_local);
	}

	g_main_loop_run(m->loop);

	return 0;
}

static int make_address(char *buf, const char *prefix, const char *suffix,
			const char *name)
{
	int r;
	struct in_addr addr;

	if (!prefix)
		prefix = "192.168.77";

	r = snprintf(buf, INET_ADDRSTRLEN, "%s.%s", prefix, suffix);
	if (r >= INET_ADDRSTRLEN)
		goto error;

	r = inet_pton(AF_INET, buf, &addr);
	if (r != 1)
		goto error;

	inet_ntop(AF_INET, &addr, buf, INET_ADDRSTRLEN);
	buf[INET_ADDRSTRLEN - 1] = 0;
	return 0;

error:
	log_error("Invalid address --%s=%s.%s (prefix: %s suffix: %s)",
		  name, prefix, suffix, prefix, suffix);
	return -EINVAL;
}

static int make_subnet(char *buf, const char *subnet)
{
	int r;
	struct in_addr addr;

	r = inet_pton(AF_INET, subnet, &addr);
	if (r != 1)
		goto error;

	inet_ntop(AF_INET, &addr, buf, INET_ADDRSTRLEN);
	buf[INET_ADDRSTRLEN - 1] = 0;
	return 0;

error:
	log_error("Invalid address --subnet=%s", subnet);
	return -EINVAL;
}

static int help(void)
{
	printf("%s [OPTIONS...] ...\n\n"
	       "Ad-hoc IPv4 DHCP Server/Client.\n\n"
	       "  -h --help                 Show this help\n"
	       "     --version              Show package version\n"
	       "     --log-level <lvl>      Maximum level for log messages\n"
	       "     --log-time             Prefix log-messages with timestamp\n"
	       "\n"
	       "     --netdev <dev>         Network device to run on\n"
	       "     --ip-binary <path>     Path to 'ip' binary [default: /bin/ip]\n"
	       "     --comm-fd <int>        Comm-socket FD passed through execve()\n"
	       "\n"
	       "Server Options:\n"
	       "     --server               Run as DHCP server instead of client\n"
	       "     --prefix <net-prefix>  Network prefix [default: 192.168.77]\n"
	       "     --local <suffix>       Local address suffix [default: 1]\n"
	       "     --gateway <suffix>     Gateway suffix [default: 1]\n"
	       "     --dns <suffix>         DNS suffix [default: 1]\n"
	       "     --subnet <mask>        Subnet mask [default: 255.255.255.0]\n"
	       "     --from <suffix>        Start address [default: 100]\n"
	       "     --to <suffix>          End address [default: 199]\n"
	       , program_invocation_short_name);

	return 0;
}

static int parse_argv(int argc, char *argv[])
{
	enum {
		ARG_VERSION = 0x100,
		ARG_LOG_LEVEL,
		ARG_LOG_TIME,

		ARG_NETDEV,
		ARG_IP_BINARY,
		ARG_COMM_FD,

		ARG_SERVER,
		ARG_PREFIX,
		ARG_LOCAL,
		ARG_GATEWAY,
		ARG_DNS,
		ARG_SUBNET,
		ARG_FROM,
		ARG_TO,
	};
	static const struct option options[] = {
		{ "help",	no_argument,		NULL,	'h' },
		{ "version",	no_argument,		NULL,	ARG_VERSION },
		{ "log-level",	required_argument,	NULL,	ARG_LOG_LEVEL },
		{ "log-time",	no_argument,		NULL,	ARG_LOG_TIME },

		{ "netdev",	required_argument,	NULL,	ARG_NETDEV },
		{ "ip-binary",	required_argument,	NULL,	ARG_IP_BINARY },
		{ "comm-fd",	required_argument,	NULL,	ARG_COMM_FD },

		{ "server",	no_argument,		NULL,	ARG_SERVER },
		{ "prefix",	required_argument,	NULL,	ARG_PREFIX },
		{ "local",	required_argument,	NULL,	ARG_LOCAL },
		{ "gateway",	required_argument,	NULL,	ARG_GATEWAY },
		{ "dns",	required_argument,	NULL,	ARG_DNS },
		{ "subnet",	required_argument,	NULL,	ARG_SUBNET },
		{ "from",	required_argument,	NULL,	ARG_FROM },
		{ "to",		required_argument,	NULL,	ARG_TO },
		{}
	};
	int c, r;
	const char *prefix = NULL, *local = NULL, *gateway = NULL;
	const char *dns = NULL, *subnet = NULL, *from = NULL, *to = NULL;

	while ((c = getopt_long(argc, argv, "hs:", options, NULL)) >= 0) {
		switch (c) {
		case 'h':
			return help();
		case ARG_VERSION:
			puts(PACKAGE_STRING);
			return 0;
		case ARG_LOG_LEVEL:
			log_max_sev = log_parse_arg(optarg);
			break;
		case ARG_LOG_TIME:
			log_init_time();
			break;
		case ARG_NETDEV:
			arg_netdev = optarg;
			break;
		case ARG_IP_BINARY:
			arg_ip_binary = optarg;
			break;
		case ARG_COMM_FD:
			arg_comm = atoi(optarg);
			break;

		case ARG_SERVER:
			arg_server = true;
			break;
		case ARG_PREFIX:
			prefix = optarg;
			break;
		case ARG_LOCAL:
			local = optarg;
			break;
		case ARG_GATEWAY:
			gateway = optarg;
			break;
		case ARG_DNS:
			dns = optarg;
			break;
		case ARG_SUBNET:
			subnet = optarg;
			break;
		case ARG_FROM:
			from = optarg;
			break;
		case ARG_TO:
			to = optarg;
			break;
		case '?':
			return -EINVAL;
		}
	}

	if (optind < argc) {
		log_error("unparsed remaining arguments starting with: %s",
			  argv[optind]);
		return -EINVAL;
	}

	if (!arg_netdev) {
		log_error("no network-device given (see --help for --netdev)");
		return -EINVAL;
	}

	if (access(arg_ip_binary, X_OK) < 0) {
		log_error("execution of ip-binary (%s) not allowed: %m",
			  arg_ip_binary);
		return -EINVAL;
	}

	if (!arg_server) {
		if (prefix || local || gateway ||
		    dns || subnet || from || to) {
			log_error("server option given, but running as client");
			return -EINVAL;
		}
	} else {
		r = make_address(arg_local, prefix, local ? : "1", "local");
		if (r < 0)
			return -EINVAL;
		r = make_address(arg_gateway, prefix, gateway ? : "1",
				 "gateway");
		if (r < 0)
			return -EINVAL;
		r = make_address(arg_dns, prefix, dns ? : "1", "dns");
		if (r < 0)
			return -EINVAL;
		r = make_subnet(arg_subnet, subnet ? : "255.255.255.0");
		if (r < 0)
			return -EINVAL;
		r = make_address(arg_from, prefix, from ? : "100", "from");
		if (r < 0)
			return -EINVAL;
		r = make_address(arg_to, prefix, to ? : "199", "to");
		if (r < 0)
			return -EINVAL;
	}

	log_format(LOG_DEFAULT_BASE, NULL, LOG_INFO,
		   "miracle-dhcp - revision %s %s %s",
		   "1.0", __DATE__, __TIME__);

	return 1;
}

int main(int argc, char **argv)
{
	struct manager *m = NULL;
	int r;

	r = parse_argv(argc, argv);
	if (r < 0)
		return EXIT_FAILURE;
	if (!r)
		return EXIT_SUCCESS;

	r = manager_new(&m);
	if (r < 0)
		goto finish;

	r = manager_run(m);

finish:
	manager_free(m);

	log_debug("exiting..");
	return abs(r);
}
