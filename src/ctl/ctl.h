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

#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <systemd/sd-bus.h>
#include "shl_dlist.h"
#include "shl_log.h"

#ifndef CTL_CTL_H
#define CTL_CTL_H

struct ctl_wifi;
struct ctl_link;
struct ctl_peer;

/* wifi handling */

struct ctl_peer {
	struct shl_dlist list;
	char *label;
	struct ctl_link *l;

	/* properties */
	char *p2p_mac;
	char *friendly_name;
	bool connected;
	char *interface;
	char *local_address;
	char *remote_address;
	char *wfd_subelements;
};

#define peer_from_dlist(_p) shl_dlist_entry((_p), struct ctl_peer, list);

int ctl_peer_connect(struct ctl_peer *p, const char *prov, const char *pin);
int ctl_peer_disconnect(struct ctl_peer *p);

struct ctl_link {
	struct shl_dlist list;
	char *label;
	struct ctl_wifi *w;

	struct shl_dlist peers;

	bool have_p2p_scan;

	/* properties */
	unsigned int ifindex;
	char *ifname;
	char *friendly_name;
	bool managed;
	char *wfd_subelements;
	bool p2p_scanning;
};

#define link_from_dlist(_l) shl_dlist_entry((_l), struct ctl_link, list);

int ctl_link_set_friendly_name(struct ctl_link *l, const char *name);
int ctl_link_set_managed(struct ctl_link *l, bool val);
int ctl_link_set_wfd_subelements(struct ctl_link *l, const char *val);
int ctl_link_set_p2p_scanning(struct ctl_link *l, bool val);

struct ctl_wifi {
	sd_bus *bus;

	struct shl_dlist links;
};

int ctl_wifi_new(struct ctl_wifi **out, sd_bus *bus);
void ctl_wifi_free(struct ctl_wifi *w);
int ctl_wifi_fetch(struct ctl_wifi *w);

struct ctl_link *ctl_wifi_find_link(struct ctl_wifi *w,
				    const char *label);
struct ctl_link *ctl_wifi_search_link(struct ctl_wifi *w,
				      const char *label);
struct ctl_link *ctl_wifi_find_link_by_peer(struct ctl_wifi *w,
					    const char *label);
struct ctl_link *ctl_wifi_search_link_by_peer(struct ctl_wifi *w,
					      const char *label);
struct ctl_peer *ctl_wifi_find_peer(struct ctl_wifi *w,
				    const char *label);
struct ctl_peer *ctl_wifi_search_peer(struct ctl_wifi *w,
				      const char *real_label);

/* sink handling */

struct ctl_sink;

int ctl_sink_new(struct ctl_sink **out,
		 sd_event *event);
void ctl_sink_free(struct ctl_sink *s);

int ctl_sink_connect(struct ctl_sink *s, const char *target);
void ctl_sink_close(struct ctl_sink *s);
bool ctl_sink_is_connecting(struct ctl_sink *s);
bool ctl_sink_is_connected(struct ctl_sink *s);
bool ctl_sink_is_closed(struct ctl_sink *s);

/* CLI handling */

extern unsigned int cli_max_sev;
void cli_printv(const char *fmt, va_list args);
void cli_printf(const char *fmt, ...);

