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

#define LOG_SUBSYSTEM "supplicant"

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <systemd/sd-event.h>

#ifdef ENABLE_SYSTEMD
#include <systemd/sd-journal.h>
#endif

#include <unistd.h>
#include "shl_dlist.h"
#include "shl_log.h"
#include "shl_util.h"
#include "util.h"
#include "wifid.h"
#include "wpas.h"

struct supplicant_group {
	unsigned long users;
	struct shl_dlist list;
	struct supplicant *s;
	struct supplicant_peer *sp;

	unsigned int subnet;
	char *ifname;
	char *local_addr;

	int dhcp_comm;
	sd_event_source *dhcp_comm_source;
	pid_t dhcp_pid;
	sd_event_source *dhcp_pid_source;

	bool go : 1;
};

struct supplicant_peer {
	struct peer *p;
	struct supplicant *s;		/* shortcut for p->l->s */
	struct supplicant_group *g;

	char *friendly_name;
	char *remote_addr;
	char *wfd_subelements;
	char *prov;
	char *pin;
	char *sta_mac;
};

struct supplicant {
	struct link *l;

	pid_t pid;
	sd_event_source *child_source;
	sd_event_source *timer_source;
	struct shl_ratelimit restart_rate;
	struct shl_ratelimit exec_rate;
	uint64_t open_cnt;
	char *conf_path;
	char *global_ctrl;
	char *dev_ctrl;

	struct wpas *bus_global;
	struct wpas *bus_dev;

	size_t setup_cnt;

	char *p2p_mac;
	struct shl_dlist groups;
	struct supplicant_peer *pending;

	bool running : 1;
	bool has_p2p : 1;
	bool has_wfd : 1;
	bool p2p_scanning : 1;
};

/* Device Password ID */
enum wps_dev_password_id {
	DEV_PW_DEFAULT = 0x0000,
	DEV_PW_USER_SPECIFIED = 0x0001,
	DEV_PW_MACHINE_SPECIFIED = 0x0002,
	DEV_PW_REKEY = 0x0003,
	DEV_PW_PUSHBUTTON = 0x0004,
	DEV_PW_REGISTRAR_SPECIFIED = 0x0005,
	DEV_PW_NFC_CONNECTION_HANDOVER = 0x0007
};

static void supplicant_failed(struct supplicant *s);
static void supplicant_peer_drop_group(struct supplicant_peer *sp);

static struct supplicant_peer *find_peer_by_p2p_mac(struct supplicant *s,
						    const char *p2p_mac)
{
	struct peer *p;

	p = link_find_peer(s->l, p2p_mac);
	if (p)
		return p->sp;

	return NULL;
}

static struct supplicant_peer *find_peer_by_any_mac(struct supplicant *s,
						    const char *mac)
{
	struct peer *p;

	LINK_FOREACH_PEER(p, s->l) {
		if (!strcmp(p->p2p_mac, mac) || (p->sp->sta_mac && !strcmp(p->sp->sta_mac, mac)))
			return p->sp;
	}

	return NULL;
}

static struct supplicant_group *find_group_by_ifname(struct supplicant *s,
						     const char *ifname)
{
	struct shl_dlist *i;
	struct supplicant_group *g;

	shl_dlist_for_each(i, &s->groups) {
		g = shl_dlist_entry(i, struct supplicant_group, list);
		if (!strcmp(g->ifname, ifname))
			return g;
	}

	return NULL;
}

/*
 * Supplicant Groups
 * The wpas daemon can create separate interfaces on-the-fly. Usually, they're
 * used for P2P operations, but others are possible, too. A supplicant_group
 * object is a shared dummy that is created once the iface appears and removed
 * once the last user drops it again.
 * As the initial iface is always created without users, we need some timer that
 * controls how long unused ifaces stay around. If the target device (or any
 * other peer) does not use the iface in a reasonable time, we simply destroy it
 * again.
 */

static void supplicant_group_free(struct supplicant_group *g)
{
	_wpas_message_unref_ struct wpas_message *m = NULL;
	struct peer *p;
	int r;

	if (!g)
		return;

	log_debug("free group %s", g->ifname);

	r = wpas_message_new_request(g->s->bus_global,
				     "P2P_GROUP_REMOVE",
				     &m);
	if (r >= 0) {
		r = wpas_message_append(m, "s", g->ifname);
		if (r >= 0) {
			r = wpas_call_async(g->s->bus_global,
					m, NULL, NULL, 0, NULL);
			if (r < 0)
				log_vERR(r);
		} else {
			log_vERR(r);
		}
	} else {
		log_vERR(r);
	}

	if (g->dhcp_pid > 0) {
		sd_event_source_unref(g->dhcp_pid_source);
		g->dhcp_pid_source = NULL;

		log_debug("killing DHCP-process pid:%d..",
			  g->dhcp_pid);
		r = kill(g->dhcp_pid, SIGTERM);
		if (r < 0)
			r = kill(g->dhcp_pid, SIGKILL);
		if (r < 0)
			log_warning("cannot kill DHCP-process pid:%d: %m",
				    (int)g->dhcp_pid);
		g->dhcp_pid = 0;
	}

	if (g->dhcp_comm >= 0) {
		sd_event_source_unref(g->dhcp_comm_source);
		g->dhcp_comm_source = NULL;
		close(g->dhcp_comm);
		g->dhcp_comm = -1;
	}

	LINK_FOREACH_PEER(p, g->s->l)
		if (p->sp->g == g)
			supplicant_peer_drop_group(p->sp);

	shl_dlist_unlink(&g->list);

	free(g->local_addr);
	free(g->ifname);
	free(g);
}

static int supplicant_group_comm_fn(sd_event_source *source,
				    int fd,
				    uint32_t mask,
				    void *data)
{
	struct supplicant_group *g = data;
	struct supplicant_peer *sp;
	struct peer *p;
	char buf[512], *t, *ip;
	ssize_t l;
	char mac[MAC_STRLEN];

	l = recv(fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
	if (l < 0) {
		l = -errno;
		if (l == -EAGAIN || l == -EINTR)
			return 0;

		log_vERRNO();
		goto error;
	} else if (!l) {
		log_error("HUP on dhcp-comm socket on %s", g->ifname);
		goto error;
	} else if (l > sizeof(buf) - 1) {
		l = sizeof(buf) - 1;
	}

	buf[l] = 0;
	log_debug("dhcp-comm-%s: %s", g->ifname, buf);

	/* we only parse "X:<addr>" right now */
	if (l < 3 || buf[1] != ':' || !buf[2])
		return 0;

	t = strdup(&buf[2]);
	if (!t) {
		log_vENOMEM();
		return 0;
	}

	switch (buf[0]) {
	case 'L':
		free(g->local_addr);
		g->local_addr = t;
		break;
	case 'G':
		if (g->sp) {
			free(g->sp->remote_addr);
			g->sp->remote_addr = t;
		} else {
			free(t);
		}
		break;
	case 'R':
		ip = strchr(t, ' ');
		if (!ip || ip == t || !ip[1]) {
			log_warning("invalid dhcp 'R' line: %s", t);
			free(t);
			break;
		}

		*ip++ = 0;
		reformat_mac(mac, t);
		sp = find_peer_by_any_mac(g->s, mac);
		if (sp) {
			ip = strdup(ip);
			if (!ip) {
				log_vENOMEM();
				free(t);
				break;
			}

			free(t);

			free(sp->remote_addr);
			sp->remote_addr = ip;
		} else {
			log_debug("ignore 'R' line for unknown mac");
			free(t);
		}

		break;
	default:
		free(t);
		break;
	}

	if (g->local_addr) {
		if (g->sp) {
			p = g->sp->p;
			if (p->sp->remote_addr)
				peer_supplicant_connected_changed(p, true);
		} else {
			LINK_FOREACH_PEER(p, g->s->l) {
				if (p->sp->g != g || !p->sp->remote_addr)
					continue;

				peer_supplicant_connected_changed(p, true);
			}
		}
	}

	return 0;

error:
	supplicant_group_free(g);
	return 0;
}

static int supplicant_group_pid_fn(sd_event_source *source,
				   const siginfo_t *info,
				   void *data)
{
	struct supplicant_group *g = data;

	log_error("DHCP client/server for %s died, stopping connection",
		  g->ifname);

	supplicant_group_free(g);

	return 0;
}

static int supplicant_group_spawn_dhcp_server(struct supplicant_group *g,
					      unsigned int subnet)
{
	char *argv[64], loglevel[64], commfd[64], prefix[64];
	char journal_id[128];
	int i, r, fds[2], fd_journal;
	pid_t pid;
	sigset_t mask;

	r = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds);
	if (r < 0)
		return log_ERRNO();

	pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return log_ERRNO();
	} else if (!pid) {
		/* child */

		close(fds[0]);
		sprintf(loglevel, "%u", log_max_sev);
		sprintf(commfd, "%d", fds[1]);
		sprintf(prefix, "192.168.%u", subnet);
		sigemptyset(&mask);
		sigprocmask(SIG_SETMASK, &mask, NULL);

#ifdef ENABLE_SYSTEMD
		/* redirect stdout/stderr to journal */
		sprintf(journal_id, "miracle-dhcp-%s", g->ifname);
		fd_journal = sd_journal_stream_fd(journal_id, LOG_INFO, false);
		if (fd_journal >= 0) {
			/* dup journal-fd to stdout and stderr */
			dup2(fd_journal, 1);
			dup2(fd_journal, 2);
		} else {
#endif
			/* no journal? redirect stdout to parent's stderr */
			dup2(2, 1);
#ifdef ENABLE_SYSTEMD
		}
#endif

		i = 0;
		argv[i++] = (char*) "miracle-dhcp";
		argv[i++] = "--server";
		argv[i++] = "--prefix";
		argv[i++] = prefix;
		argv[i++] = "--log-level";
		argv[i++] = loglevel;
		argv[i++] = "--netdev";
		argv[i++] = g->ifname;
		argv[i++] = "--comm-fd";
		argv[i++] = commfd;
		if (g->s->l->ip_binary) {
			argv[i++] = "--ip-binary";
			argv[i++] = g->s->l->ip_binary;
		}
		argv[i] = NULL;

		if (execvpe(argv[0], argv, environ)< 0) {
			log_error("dhcp failed (%d): %m", errno);
		}
		_exit(1);
	}

	close(fds[1]);
	g->dhcp_comm = fds[0];
	g->dhcp_pid = pid;

	return 0;
}