#define cli_log(_fmt, ...) \
	cli_printf(_fmt "\n", ##__VA_ARGS__)
#define cli_log_fn(_fmt, ...) \
	cli_printf(_fmt " (%s() in %s:%d)\n", ##__VA_ARGS__, __func__, __FILE__, __LINE__)
#define cli_error(_fmt, ...) \
	((LOG_ERROR <= cli_max_sev) ? \
	cli_log_fn("ERROR: " _fmt, ##__VA_ARGS__) : (void)0)
#define cli_warning(_fmt, ...) \
	((LOG_WARNING <= cli_max_sev) ? \
	cli_log_fn("WARNING: " _fmt, ##__VA_ARGS__) : (void)0)
#define cli_notice(_fmt, ...) \
	((LOG_NOTICE <= cli_max_sev) ? \
	cli_log("NOTICE: " _fmt, ##__VA_ARGS__) : (void)0)
#define cli_debug(_fmt, ...) \
	((LOG_DEBUG <= cli_max_sev) ? \
	cli_log_fn("DEBUG: " _fmt, ##__VA_ARGS__) : (void)0)

#define cli_EINVAL() \
	(cli_error("invalid arguments"), -EINVAL)
#define cli_vEINVAL() \
	((void)cli_EINVAL())

#define cli_EFAULT() \
	(cli_error("internal operation failed"), -EFAULT)
#define cli_vEFAULT() \
	((void)cli_EFAULT())

#define cli_ENOMEM() \
	(cli_error("out of memory"), -ENOMEM)
#define cli_vENOMEM() \
	((void)cli_ENOMEM())

#define cli_EPIPE() \
	(cli_error("fd closed unexpectedly"), -EPIPE)
#define cli_vEPIPE() \
	((void)cli_EPIPE())

#define cli_ERRNO() \
	(cli_error("syscall failed (%d): %m", errno), -errno)
#define cli_vERRNO() \
	((void)cli_ERRNO())

#define cli_ERR(_r) \
	(errno = -(_r), cli_error("syscall failed (%d): %m", (_r)), (_r))
#define cli_vERR(_r) \
	((void)cli_ERR(_r))

#define cli_log_parser(_r) \
	(cli_error("cannot parse dbus message: %s", \
		   strerror((_r) < 0 ? -(_r) : (_r))), (_r))

#define cli_log_create(_r) \
	(cli_error("cannot create dbus message: %s", \
		   strerror((_r) < 0 ? -(_r) : (_r))), (_r))

#define CLI_DEFAULT		"\x1B[0m"
#define CLI_RED			"\x1B[0;91m"
#define CLI_GREEN		"\x1B[0;92m"
#define CLI_YELLOW		"\x1B[0;93m"
#define CLI_BLUE		"\x1B[0;94m"
#define CLI_BOLDGRAY		"\x1B[1;30m"
#define CLI_BOLDWHITE		"\x1B[1;37m"

#define CLI_PROMPT		CLI_BLUE "[miraclectl] # " CLI_DEFAULT

struct cli_cmd {
	const char *cmd;
	const char *args;
	enum {
		CLI_N,	/* no */
		CLI_M,	/* maybe */
		CLI_Y,	/* yes */
	} cli_cmp;
	enum {
		CLI_MORE,
		CLI_LESS,
		CLI_EQUAL,
	} argc_cmp;
	int argc;
	int (*fn) (char **args, unsigned int n);
	const char *desc;
};

extern sd_event *cli_event;
extern sd_bus *cli_bus;

extern unsigned int wfd_supported_res_cea;
extern unsigned int wfd_supported_res_vesa;
extern unsigned int wfd_supported_res_hh;

int cli_init(sd_bus *bus, const struct cli_cmd *cmds);
void cli_destroy(void);
int cli_run(void);
void cli_exit(void);
bool cli_running(void);

int cli_help(const struct cli_cmd *cmds, int whitespace);
int cli_do(const struct cli_cmd *cmds, char **args, unsigned int n);

/* callback functions */

void ctl_fn_peer_new(struct ctl_peer *p);
void ctl_fn_peer_free(struct ctl_peer *p);
void ctl_fn_peer_provision_discovery(struct ctl_peer *p,
				     const char *prov,
				     const char *pin);
void ctl_fn_peer_go_neg_request(struct ctl_peer *p,
				     const char *prov,
				     const char *pin);
void ctl_fn_peer_formation_failure(struct ctl_peer *p, const char *reason);
void ctl_fn_peer_connected(struct ctl_peer *p);
void ctl_fn_peer_disconnected(struct ctl_peer *p);
void ctl_fn_link_new(struct ctl_link *l);
void ctl_fn_link_free(struct ctl_link *l);

void ctl_fn_sink_connected(struct ctl_sink *s);
void ctl_fn_sink_disconnected(struct ctl_sink *s);
void ctl_fn_sink_resolution_set(struct ctl_sink *s);

void cli_fn_help(void);

#endif /* CTL_CTL_H */