static int supplicant_group_spawn_dhcp_client(struct supplicant_group *g)
{
	char *argv[64], loglevel[64], commfd[64];
	char journal_id[128];
	int i, r, fds[2], fd_journal;
	pid_t pid;
	sigset_t mask;

	r = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fds);
	if (r < 0)
		return log_ERRNO();

	pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return log_ERRNO();
	} else if (!pid) {
		/* child */

		close(fds[0]);
		sprintf(loglevel, "%u", log_max_sev);
		sprintf(commfd, "%d", fds[1]);
		sigemptyset(&mask);
		sigprocmask(SIG_SETMASK, &mask, NULL);

#ifdef ENABLE_SYSTEMD
		/* redirect stdout/stderr to journal */
		sprintf(journal_id, "miracle-dhcp-%s", g->ifname);
		fd_journal = sd_journal_stream_fd(journal_id, LOG_INFO, false);
		if (fd_journal >= 0) {
			/* dup journal-fd to stdout and stderr */
			dup2(fd_journal, 1);
			dup2(fd_journal, 2);
		} else {
#endif
			/* no journal? redirect stdout to parent's stderr */
			dup2(2, 1);
#ifdef ENABLE_SYSTEMD
		}
#endif

		i = 0;
		argv[i++] = (char*) "miracle-dhcp";
		argv[i++] = "--log-level";
		argv[i++] = loglevel;
		argv[i++] = "--netdev";
		argv[i++] = g->ifname;
		argv[i++] = "--comm-fd";
		argv[i++] = commfd;
		if (g->s->l->ip_binary) {
			argv[i++] = "--ip-binary";
			argv[i++] = g->s->l->ip_binary;
		}
		argv[i] = NULL;

		if (execvpe(argv[0], argv, environ) < 0) {
			log_error("dhcp failed (%d): %m", errno);
		}
		_exit(1);
	}

	close(fds[1]);
	g->dhcp_comm = fds[0];
	g->dhcp_pid = pid;

	return 0;
}

static int supplicant_group_new(struct supplicant *s,
				struct supplicant_group **out,
				const char *ifname,
				bool go)
{
	struct supplicant_group *g, *j;
	struct shl_dlist *i;
	unsigned int subnet;
	int r;

	if (!s || !ifname)
		return log_EINVAL();

	log_debug("new group: %s", ifname);

	g = calloc(1, sizeof(*g));
	if (!g)
		return log_ENOMEM();

	g->s = s;
	g->go = go;
	g->dhcp_comm = -1;

	g->ifname = strdup(ifname);
	if (!g->ifname) {
		r = log_ENOMEM();
		goto error;
	}

	if (g->go) {
		/* find free subnet */
		for (subnet = 50; subnet < 256; ++subnet) {
			shl_dlist_for_each(i, &s->groups) {
				j = shl_dlist_entry(i,
						    struct supplicant_group,
						    list);
				if (j->subnet == subnet)
					break;
			}

			if (i == &s->groups) {
				g->subnet = subnet;
				break;
			}
		}

		if (g->subnet) {
			r = supplicant_group_spawn_dhcp_server(g, g->subnet);
		} else {
			log_warning("out of free subnets for local groups");
			r = -EINVAL;
		}
	} else {
		r = supplicant_group_spawn_dhcp_client(g);
	}
	if (r < 0)
		goto error;

	r = sd_event_add_io(s->l->m->event,
			    &g->dhcp_comm_source,
			    g->dhcp_comm,
			    EPOLLHUP | EPOLLERR | EPOLLIN,
			    supplicant_group_comm_fn,
			    g);
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	r = sd_event_add_child(s->l->m->event,
			       &g->dhcp_pid_source,
			       g->dhcp_pid,
			       WEXITED,
			       supplicant_group_pid_fn,
			       g);
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	shl_dlist_link(&s->groups, &g->list);
	if (out)
		*out = g;
	return 0;

error:
	supplicant_group_free(g);
	return r;
}

static void supplicant_group_keep(struct supplicant_group *g)
{
	if (!g)
		return;

	++g->users;
}

static void supplicant_group_drop(struct supplicant_group *g)
{
	if (!g || !g->users || --g->users)
		return;

	supplicant_group_free(g);
}

/*
 * Supplicant Peers
 * Wpas has a quite high-level P2P interface, which makes it impossible to deal
 * with P2P devices as stations, but instead requires us to handle them as
 * separate peers.
 * A supplicant_peer object represents a remote P2P peer. Each peer might have
 * multiple real stations, but the wpas API limits us to a single connection.
 * Therefore, we treat each peer as a whole and allow only one private
 * connection to it, regardless whether it's a local GO or client.
 */

static void supplicant_peer_set_group(struct supplicant_peer *sp,
				      struct supplicant_group *g)
{
	if (sp->g) {
		if (sp->g == g)
			return;

		supplicant_peer_drop_group(sp);
	}

	sp->g = g;
	supplicant_group_keep(g);
}

static void supplicant_peer_drop_group(struct supplicant_peer *sp)
{
	if (!sp->g)
		return;

	if (sp->g->sp == sp)
		sp->g = NULL;

	supplicant_group_drop(sp->g);
	sp->g = NULL;

	free(sp->remote_addr);
	sp->remote_addr = NULL;
	free(sp->sta_mac);
	sp->sta_mac = NULL;

	peer_supplicant_connected_changed(sp->p, false);
}

static int supplicant_peer_new(struct supplicant *s,
			       const char *p2p_mac,
			       struct supplicant_peer **out)
{
	struct supplicant_peer *sp;
	struct peer *p;
	int r;

	r = peer_new(s->l, p2p_mac, &p);
	if (r < 0) {
		log_error("cannot add new supplicant-peer for %s: %d",
			  p2p_mac, r);
		return r;
	}

	sp = calloc(1, sizeof(*sp));
	if (!sp) {
		r = log_ENOMEM();
		peer_free(p);
		return r;
	}

	sp->p = p;
	sp->s = s;
	p->sp = sp;

	*out = sp;
	return 0;
}

static void supplicant_peer_free(struct supplicant_peer *sp)
{
	if (!sp)
		return;

	if (sp->s->pending == sp) {
		sp->s->pending = NULL;
		peer_supplicant_formation_failure(sp->p, "lost");
	}

	supplicant_peer_drop_group(sp);
	peer_supplicant_stopped(sp->p);
	peer_free(sp->p);

	free(sp->sta_mac);
	free(sp->remote_addr);
	free(sp->pin);
	free(sp->prov);
	free(sp->friendly_name);
	free(sp->wfd_subelements);
	free(sp);
}

const char *supplicant_peer_get_friendly_name(struct supplicant_peer *sp)
{
	if (!sp)
		return NULL;

	return sp->friendly_name;
}

const char *supplicant_peer_get_interface(struct supplicant_peer *sp)
{
	if (!sp || !sp->g)
		return NULL;

	return sp->g->ifname;
}

const char *supplicant_peer_get_local_address(struct supplicant_peer *sp)
{
	if (!sp || !sp->g)
		return NULL;

	return sp->g->local_addr;
}

const char *supplicant_peer_get_remote_address(struct supplicant_peer *sp)
{
	if (!sp || !sp->g)
		return NULL;

	return sp->remote_addr;
}

const char *supplicant_peer_get_wfd_subelements(struct supplicant_peer *sp)
{
	if (!sp)
		return NULL;

	return sp->wfd_subelements;
}

int supplicant_peer_connect(struct supplicant_peer *sp,
			    const char *prov_type,
			    const char *pin)
{
	_wpas_message_unref_ struct wpas_message *m = NULL;
	int r;

	if (!sp || !sp->s->running)
		return log_EINVAL();
	if (sp->g)
		return 0;

	if (sp->s->pending && sp->s->pending != sp)
		return log_ERR(-EALREADY);

	if (!prov_type && !(prov_type = sp->prov))
		prov_type = "pbc";
	if (!pin)
		pin = sp->pin;

	log_debug("connect to %s via %s/%s", sp->p->p2p_mac, prov_type, pin);

	r = wpas_message_new_request(sp->s->bus_global,
				     "P2P_CONNECT",
				     &m);
	if (r < 0)
		return log_ERR(r);

	r = wpas_message_append(m, "s", sp->p->p2p_mac);
	if (r < 0)
		return log_ERR(r);

	if (!strcmp(prov_type, "pbc")) {
		r = wpas_message_append(m, "s", "pbc");
		if (r < 0)
			return log_ERR(r);
	} else if (!strcmp(prov_type, "display")) {
		if (!pin || !*pin)
			return -EINVAL;

		r = wpas_message_append(m, "ss", pin, "display");
		if (r < 0)
			return log_ERR(r);
	} else if (!strcmp(prov_type, "pin")) {
		if (!pin || !*pin)
			return -EINVAL;

		r = wpas_message_append(m, "ss", pin, "pin");
		if (r < 0)
			return log_ERR(r);
	} else if (!strcmp(prov_type, "keypad")) {
		if (!pin || !*pin)
			return -EINVAL;

		r = wpas_message_append(m, "ss", pin, "keypad");
		if (r < 0)
			return log_ERR(r);
	} else {
		return -EINVAL;
	}

	r = wpas_call_async(sp->s->bus_global, m, NULL, NULL, 0, NULL);
	if (r < 0)
		return log_ERR(r);

	sp->s->pending = sp;

	return 0;
}

void supplicant_peer_disconnect(struct supplicant_peer *sp)
{
	if (!sp)
		return;

	log_debug("disconnect from %s", sp->p->p2p_mac);

	/* clear cache even if not connected; can be used as custom reset */
	free(sp->pin);
	sp->pin = NULL;
	free(sp->prov);
	sp->prov = NULL;

	supplicant_peer_drop_group(sp);
}

/*
 * Supplicant Communication
 * Following are the core communication elements of the supplicant handling.
 * The supplicant core control handling starts the communication with
 * supplicant_started() and stops it with supplicant_stopped(). The
 * communication part thus does not have to deal with supplicant
 * process-execution, restarting and ctrl-iface establishment.
 *
 * The communication is highly asynchronous. Once we're started, we know that
 * we have a working comm-channel to wpas. We send initialization sequences to
 * wpas and once everything is set-up, we call into the link-layer to notify
 * them that the link is ready now.
 *
 * At any time, if there is a fatal error, we simply call supplicant_failed(),
 * which is part of the core supplicant control interface. It will tear down the
 * supplicant, stop our communication layer via supplicant_stopped() and then
 * schedule a restart.
 */

static void supplicant_parse_peer(struct supplicant *s,
				  struct wpas_message *m)
{
	struct supplicant_peer *sp;
	const char *mac, *name, *val;
	char *t;
	int r;

	r = wpas_message_read(m, "s", &mac);
	if (r < 0) {
		log_debug("no p2p-mac in P2P_PEER information: %s",
			  wpas_message_get_raw(m));
		return;
	}

	sp = find_peer_by_p2p_mac(s, mac);
	if (!sp) {
		r = supplicant_peer_new(s, mac, &sp);
		if (r < 0)
			return;
	}

	/* P2P-PEER reports the device name as 'device_name', P2P-DEVICE-FOUND
	 * uses 'name. Allow either here.. */
	r = wpas_message_dict_read(m, "device_name", 's', &name);
	if (r < 0)
		r = wpas_message_dict_read(m, "name", 's', &name);
	if (r >= 0) {
		t = strdup(name);
		if (!t) {
			log_vENOMEM();
		} else {
			free(sp->friendly_name);
			sp->friendly_name = t;
			peer_supplicant_friendly_name_changed(sp->p);
		}
	} else {
		log_debug("no device-name in P2P_PEER information: %s",
			  wpas_message_get_raw(m));
	}

	r = wpas_message_dict_read(m, "wfd_subelems", 's', &val);
	if (r >= 0) {
		t = strdup(val);
		if (!t) {
			log_vENOMEM();
		} else {
			free(sp->wfd_subelements);
			sp->wfd_subelements = t;
			peer_supplicant_wfd_subelements_changed(sp->p);
		}
	} else {
		/* TODO: wfd_dev_info only contains the dev-info sub-elem,
		 * while wfd_sublemens contains all. Fix that! The user has no
		 * chance to distinguish both.
		 * We currently use it only as boolean (set/unset) but once we
		 * parse it we _definitely_ have to provide proper data. */
		r = wpas_message_dict_read(m, "wfd_dev_info", 's', &val);
		if (r >= 0) {
			t = strdup(val);
			if (!t) {
				log_vENOMEM();
			} else {
				free(sp->wfd_subelements);
				sp->wfd_subelements = t;
				peer_supplicant_wfd_subelements_changed(sp->p);
			}
		}
	}

	if (s->running)
		peer_supplicant_started(sp->p);
}

static void supplicant_event_p2p_find_stopped(struct supplicant *s,
					      struct wpas_message *m)
{
	if (!s->p2p_scanning)
		return;

	log_debug("p2p-scanning stopped on %s", s->l->ifname);
	s->p2p_scanning = false;
	link_supplicant_p2p_scan_changed(s->l, false);
}

static int supplicant_p2p_peer_fn(struct wpas *w,
				  struct wpas_message *reply,
				  void *data)
{
	struct supplicant *s = data;

	if (wpas_message_is_fail(reply))
		return 0;

	supplicant_parse_peer(s, reply);
	return 0;
}

static void supplicant_event_p2p_device_found(struct supplicant *s,
					      struct wpas_message *ev)
{
	_wpas_message_unref_ struct wpas_message *m = NULL;
	const char *mac;
	int r;

	/*
	 * The P2P-DEVICE-FOUND event is quite small. Request a full
	 * peer-report.
	 */

	r = wpas_message_dict_read(ev, "p2p_dev_addr", 's', &mac);
	if (r < 0) {
		log_debug("no p2p_dev_addr in P2P-DEVICE-FOUND: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	supplicant_parse_peer(s, ev);

	r = wpas_message_new_request(s->bus_global,
				     "P2P_PEER",
				     &m);
	if (r < 0)
		goto error;

	r = wpas_message_append(m, "s", mac);
	if (r < 0)
		goto error;

	r = wpas_call_async(s->bus_global,
			    m,
			    supplicant_p2p_peer_fn,
			    s,
			    0,
			    NULL);
	if (r < 0)
		goto error;

	log_debug("requesting data for new peer %s", mac);
	return;

error:
	log_warning("cannot retrieve peer information from wpas for %s",
		    mac);
}

static void supplicant_event_p2p_device_lost(struct supplicant *s,
					     struct wpas_message *ev)
{
	struct supplicant_peer *sp;
	const char *mac;
	int r;

	r = wpas_message_dict_read(ev, "p2p_dev_addr", 's', &mac);
	if (r < 0) {
		log_debug("no p2p_dev_addr in P2P-DEVICE-LOST: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	sp = find_peer_by_p2p_mac(s, mac);
	if (sp) {
		log_debug("lost peer %s", mac);
		supplicant_peer_free(sp);
	} else {
		log_debug("stale P2P-DEVICE-LOST: %s",
			  wpas_message_get_raw(ev));
	}
}

static void supplicant_event_p2p_prov_disc_pbc_req(struct supplicant *s,
						   struct wpas_message *ev)
{
	struct supplicant_peer *sp;
	const char *mac;
	char *t;
	int r;

	r = wpas_message_dict_read(ev, "p2p_dev_addr", 's', &mac);
	if (r < 0) {
		log_debug("no p2p_dev_addr in P2P-PROV-DISC-PBC-REQ: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	sp = find_peer_by_p2p_mac(s, mac);
	if (!sp) {
		log_debug("stale P2P-PROV-DISC-PBC-REQ: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	t = strdup("pbc");
	if (!t)
		return log_vENOMEM();

	free(sp->prov);
	sp->prov = t;
	free(sp->pin);
	sp->pin = NULL;

	peer_supplicant_provision_discovery(sp->p, sp->prov, sp->pin);
}

static void supplicant_event_p2p_go_neg_request(struct supplicant *s,
					       struct wpas_message *ev)
{
	struct supplicant_peer *sp;
	const char *mac;
	int r;


	r = wpas_message_read(ev, "s", &mac);
	if (r < 0) {
		log_debug("no p2p-mac in P2P-GO-NEG-REQUEST information: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	sp = find_peer_by_p2p_mac(s, mac);
	if (!sp) {
		log_debug("stale P2P-GO-NEG-REQUEST: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	/*
	 * prov should be set by previous
	 * P2P-PROV-DISC-PBC-REQ
	 * P2P-PROV-DISC-SHOW-PIN
	 * or P2P-PROV-DISC-ENTER-PIN
	 * if not set pbc mode
	 */

	if (!sp->prov) {
		sp->prov = strdup("pbc");
		free(sp->pin);
		sp->pin = NULL;
	}

	if (!sp->g) {
		log_debug("GO Negotiation Request from %s", mac);
		peer_supplicant_go_neg_request(sp->p, sp->prov, sp->pin);
	} else {
		log_debug("GO Negotiation Request from already connected peer %s",
			  mac);
	}
}

static void supplicant_event_p2p_prov_disc_show_pin(struct supplicant *s,
						    struct wpas_message *ev)
{
	struct supplicant_peer *sp;
	const char *mac, *pin;
	char *t, *u;
	int r;

	r = wpas_message_dict_read(ev, "p2p_dev_addr", 's', &mac);
	if (r < 0) {
		log_debug("no p2p_dev_addr in P2P-PROV-DISC-SHOW-PIN: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	sp = find_peer_by_p2p_mac(s, mac);
	if (!sp) {
		log_debug("stale P2P-PROV-DISC-SHOW-PIN: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	r = wpas_message_argv_read(ev, 1, 's', &pin);
	if (r < 0) {
		log_debug("no pin given in P2P-PROV-DISC-SHOW-PIN: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	t = strdup("display");
	if (!t)
		return log_vENOMEM();

	u = strdup(pin);
	if (!u) {
		free(t);
		return log_vENOMEM();
	}

	free(sp->prov);
	sp->prov = t;
	free(sp->pin);
	sp->pin = u;

	peer_supplicant_provision_discovery(sp->p, sp->prov, sp->pin);
}

static void supplicant_event_p2p_prov_disc_enter_pin(struct supplicant *s,
						     struct wpas_message *ev)
{
	struct supplicant_peer *sp;
	const char *mac;
	char *t;
	int r;

	r = wpas_message_dict_read(ev, "p2p_dev_addr", 's', &mac);
	if (r < 0) {
		log_debug("no p2p_dev_addr in P2P-PROV-DISC-ENTER-PIN: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	sp = find_peer_by_p2p_mac(s, mac);
	if (!sp) {
		log_debug("stale P2P-PROV-DISC-ENTER-PIN: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	t = strdup("pin");
	if (!t)
		return log_vENOMEM();

	free(sp->prov);
	sp->prov = t;
	free(sp->pin);
	sp->pin = NULL;

	peer_supplicant_provision_discovery(sp->p, sp->prov, sp->pin);
}

static void supplicant_event_p2p_go_neg_success(struct supplicant *s,
						struct wpas_message *ev)
{
	struct supplicant_peer *sp;
	const char *mac, *sta;
	char *t;
	int r;

	r = wpas_message_dict_read(ev, "peer_dev", 's', &mac);
	if (r < 0) {
		log_debug("no peer_dev in P2P-GO-NEG-SUCCESS: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	sp = find_peer_by_p2p_mac(s, mac);
	if (!sp) {
		log_debug("stale P2P-GO-NEG-SUCCESS: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	if (sp->g) {
		log_debug("P2P-GO-NEG-SUCCESS on already connected peer: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	r = wpas_message_dict_read(ev, "peer_iface", 's', &sta);
	if (r < 0) {
		log_debug("no peer_iface in P2P-GO-NEG-SUCCESS: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	if (!sp->sta_mac || strcmp(sp->sta_mac, sta)) {
		t = strdup(sta);
		if (!t)
			return log_vENOMEM();

		log_debug("set STA-MAC for %s from %s to %s (via GO-NEG-SUCCESS)",
			  mac, sp->sta_mac ? : "<none>", sta);

		free(sp->sta_mac);
		sp->sta_mac = t;
	}
}

static void supplicant_event_p2p_group_started(struct supplicant *s,
					       struct wpas_message *ev)
{
	struct supplicant_peer *sp;
	struct supplicant_group *g;
	const char *mac, *ssid, *ifname, *go;
	bool is_go;
	int r;

	r = wpas_message_dict_read(ev, "go_dev_addr", 's', &mac);
	if (r < 0) {
		log_debug("no go_dev_addr in P2P-GROUP-STARTED: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	r = wpas_message_dict_read(ev, "ssid", 's', &ssid);
	if (r == 0) {
		log_debug("ssid: %s", ssid);
	}

	r = wpas_message_argv_read(ev, 0, 's', &ifname);
	if (r < 0) {
		log_debug("no ifname in P2P-GROUP-STARTED: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	r = wpas_message_argv_read(ev, 1, 's', &go);
	if (r < 0) {
		log_debug("no GO/client type in P2P-GROUP-STARTED: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	is_go = !strcmp(go, "GO");

	sp = find_peer_by_p2p_mac(s, mac);
	if (!sp) {
		if (!s->p2p_mac || strcmp(s->p2p_mac, mac)) {
			log_debug("stray P2P-GROUP-STARTED: %s",
				  wpas_message_get_raw(ev));
			return;
		}
	}

	g = find_group_by_ifname(s, ifname);
	if (!g) {
		r = supplicant_group_new(s, &g, ifname, is_go);
		if (r < 0)
			return;

		log_debug("start %s group on new group %s as %s/%d",
			  sp ? "remote" : "local", g->ifname, go, is_go);
	} else {
		log_debug("start %s group on existing group %s as %s/%d",
			  sp ? "remote" : "local", g->ifname, go, is_go);
	}

	if (sp) {
		supplicant_peer_set_group(sp, g);
		g->sp = sp;
	}

	/* TODO: For local-groups, we should schedule some timer so the
	 * group gets removed in case the remote side never connects. */
}

static void supplicant_event_p2p_group_removed(struct supplicant *s,
					       struct wpas_message *ev)
{
	struct supplicant_group *g;
	const char *ifname;
	int r;

	r = wpas_message_argv_read(ev, 0, 's', &ifname);
	if (r < 0) {
		log_debug("no ifname in P2P-GROUP-REMOVED: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	g = find_group_by_ifname(s, ifname);
	if (!g) {
		log_debug("stray P2P-GROUP-REMOVED: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	log_debug("remove group %s", ifname);
	supplicant_group_free(g);
}

static void supplicant_event_p2p_go_neg_failure(struct supplicant *s,
					       struct wpas_message *ev)
{
	struct peer *p;

	if (s->pending) {
		log_debug("peer %s group owner negotiation failed",
			  s->pending->friendly_name);
		p = s->pending->p;
		s->pending = NULL;
		peer_supplicant_formation_failure(p, "group owner negotiation failed");
	}
}

static void supplicant_event_p2p_group_formation_failure(struct supplicant *s,
					       struct wpas_message *ev)
{
	struct peer *p;

	/* There is no useful information in this event at all. Why would
	 * anyone want to know to which group formation (or even peer?) this
	 * event belongs to? No, we have to track all that ourselves, sigh.. */
	if (s->pending) {
		log_debug("peer %s connection failed",
			  s->pending->friendly_name);
		p = s->pending->p;
		s->pending = NULL;
		peer_supplicant_formation_failure(p, "unknown");
	}
}

static void supplicant_event_ap_sta_connected(struct supplicant *s,
					      struct wpas_message *ev)
{
	struct supplicant_peer *sp;
	struct supplicant_group *g;
	const char *sta_mac, *p2p_mac, *ifname;
	char *t;
	int r;

	r = wpas_message_dict_read(ev, "p2p_dev_addr", 's', &p2p_mac);
	if (r < 0) {
		log_debug("no p2p_dev_addr in AP-STA-CONNECTED: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	r = wpas_message_argv_read(ev, 0, 's', &sta_mac);
	if (r < 0) {
		log_debug("no station-mac in AP-STA-CONNECTED: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	sp = find_peer_by_p2p_mac(s, p2p_mac);
	if (!sp) {
		log_debug("stray AP-STA-CONNECTED: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	if (sp->g) {
		log_debug("AP-STA-CONNECTED for already connected peer: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	if (!sp->sta_mac || strcmp(sp->sta_mac, sta_mac)) {
		t = strdup(sta_mac);
		if (!t)
			return log_vENOMEM();

		log_debug("set STA-MAC for %s from %s to %s (via AP-STA-CONNECTED)",
			  p2p_mac, sp->sta_mac ? : "<none>", sta_mac);

		free(sp->sta_mac);
		sp->sta_mac = t;
	}

	ifname = wpas_message_get_ifname(ev);
	if (!ifname) {
		log_debug("no ifname in AP-STA-CONNECTED: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	g = find_group_by_ifname(s, ifname);
	if (!g) {
		log_debug("unknown ifname %s in AP-STA-CONNECTED: %s",
			  ifname, wpas_message_get_raw(ev));
		return;
	}

	log_debug("bind peer %s to existing local group %s", p2p_mac, ifname);
	supplicant_peer_set_group(sp, g);
}

static void supplicant_event_ap_sta_disconnected(struct supplicant *s,
						 struct wpas_message *ev)
{
	struct supplicant_peer *sp;
	const char *p2p_mac;
	int r;

	r = wpas_message_dict_read(ev, "p2p_dev_addr", 's', &p2p_mac);
	if (r < 0) {
		log_debug("no p2p_dev_addr in AP-STA-DISCONNECTED: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	sp = find_peer_by_p2p_mac(s, p2p_mac);
	if (!sp) {
		log_debug("stray AP-STA-DISCONNECTED: %s",
			  wpas_message_get_raw(ev));
		return;
	}

	if (sp->s->pending == sp) {
		sp->s->pending = NULL;
		if (sp->p->connected)
			peer_supplicant_connected_changed(sp->p, false);
		else
			peer_supplicant_formation_failure(sp->p, "disconnected");
	}

	log_debug("unbind peer %s from its group", p2p_mac);
	supplicant_peer_drop_group(sp);
}

static void supplicant_event(struct supplicant *s, struct wpas_message *m)
{
	const char *name;

	if (wpas_message_is_event(m, NULL)) {
		name = wpas_message_get_name(m);
		if (!name) {
			log_debug("unnamed wpas-event: %s",
				  wpas_message_get_raw(m));
			return;
		}

		/* ignored events */
		if (!strcmp(name, "CTRL-EVENT-SCAN-STARTED") ||
		    !strcmp(name, "CTRL-EVENT-SCAN-RESULTS") ||
		    !strcmp(name, "CTRL-EVENT-EAP-STARTED") ||
		    !strcmp(name, "CTRL-EVENT-EAP-PROPOSED-METHOD") ||
		    !strcmp(name, "CTRL-EVENT-EAP-FAILURE") ||
		    !strcmp(name, "CTRL-EVENT-BSS-REMOVED") ||
		    !strcmp(name, "CTRL-EVENT-BSS-ADDED") ||
		    !strcmp(name, "CTRL-EVENT-CONNECTED") ||
		    !strcmp(name, "CTRL-EVENT-DISCONNECTED") ||
		    !strcmp(name, "WPS-PBC-ACTIVE") ||
		    !strcmp(name, "WPS-PBC-DISABLE") ||
		    !strcmp(name, "WPS-AP-AVAILABLE-PBC") ||
		    !strcmp(name, "WPS-AP-AVAILABLE-AUTH") ||
		    !strcmp(name, "WPS-AP-AVAILABLE-PIN") ||
		    !strcmp(name, "CTRL-EVENT-EAP-STATUS") ||
		    !strcmp(name, "CTRL-EVENT-EAP-METHOD") ||
		    !strcmp(name, "WPS-CRED-RECEIVED") ||
		    !strcmp(name, "WPS-AP-AVAILABLE") ||
		    !strcmp(name, "WPS-REG-SUCCESS") ||
		    !strcmp(name, "WPS-SUCCESS") ||
		    !strcmp(name, "WPS-ENROLLEE-SEEN") ||
		    !strcmp(name, "P2P-GROUP-FORMATION-SUCCESS") ||
		    !strcmp(name, "AP-ENABLED") ||
		    !strcmp(name, "SME:") ||
		    !strcmp(name, "WPA:") ||
		    !strcmp(name, "Trying") ||
		    !strcmp(name, "No network configuration found for the current AP") ||
		    !strcmp(name, "Associated"))
			return;

		if (!strcmp(name, "P2P-FIND-STOPPED"))
			supplicant_event_p2p_find_stopped(s, m);
		else if (!strcmp(name, "P2P-DEVICE-FOUND"))
			supplicant_event_p2p_device_found(s, m);
		else if (!strcmp(name, "P2P-DEVICE-LOST"))
			supplicant_event_p2p_device_lost(s, m);
		else if (!strcmp(name, "P2P-PROV-DISC-PBC-REQ"))
			supplicant_event_p2p_prov_disc_pbc_req(s, m);
		else if (!strcmp(name, "P2P-PROV-DISC-SHOW-PIN"))
			supplicant_event_p2p_prov_disc_show_pin(s, m);
		else if (!strcmp(name, "P2P-PROV-DISC-ENTER-PIN"))
			supplicant_event_p2p_prov_disc_enter_pin(s, m);
		else if (!strcmp(name, "P2P-GO-NEG-SUCCESS"))
			supplicant_event_p2p_go_neg_success(s, m);
		else if (!strcmp(name, "P2P-GO-NEG-REQUEST"))
			supplicant_event_p2p_go_neg_request(s, m);
		else if (!strcmp(name, "P2P-GROUP-STARTED"))
			supplicant_event_p2p_group_started(s, m);
		else if (!strcmp(name, "P2P-GROUP-REMOVED"))
			supplicant_event_p2p_group_removed(s, m);
		else if (!strcmp(name, "P2P-GO-NEG-FAILURE"))
			supplicant_event_p2p_go_neg_failure(s, m);
		else if (!strcmp(name, "P2P-GROUP-FORMATION-FAILURE"))
			supplicant_event_p2p_group_formation_failure(s, m);
		else if (!strcmp(name, "AP-STA-CONNECTED"))
			supplicant_event_ap_sta_connected(s, m);
		else if (!strcmp(name, "AP-STA-DISCONNECTED"))
			supplicant_event_ap_sta_disconnected(s, m);
		else
			log_debug("unhandled wpas-event: %s",
				  wpas_message_get_raw(m));
	} else {
		log_debug("unhandled wpas-message: %s",
			  wpas_message_get_raw(m));
	}
}

static void supplicant_try_ready(struct supplicant *s)
{
	struct peer *p;

	if (s->running)
		return;

	/* if all setup-commands completed, notify link layer */
	if (s->setup_cnt > 0)
		return;

	if (!s->has_p2p)
		s->has_wfd = false;

	s->running = true;
	link_supplicant_started(s->l);

	LINK_FOREACH_PEER(p, s->l)
		peer_supplicant_started(p);
}

static int supplicant_p2p_set_disallow_freq_fn(struct wpas *w,
					       struct wpas_message *reply,
					       void *data)
{
	struct supplicant *s = data;

	/* P2P_SET disallow_freq received */
	--s->setup_cnt;

	if (!wpas_message_is_ok(reply))
		log_warning("cannot set p2p disallow_freq field");

	supplicant_try_ready(s);
	return 0;
}

static int supplicant_init_p2p_peer_fn(struct wpas *w,
				       struct wpas_message *reply,
				       void *data)
{
	_wpas_message_unref_ struct wpas_message *m = NULL;
	_shl_free_ char *next = NULL;
	struct supplicant *s = data;
	const char *mac;
	int r;

	/*
	 * Using P2P_PEER to get a list of initial peers is racy. If a peer
	 * exits while we iterate over the list, a "P2P_PEER NEXT-<addr>"
	 * command will fail. Furthermore, if we get a P2P-DEVICE-LOST event
	 * for a peer before we could add it, we will never handle the event.
	 * Blame whoever wrote that stupid wpas interface..
	 */

	/* got P2P_PEER response */
	--s->setup_cnt;

	/* FAIL means end-of-list */
	if (!wpas_message_is_fail(reply)) {
		r = wpas_message_read(reply, "s", &mac);
		if (r < 0) {
			log_vERR(r);
			goto error;
		}

		wpas_message_rewind(reply);
		supplicant_parse_peer(s, reply);

		r = wpas_message_new_request(s->bus_global,
					     "P2P_PEER",
					     &m);
		if (r < 0) {
			log_vERR(r);
			goto error;
		}

		next = shl_strcat("NEXT-", mac);
		if (!next) {
			r = log_ENOMEM();
			goto error;
		}

		r = wpas_message_append(m, "s", next);
		if (r < 0) {
			log_vERR(r);
			goto error;
		}

		r = wpas_call_async(s->bus_global,
				    m,
				    supplicant_init_p2p_peer_fn,
				    s,
				    0,
				    NULL);
		if (r < 0) {
			log_vERR(r);
			goto error;
		}

		/* require next P2P_PEER listing */
		++s->setup_cnt;
	}

	supplicant_try_ready(s);
	return 0;

error:
	log_warning("cannot read some initial P2P peers, ignoring");
	supplicant_try_ready(s);
	return 0;
}

static int supplicant_set_wifi_display_fn(struct wpas *w,
					  struct wpas_message *reply,
					  void *data)
{
	_wpas_message_unref_ struct wpas_message *m = NULL;
	struct supplicant *s = data;
	int r;

	/* SET received */
	--s->setup_cnt;

	if (!wpas_message_is_ok(reply)) {
		log_warning("cannot enable wpas wifi-display support");
		s->has_wfd = false;
	}

	if (s->has_wfd) {
		/* update subelements */

		r = wpas_message_new_request(s->bus_global,
					     "WFD_SUBELEM_SET",
					     &m);
		if (r < 0) {
			log_vERR(r);
			goto error;
		}

		r = wpas_message_append(m, "s", "0");
		if (r < 0) {
			log_vERR(r);
			goto error;
		}

		if (!shl_isempty(s->l->wfd_subelements)) {
			r = wpas_message_append(m, "s", s->l->wfd_subelements);
			if (r < 0) {
				log_vERR(r);
				goto error;
			}
		}

		r = wpas_call_async(s->bus_global,
				    m,
				    NULL,
				    NULL,
				    0,
				    NULL);
		if (r < 0) {
			log_vERR(r);
			goto error;
		}
	}

	supplicant_try_ready(s);
	return 0;

error:
	supplicant_failed(s);
	return 0;
}

static int supplicant_status_fn(struct wpas *w,
				struct wpas_message *reply,
				void *data)
{
	_wpas_message_unref_ struct wpas_message *m = NULL;
	struct supplicant *s = data;
	const char *p2p_state = NULL, *wifi_display = NULL, *p2p_mac = NULL;
	char *t;
	int r;

	/* STATUS received */
	--s->setup_cnt;

	wpas_message_dict_read(reply, "p2p_state", 's', &p2p_state);
	wpas_message_dict_read(reply, "wifi_display", 's', &wifi_display);
	wpas_message_dict_read(reply, "p2p_device_address", 's', &p2p_mac);

	if (!p2p_state) {
		log_warning("wpa_supplicant or driver does not support P2P");
	} else if (!strcmp(p2p_state, "DISABLED")) {
		log_warning("P2P support disabled on given interface");
	} else {
		s->has_p2p = true;

		r = wpas_message_new_request(s->bus_global,
					     "SET",
					     &m);
		if (r < 0) {
			log_vERR(r);
			goto error;
		}

		r = wpas_message_append(m, "ss",
					"device_name",
					s->l->friendly_name ? : "Miracle");
		if (r < 0) {
			log_vERR(r);
			goto error;
		}

		r = wpas_call_async(s->bus_global,
				    m,
				    NULL,
				    NULL,
				    0,
				    NULL);
		if (r < 0) {
			log_vERR(r);
			goto error;
		}

		/* require P2P_SET disallow_freq response */
		++s->setup_cnt;

		r = wpas_message_new_request(s->bus_global,
					     "P2P_SET",
					     &m);
		if (r < 0) {
			log_vERR(r);
			goto error;
		}

		r = wpas_message_append(m, "ss", "disallow_freq", "5180-5900");
		if (r < 0) {
			log_vERR(r);
			goto error;
		}

		r = wpas_call_async(s->bus_global,
				    m,
				    supplicant_p2p_set_disallow_freq_fn,
				    s,
				    0,
				    NULL);
		if (r < 0) {
			log_vERR(r);
			goto error;
		}

		/* require P2P_PEER listing */
		++s->setup_cnt;

		r = wpas_message_new_request(s->bus_global,
					     "P2P_PEER",
					     &m);
		if (r < 0) {
			log_vERR(r);
			goto error;
		}

		r = wpas_message_append(m, "s", "FIRST");
		if (r < 0) {
			log_vERR(r);
			goto error;
		}

		r = wpas_call_async(s->bus_global,
				    m,
				    supplicant_init_p2p_peer_fn,
				    s,
				    0,
				    NULL);
		if (r < 0) {
			log_vERR(r);
			goto error;
		}
	}

	if (!wifi_display) {
		log_warning("wpa_supplicant does not support wifi-display");
	} else if (s->has_p2p) {
		s->has_wfd = true;

		/* require SET response */
		++s->setup_cnt;

		r = wpas_message_new_request(s->bus_global,
					     "SET",
					     &m);
		if (r < 0) {
			log_vERR(r);
			goto error;
		}

		r = wpas_message_append(m, "ss", "wifi_display", "1");
		if (r < 0) {
			log_vERR(r);
			goto error;
		}

		r = wpas_call_async(s->bus_global,
				    m,
				    supplicant_set_wifi_display_fn,
				    s,
				    0,
				    NULL);
		if (r < 0) {
			log_vERR(r);
			goto error;
		}
	}

	if (p2p_mac) {
		log_debug("local p2p-address is: %s", p2p_mac);
		t = strdup(p2p_mac);
		if (!t) {
			log_vENOMEM();
		} else {
			free(s->p2p_mac);
			s->p2p_mac = t;
		}
	}

	supplicant_try_ready(s);
	return 0;

error:
	supplicant_failed(s);
	return 0;
}

static void supplicant_started(struct supplicant *s)
{
	_wpas_message_unref_ struct wpas_message *m = NULL;
	int r;

	/* clear left-overs from previous runs */
	s->p2p_scanning = false;

	/* require STATUS response */
	++s->setup_cnt;

	r = wpas_message_new_request(s->bus_global,
				     "STATUS",
				     &m);
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	r = wpas_call_async(s->bus_global,
			    m,
			    supplicant_status_fn,
			    s,
			    0,
			    NULL);
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	supplicant_try_ready(s);
	return;

error:
	supplicant_failed(s);
}

static void supplicant_stopped(struct supplicant *s)
{
	struct supplicant_group *g;
	struct peer *p;

	while ((p = LINK_FIRST_PEER(s->l)))
		supplicant_peer_free(p->sp);

	while (!shl_dlist_empty(&s->groups)) {
		g = shl_dlist_first_entry(&s->groups,
					  struct supplicant_group,
					  list);
		supplicant_group_free(g);
	}

	free(s->p2p_mac);
	s->p2p_mac = NULL;

	if (s->running) {
		s->running = false;
		link_supplicant_stopped(s->l);
	}
}

static int supplicant_p2p_find_fn(struct wpas *w,
				  struct wpas_message *reply,
				  void *data)
{
	struct supplicant *s = data;

	/* if already scanning, ignore any failures */
	if (s->p2p_scanning)
		return 0;

	if (!wpas_message_is_ok(reply)) {
		log_warning("P2P_FIND failed");
		return 0;
	}

	log_debug("p2p-scanning now active on %s", s->l->ifname);
	s->p2p_scanning = true;
	link_supplicant_p2p_scan_changed(s->l, true);

	return 0;
}

int supplicant_set_friendly_name(struct supplicant *s, const char *name)
{
	_wpas_message_unref_ struct wpas_message *m = NULL;
	int r;

	if (!s->running || !name || !*name)
		return log_EINVAL();

	r = wpas_message_new_request(s->bus_global,
				     "SET",
				     &m);
	if (r < 0)
		return log_ERR(r);

	r = wpas_message_append(m, "ss", "device_name", name);
	if (r < 0)
		return log_ERR(r);

	r = wpas_call_async(s->bus_global,
			    m,
			    NULL,
			    NULL,
			    0,
			    NULL);
	if (r < 0)
		return log_ERR(r);

	log_debug("send 'SET device_name %s' to wpas on %s",
		  name, s->l->ifname);

	return 0;
}

int supplicant_set_wfd_subelements(struct supplicant *s, const char *val)
{
	_wpas_message_unref_ struct wpas_message *m = NULL;
	int r;

	if (!s->running || !val)
		return log_EINVAL();

	r = wpas_message_new_request(s->bus_global,
				     "WFD_SUBELEM_SET",
				     &m);
	if (r < 0)
		return log_ERR(r);

	r = wpas_message_append(m, "s", "0");
	if (r < 0)
		return log_ERR(r);

	if (!shl_isempty(val)) {
		r = wpas_message_append(m, "s", val);
		if (r < 0)
			return log_ERR(r);
	}

	r = wpas_call_async(s->bus_global,
			    m,
			    NULL,
			    NULL,
			    0,
			    NULL);
	if (r < 0)
		return log_ERR(r);

	log_debug("send 'WFD_SUBELEM_SET 0 %s' to wpas on %s",
		  val, s->l->ifname);

	return 0;
}

int supplicant_p2p_start_scan(struct supplicant *s)
{
	_wpas_message_unref_ struct wpas_message *m = NULL;
	int r;

	if (!s->running || !s->has_p2p)
		return log_EINVAL();

	s->pending = NULL;

	/*
	 * This call is asynchronous. You can safely issue multiple of these
	 * in parallel. You're supposed to track the "p2p_scanning" boolean
	 * value to see whether scanning really is active or not.
	 *
	 * Note that we could make this synchronous, but there's no real gain.
	 * You still don't get any meaningful errors from wpa_supplicant, so
	 * there's really no use to it. Moreover, wpas' state tracking is quite
	 * unreliable so we can never know whether we're really still scanning.
	 * Therefore, we send the P2P_FIND on _each_ start_scan() request. It's
	 * the callers responsibility to send it in proper intervals or after
	 * they issues other wpas calls. Yes, this is ugly but currently the
	 * only way to make this work reliably.
	 */

	r = wpas_message_new_request(s->bus_global,
				     "P2P_FIND",
				     &m);
	if (r < 0)
		return log_ERR(r);

	r = wpas_call_async(s->bus_global,
			    m,
			    supplicant_p2p_find_fn,
			    s,
			    0,
			    NULL);
	if (r < 0)
		return log_ERR(r);

	log_debug("sent P2P_FIND to wpas on %s", s->l->ifname);

	return 0;
}

void supplicant_p2p_stop_scan(struct supplicant *s)
{
	_wpas_message_unref_ struct wpas_message *m = NULL;
	int r;

	if (!s->running || !s->has_p2p)
		return log_vEINVAL();

	/*
	 * Always send the P2P_STOP_FIND message even if we think we're not
	 * scanning right now. There might be an asynchronous p2p_find pending,
	 * so abort that by a later p2p_stop_find.
	 */

	r = wpas_message_new_request(s->bus_global,
				     "P2P_STOP_FIND",
				     &m);
	if (r < 0)
		return log_vERR(r);

	r = wpas_call_async(s->bus_global,
			    m,
			    NULL,
			    NULL,
			    0,
			    NULL);
	if (r < 0)
		return log_vERR(r);

	log_debug("sent P2P_STOP_FIND to wpas on %s", s->l->ifname);
}

bool supplicant_p2p_scanning(struct supplicant *s)
{
	return s && s->running && s->has_p2p && s->p2p_scanning;
}

/*
 * Supplicant Control
 * This is the core supplicant-handling, each object manages one external
 * wpa_supplicant process doing the hard work for us. We do all that
 * asynchronously.
 *
 * We support rate-controlled restarting of wpas connections, respawning in case
 * of errors and other fallback handling. We open one connection to the global
 * control interface of wpas, and in case of legacy drivers, a separate
 * p2p-dev-* iface connection.
 *
 * We don't use shared wpas instances across multiple devices. wpas runs only a
 * single p2p-supplicant per instance, so we'd be highly limited. Furthermore,
 * wpas is synchronous in most operations which is very annoying if you have to
 * deal with multiple parallel interfaces.
 */

int supplicant_new(struct link *l,
		   struct supplicant **out)
{
	struct supplicant *s;

	if (!l)
		return log_EINVAL();

	log_debug("new supplicant for %s", l->ifname);

	s = calloc(1, sizeof(*s));
	if (!s)
		return log_ENOMEM();

	s->l = l;
	s->pid = -1;
	shl_dlist_init(&s->groups);

	/* allow 2 restarts in 10s */
	SHL_RATELIMIT_INIT(s->restart_rate, 10 * 1000ULL * 1000ULL, 2);
	/* allow 3 execs in 10s */
	SHL_RATELIMIT_INIT(s->exec_rate, 10 * 1000ULL * 1000ULL, 3);

	if (out)
		*out = s;

	return 0;
}

void supplicant_free(struct supplicant *s)
{
	if (!s)
		return;

	log_debug("free supplicant of %s", s->l->ifname);

	supplicant_stop(s);
	free(s);
}

static int supplicant_dev_fn(struct wpas *w,
			     struct wpas_message *m,
			     void *data)
{
	struct supplicant *s = data;

	if (!m) {
		log_error("HUP on supplicant dev-socket of %s", s->l->ifname);
		goto error;
	}

	supplicant_event(s, m);
	return 0;

error:
	supplicant_failed(s);
	return 0;
}

static int supplicant_global_fn(struct wpas *w,
				struct wpas_message *m,
				void *data)
{
	struct supplicant *s = data;

	if (!m) {
		log_error("HUP on supplicant socket of %s", s->l->ifname);
		goto error;
	}

	/* ignore events on the global-iface, we only listen on dev-iface */
	if(link_is_using_dev(s->l) && wpas_message_get_ifname(m)) {
        supplicant_event(s, m);
    }

	return 0;

error:
	supplicant_failed(s);
	return 0;
}

static int supplicant_dev_attach_fn(struct wpas *w,
				    struct wpas_message *m,
				    void *data)
{
	struct supplicant *s = data;

	if (!wpas_message_is_ok(m)) {
		log_error("cannot attach to dev-wpas interface of %s",
			  s->l->ifname);
		goto error;
	}

	supplicant_started(s);
	return 0;

error:
	supplicant_failed(s);
	return 0;
}

static int supplicant_global_attach_fn(struct wpas *w,
				       struct wpas_message *reply,
				       void *data)
{
	_wpas_message_unref_ struct wpas_message *m = NULL;
	struct supplicant *s = data;
	int r;

	if (!wpas_message_is_ok(reply)) {
		log_error("cannot attach to global wpas interface of %s",
			  s->l->ifname);
		goto error;
	}

	/*
	 * Devices with P2P_DEVICE support (instead of direct P2P_GO/CLIENT
	 * support) are broken with a *lot* of wpa_supplicant versions on the
	 * global interface. Therefore, try to open the p2p-dev-* interface
	 * after the global-ATTACH succeeded (which means the iface is properly
	 * set up). If this works, use the p2p-dev-* interface, otherwise, just
	 * copy the global interface over to bus_dev.
	 * Event-forwarding is broken on the global-interface in such cases,
	 * too. So ATTACH again on the other interface and ignore events on the
	 * global interface in that case.
	 */

	r = wpas_open(s->dev_ctrl, &s->bus_dev);
	if (r >= 0) {
		r = wpas_attach_event(s->bus_dev, s->l->m->event, 0);
		if (r < 0)
			goto error;

		r = wpas_add_match(s->bus_dev, supplicant_dev_fn, s);
		if (r < 0)
			goto error;

		r = wpas_message_new_request(s->bus_dev, "ATTACH", &m);
		if (r < 0)
			goto error;

		r = wpas_call_async(s->bus_dev,
				    m,
				    supplicant_dev_attach_fn,
				    s,
				    0,
				    NULL);
		if (r < 0)
			goto error;

		return 0;
	}

	s->bus_dev = s->bus_global;
	wpas_ref(s->bus_dev);

	r = wpas_add_match(s->bus_dev, supplicant_dev_fn, s);
	if (r < 0)
		goto error;

	supplicant_started(s);
	return 0;

error:
	supplicant_failed(s);
	return 0;
}

static int supplicant_open(struct supplicant *s)
{
	_wpas_message_unref_ struct wpas_message *m = NULL;
	int r;

	log_debug("open supplicant of %s", s->l->ifname);

	r = wpas_open(s->global_ctrl, &s->bus_global);
	if (r < 0) {
		if (r != -ENOENT && r != ECONNREFUSED)
			log_error("cannot connect to wpas: %d", r);
		return r;
	}

	r = wpas_attach_event(s->bus_global, s->l->m->event, 0);
	if (r < 0)
		goto error;

	r = wpas_add_match(s->bus_global, supplicant_global_fn, s);
	if (r < 0)
		goto error;

	r = wpas_message_new_request(s->bus_global, "ATTACH", &m);
	if (r < 0)
		goto error;

	r = wpas_call_async(s->bus_global,
			    m,
			    supplicant_global_attach_fn,
			    s,
			    0,
			    NULL);
	if (r < 0)
		goto error;

	return 0;

error:
	log_error("cannot connect to wpas: %d", r);
	wpas_unref(s->bus_global);
	s->bus_global = NULL;
	return r;
}

static void supplicant_close(struct supplicant *s)
{
	log_debug("close supplicant of %s", s->l->ifname);

	wpas_remove_match(s->bus_dev, supplicant_dev_fn, s);
	wpas_detach_event(s->bus_dev);
	wpas_unref(s->bus_dev);
	s->bus_dev = NULL;

	wpas_remove_match(s->bus_global, supplicant_global_fn, s);
	wpas_detach_event(s->bus_global);
	wpas_unref(s->bus_global);
	s->bus_global = NULL;
}

static void supplicant_failed(struct supplicant *s)
{
	uint64_t ms;
	int r;

	if (shl_ratelimit_test(&s->restart_rate)) {
		ms = 200ULL;
		log_error("wpas (pid:%d) failed unexpectedly, relaunching after short grace period..",
			  (int)s->pid);
	} else {
		ms = 30 * 1000ULL;
		log_error("wpas (pid:%d) failed again.. entering grace period, waiting %llus before relaunching",
			  (int)s->pid, (unsigned long long)ms / 1000ULL);
	}

	ms *= 1000;
	ms += shl_now(CLOCK_MONOTONIC);
	sd_event_source_set_time(s->timer_source, ms);
	sd_event_source_set_enabled(s->timer_source, SD_EVENT_ON);

	/* Always send SIGTERM, even if already dead the child is still not
	 * reaped so we can still send signals. */
	if (s->pid > 0) {
		log_debug("terminating wpas (pid:%d)", (int)s->pid);
		r = kill(s->pid, SIGTERM);
		if (r < 0)
			r = kill(s->pid, SIGKILL);
		if (r < 0)
			log_warning("cannot kill wpas pid:%d: %m",
				    (int)s->pid);
	}

	s->pid = 0;
	sd_event_source_unref(s->child_source);
	s->child_source = NULL;

	supplicant_close(s);
	supplicant_stopped(s);
}

static int supplicant_child_fn(sd_event_source *source,
			       const siginfo_t *si,
			       void *data)
{
	struct supplicant *s = data;

	supplicant_failed(s);

	return 0;
}

static void supplicant_run(struct supplicant *s, const char *binary)
{
	char *argv[64], journal_id[128];
	int i, fd_journal;
	sigset_t mask;

	sigemptyset(&mask);
	sigprocmask(SIG_SETMASK, &mask, NULL);

#ifdef ENABLE_SYSTEMD
	/* redirect stdout/stderr to journal */
	sprintf(journal_id, "miracle-wifid-%s-%u",
		s->l->ifname, s->l->ifindex);
	fd_journal = sd_journal_stream_fd(journal_id, LOG_INFO, false);
	if (fd_journal >= 0) {
		/* dup journal-fd to stdout and stderr */
		dup2(fd_journal, 1);
		dup2(fd_journal, 2);
	} else {
#endif
		/* no journal? redirect stdout to parent's stderr */
		dup2(2, 1);
#ifdef ENABLE_SYSTEMD
	}
#endif

	/* initialize wpa_supplicant args */
	i = 0;
	argv[i++] = (char*)binary;

	/* debugging? */
	if (arg_wpa_loglevel >= LOG_DEBUG)
		argv[i++] = "-dd";
	else if (arg_wpa_loglevel >= LOG_INFO)
		argv[i++] = "-d";
	else if (arg_wpa_loglevel < LOG_ERROR)
		argv[i++] = "-qq";
	else if (arg_wpa_loglevel < LOG_NOTICE)
		argv[i++] = "-q";

	argv[i++] = "-c";
	argv[i++] = s->conf_path;
	argv[i++] = "-C";
	argv[i++] = (char*)"/run/miracle/wifi";
	argv[i++] = "-i";
	argv[i++] = s->l->ifname;
	argv[i++] = "-g";
	argv[i++] = s->global_ctrl;

	if (arg_wpa_syslog) {
		argv[i++] = "-s";
	}

	argv[i] = NULL;

	/* execute wpa_supplicant; if it fails, the caller issues exit(1) */
	execve(argv[0], argv, environ);
}

static int supplicant_find(char **binary)
{
    _shl_free_ char *path = getenv("PATH");
    if(!path) {
        return -EINVAL;
    }

    path = strdup(path);
    if(!path) {
        return log_ENOMEM();
    }

    struct stat bin_stat;
    char *curr = path, *next;
    while(1) {
        curr = strtok_r(curr, ":", &next);
        if(!curr) {
            break;
        }

        _shl_free_ char *bin = shl_strcat(curr, "/wpa_supplicant");
        if (!bin)
            return log_ENOMEM();

        if(stat(bin, &bin_stat) < 0) {
            if(ENOENT == errno || ENOTDIR == errno) {
                goto end;
            }
            return log_ERRNO();
        }

        if (!access(bin, X_OK)) {
            *binary = strdup(bin);
            return 0;
        }

end:
        curr = NULL;
    }

    return -EINVAL;
}

static int supplicant_spawn(struct supplicant *s)
{
	_shl_free_ char *binary = NULL;
	pid_t pid;
	int r;

	if (!s)
		return log_EINVAL();
	if (s->pid > 0)
		return 0;

	log_debug("spawn supplicant of %s", s->l->ifname);

    if (supplicant_find(&binary) < 0) {
        if (binary != NULL) {
            log_error("execution of wpas (%s) not possible: %m", binary);
	} else {
            log_error("execution of wpas not possible: %m");
	}
        return -EINVAL;
    }

    log_info("wpa_supplicant found: %s", binary);

	pid = fork();
	if (pid < 0) {
		return log_ERRNO();
	} else if (!pid) {
		supplicant_run(s, binary);
		exit(1);
	}

	s->pid = pid;
	s->open_cnt = 0;
	log_info("wpas spawned as pid:%d", (int)pid);

	sd_event_source_unref(s->child_source);
	s->child_source = NULL;

	r = sd_event_add_child(s->l->m->event,
			       &s->child_source,
			       s->pid,
			       WEXITED,
			       supplicant_child_fn,
			       s);
	if (r < 0)
		return log_ERR(r);

	return 0;
}

static int supplicant_timer_fn(sd_event_source *source,
			       uint64_t usec,
			       void *data)
{
	struct supplicant *s = data;
	uint64_t ms;
	int r;

	if (!s->pid) {
		r = supplicant_spawn(s);
		if (r < 0) {
			/* We cannot *spawn* wpas, we might have hit a
			 * system-update or similar. Try again in a short
			 * timespan, but if that fails to often, we stop trying
			 * for a longer time. Note that the binary has been
			 * around during startup, otherwise, supplicant_start()
			 * would have failed. Therefore, we know that there at
			 * least used to be an executable so it's fine to
			 * retry.*/
			if (shl_ratelimit_test(&s->exec_rate)) {
				ms = 1000ULL; /* 1000ms */
				log_error("cannot execute wpas, retrying after short grace period..");
			} else {
				ms = 60 * 1000ULL; /* 60s */
				log_error("still cannot execute wpas.. entering grace period, waiting %llus before retrying",
					  (unsigned long long)ms / 1000ULL);
			}

			ms *= 1000ULL;
			ms += shl_now(CLOCK_MONOTONIC);
			sd_event_source_set_time(source, ms);
			sd_event_source_set_enabled(source, SD_EVENT_ON);
		} else {
			ms = shl_now(CLOCK_MONOTONIC);
			ms += 200 * 1000ULL; /* 200ms startup timer */
			sd_event_source_set_time(source, ms);
			sd_event_source_set_enabled(source, SD_EVENT_ON);
		}
	} else if (s->pid > 0 && !s->running) {
		r = supplicant_open(s);
		if (r < 0) {
			/* Cannot connect to supplicant, retry in 200ms
			 * but increase the timeout for each attempt so
			 * we lower the rate in case sth goes wrong. */
			s->open_cnt = shl_min(s->open_cnt + 1, (uint64_t)1000);
			ms = s->open_cnt * 200 * 1000ULL;
			ms += shl_now(CLOCK_MONOTONIC);
			sd_event_source_set_time(source, ms);
			sd_event_source_set_enabled(source, SD_EVENT_ON);
			if (s->open_cnt == 5)
				log_warning("still cannot connect to wpas after 5 retries");
		} else {
			/* wpas is running smoothly, disable timer */
			sd_event_source_set_enabled(source, SD_EVENT_OFF);
		}
	} else {
		/* Who armed this timer? What timer is this? */
		sd_event_source_set_enabled(source, SD_EVENT_OFF);
	}

	return 0;
}

static int supplicant_write_config(struct supplicant *s)
{
	_shl_free_ char *path = NULL;
	FILE *f;
	int r;

	r = asprintf(&path, "/run/miracle/wifi/%s-%u.conf",
		     s->l->ifname, s->l->ifindex);
	if (r < 0)
		return log_ENOMEM();

	f = fopen(path, "we");
	if (!f)
		return log_ERRNO();

	r = fprintf(f,
		    "# Generated configuration - DO NOT EDIT!\n"
		    "device_name=%s\n"
		    "device_type=%s\n"
		    "config_methods=%s\n"
		    "driver_param=%s\n"
		    "ap_scan=%s\n"
		    "# End of configuration\n",
		    s->l->friendly_name ?: "unknown",
		    "1-0050F204-1",
		    s->l->config_methods ?: "pbc",
		    "p2p_device=1",
		    "1");
	if (r < 0) {
		r = log_ERRNO();
		fclose(f);
		return r;
	}

	fclose(f);
	free(s->conf_path);
	s->conf_path = path;
	path = NULL;

	return 0;
}

int supplicant_start(struct supplicant *s)
{
	int r;

	if (!s)
		return log_EINVAL();
	if (s->pid >= 0)
		return 0;

	log_debug("start supplicant of %s", s->l->ifname);

	SHL_RATELIMIT_RESET(s->restart_rate);
	SHL_RATELIMIT_RESET(s->exec_rate);

	r = asprintf(&s->global_ctrl, "/run/miracle/wifi/%s-%u.global",
		     s->l->ifname, s->l->ifindex);
	if (r < 0) {
		r = log_ENOMEM();
		goto error;
	}

	r = asprintf(&s->dev_ctrl, "/run/miracle/wifi/p2p-dev-%s",
		     s->l->ifname);
	if (r < 0) {
		r = log_ENOMEM();
		goto error;
	}

	r = supplicant_write_config(s);
	if (r < 0)
		goto error;

	/* add initial 200ms startup timer */
	r = sd_event_add_time(s->l->m->event,
			      &s->timer_source,
			      CLOCK_MONOTONIC,
			      shl_now(CLOCK_MONOTONIC) + 200 * 1000ULL,
			      0,
			      supplicant_timer_fn,
			      s);
	if (r < 0) {
		log_vERR(r);
		goto error;
	}

	r = supplicant_spawn(s);
	if (r < 0)
		goto error;

	return 0;

error:
	supplicant_stop(s);
	return r;
}

void supplicant_stop(struct supplicant *s)
{
	int r;

	if (!s)
		return log_vEINVAL();

	log_debug("stop supplicant of %s", s->l->ifname);

	supplicant_close(s);

	sd_event_source_unref(s->child_source);
	s->child_source = NULL;
	sd_event_source_unref(s->timer_source);
	s->timer_source = NULL;

	if (s->pid > 0) {
		r = kill(s->pid, SIGTERM);
		if (r < 0)
			r = kill(s->pid, SIGKILL);
		if (r < 0)
			log_warning("cannot kill wpas pid:%d: %m",
				    (int)s->pid);
	}

	if (s->conf_path) {
		unlink(s->conf_path);
		free(s->conf_path);
		s->conf_path = NULL;
	}

	free(s->global_ctrl);
	s->global_ctrl = NULL;
	free(s->dev_ctrl);
	s->dev_ctrl = NULL;

	s->pid = -1;
	supplicant_stopped(s);
}

bool supplicant_is_running(struct supplicant *s)
{
	if (!s) {
		log_vEINVAL();
		return false;
	}

	/* pid > 0 means supplicant-process is known, pid == 0 means we are
	 * currently in a grace period to restart it. pid < 0 means we are
	 * not managing any wpas instance. */

	return s->pid >= 0;
}

bool supplicant_is_ready(struct supplicant *s)
{
	if (!s) {
		log_vEINVAL();
		return false;
	}

	return s->running;
}
