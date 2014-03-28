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

#include "test_common.h"
#include "wpas.h"

static struct wpas *server, *client;
static sd_event *event;

static struct wpas *start_test_client(void)
{
	char spath[128];
	int r;

	sprintf(spath, "/tmp/miracle-test-sock-%d", getpid());

	r = sd_event_default(&event);
	ck_assert_int_ge(r, 0);

	r = wpas_create(spath, &server);
	ck_assert_int_ge(r, 0);
	r = wpas_attach_event(server, NULL, 0);
	ck_assert_int_ge(r, 0);

	r = wpas_open(spath, &client);
	ck_assert_int_ge(r, 0);
	r = wpas_attach_event(client, NULL, 0);
	ck_assert_int_ge(r, 0);

	return client;
}

static void stop_test_client(void)
{
	wpas_unref(client);
	client = NULL;
	wpas_unref(server);
	server = NULL;
	sd_event_unref(event);
	event = NULL;
}

START_TEST(bus_invalid_open)
{
	char ipath[128];
	struct wpas *w;
	int r;

	sprintf(ipath, "/tmp/miracle/invalid-test-dir/invalid-test-path-%d",
		getpid());

	/* test invalid client */

	w = TEST_INVALID_PTR;
	r = wpas_open(NULL, NULL);
	ck_assert_int_lt(r, 0);

	r = wpas_open(ipath, NULL);
	ck_assert_int_lt(r, 0);

	r = wpas_open(NULL, &w);
	ck_assert_int_lt(r, 0);
	ck_assert(w == TEST_INVALID_PTR);

	r = wpas_open(ipath, &w);
	ck_assert_int_lt(r, 0);
	ck_assert(w == TEST_INVALID_PTR);
}
END_TEST

START_TEST(bus_invalid_create)
{
	char ipath[128];
	struct wpas *s;
	int r;

	sprintf(ipath, "/tmp/miracle/invalid-test-dir/invalid-test-path-%d",
		getpid());

	/* test invalid server */

	s = TEST_INVALID_PTR;
	r = wpas_create(NULL, NULL);
	ck_assert_int_lt(r, 0);

	r = wpas_create(ipath, NULL);
	ck_assert_int_lt(r, 0);

	r = wpas_create(NULL, &s);
	ck_assert_int_lt(r, 0);
	ck_assert(s == TEST_INVALID_PTR);
}
END_TEST

START_TEST(bus_create)
{
	char spath[128];
	struct wpas *w, *s;
	int r;

	sprintf(spath, "/tmp/miracle-test-sock-%d", getpid());

	/* test server creation */

	s = TEST_INVALID_PTR;
	w = TEST_INVALID_PTR;

	unlink(spath);
	ck_assert_int_lt(access(spath, F_OK), 0);

	r = wpas_create(spath, &s);
	ck_assert_int_ge(r, 0);
	ck_assert(s != TEST_INVALID_PTR);
	ck_assert(s != NULL);

	ck_assert_int_ge(access(spath, F_OK), 0);

	r = wpas_create(spath, &w);
	ck_assert_int_eq(r, -EADDRINUSE);
	ck_assert(w == TEST_INVALID_PTR);

	ck_assert_int_ge(access(spath, F_OK), 0);

	wpas_unref(s);
	s = TEST_INVALID_PTR;

	ck_assert_int_lt(access(spath, F_OK), 0);

	/* test again with pre-existing but unused file */
	ck_assert_int_ge(open(spath, O_RDWR | O_CREAT | O_CLOEXEC, S_IRWXU), 0);
	ck_assert_int_ge(access(spath, F_OK), 0);

	r = wpas_create(spath, &s);
	ck_assert_int_ge(r, 0);
	ck_assert(s != TEST_INVALID_PTR);
	ck_assert(s != NULL);

	ck_assert_int_ge(access(spath, F_OK), 0);

	wpas_unref(s);
	s = TEST_INVALID_PTR;

	ck_assert_int_lt(access(spath, F_OK), 0);
}
END_TEST

START_TEST(bus_open)
{
	char spath[128];
	struct wpas *w, *s;
	int r;

	sprintf(spath, "/tmp/miracle-test-sock-%d", getpid());

	/* test client connection */

	s = TEST_INVALID_PTR;
	w = TEST_INVALID_PTR;

	r = wpas_create(spath, &s);
	ck_assert_int_ge(r, 0);

	ck_assert_int_ge(access(spath, F_OK), 0);

	r = wpas_open(spath, &w);
	ck_assert_int_ge(r, 0);

	wpas_unref(w);
	wpas_unref(s);

	ck_assert_int_lt(access(spath, F_OK), 0);
}
END_TEST

TEST_DEFINE_CASE(bus)
	TEST(bus_invalid_open)
	TEST(bus_invalid_create)
	TEST(bus_create)
	TEST(bus_open)
TEST_END_CASE

START_TEST(msg_invalid_new)
{
	struct wpas_message *m;
	struct wpas *w;
	int r;

	w = start_test_client();

	m = TEST_INVALID_PTR;

	r = wpas_message_new_event(NULL, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);

	r = wpas_message_new_event(w, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);

	r = wpas_message_new_event(NULL, "name", 0, NULL);
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);

	r = wpas_message_new_event(NULL, NULL, 0, &m);
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);

	r = wpas_message_new_event(w, NULL, 0, &m);
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);

	r = wpas_message_new_event(w, "", 0, &m);
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);

	r = wpas_message_new_event(w, "name", 0, NULL);
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);

	r = wpas_message_new_event(NULL, "name", 0, &m);
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);

	stop_test_client();
}
END_TEST

START_TEST(msg_new_event)
{
	struct wpas_message *m;
	struct wpas *w;
	int r;

	w = start_test_client();

	m = TEST_INVALID_PTR;

	r = wpas_message_new_event(w, "name", 5, &m);
	ck_assert_int_ge(r, 0);
	ck_assert(m != TEST_INVALID_PTR);
	ck_assert(m != NULL);

	ck_assert_int_eq(wpas_message_is_event(m, NULL), 1);
	ck_assert_int_eq(wpas_message_is_event(m, "name"), 1);
	ck_assert_int_eq(wpas_message_is_event(m, "names"), 0);
	ck_assert_int_eq(wpas_message_is_event(m, "nam"), 0);
	ck_assert_int_eq(wpas_message_is_event(m, ""), 0);
	ck_assert_int_eq(wpas_message_is_request(m, NULL), 0);
	ck_assert_int_eq(wpas_message_is_reply(m), 0);

	ck_assert_int_eq(wpas_message_get_cookie(m), 0);
	ck_assert_ptr_eq(wpas_message_get_bus(m), w);
	ck_assert_int_eq(wpas_message_get_type(m), WPAS_MESSAGE_EVENT);
	ck_assert_int_eq(wpas_message_get_level(m), 5);
	ck_assert_str_eq(wpas_message_get_name(m), "name");
	ck_assert_ptr_eq((void*)wpas_message_get_raw(m), NULL);

	wpas_message_unref(m);

	stop_test_client();
}
END_TEST

START_TEST(msg_new_request)
{
	struct wpas_message *m;
	struct wpas *w;
	int r;

	w = start_test_client();

	m = TEST_INVALID_PTR;

	r = wpas_message_new_request(w, "name", &m);
	ck_assert_int_ge(r, 0);
	ck_assert(m != TEST_INVALID_PTR);
	ck_assert(m != NULL);

	ck_assert_int_eq(wpas_message_is_request(m, NULL), 1);
	ck_assert_int_eq(wpas_message_is_request(m, "name"), 1);
	ck_assert_int_eq(wpas_message_is_request(m, "names"), 0);
	ck_assert_int_eq(wpas_message_is_request(m, "nam"), 0);
	ck_assert_int_eq(wpas_message_is_request(m, ""), 0);
	ck_assert_int_eq(wpas_message_is_event(m, NULL), 0);
	ck_assert_int_eq(wpas_message_is_reply(m), 0);

	ck_assert_int_eq(wpas_message_get_cookie(m), 0);
	ck_assert_ptr_eq(wpas_message_get_bus(m), w);
	ck_assert_int_eq(wpas_message_get_type(m), WPAS_MESSAGE_REQUEST);
	ck_assert_int_eq(wpas_message_get_level(m), 0);
	ck_assert_str_eq(wpas_message_get_name(m), "name");
	ck_assert_ptr_eq((void*)wpas_message_get_raw(m), NULL);

	wpas_message_unref(m);

	stop_test_client();
}
END_TEST

START_TEST(msg_new_reply)
{
	struct wpas_message *m;
	struct wpas *w;
	int r;

	w = start_test_client();

	m = TEST_INVALID_PTR;

	r = wpas_message_new_reply(w, &m);
	ck_assert_int_ge(r, 0);
	ck_assert(m != TEST_INVALID_PTR);
	ck_assert(m != NULL);

	ck_assert_int_eq(wpas_message_is_reply(m), 1);
	ck_assert_int_eq(wpas_message_is_event(m, NULL), 0);
	ck_assert_int_eq(wpas_message_is_request(m, NULL), 0);

	ck_assert_int_eq(wpas_message_get_cookie(m), 0);
	ck_assert_ptr_eq(wpas_message_get_bus(m), w);
	ck_assert_int_eq(wpas_message_get_type(m), WPAS_MESSAGE_REPLY);
	ck_assert_int_eq(wpas_message_get_level(m), 0);
	ck_assert_ptr_eq((void*)wpas_message_get_name(m), NULL);
	ck_assert_ptr_eq((void*)wpas_message_get_raw(m), NULL);

	wpas_message_unref(m);

	stop_test_client();
}
END_TEST

START_TEST(msg_peer)
{
	struct wpas_message *m;
	struct wpas *w;
	char *t;
	int r;

	w = start_test_client();

	r = wpas_message_new_event(w, "name", 5, &m);
	ck_assert_int_ge(r, 0);

	ck_assert_ptr_eq((void*)wpas_message_get_peer(m), NULL);
	t = wpas_message_get_escaped_peer(m);
	ck_assert_str_eq(t, "<none>");
	free(t);

	wpas_message_set_peer(m, "/some/path");
	ck_assert_str_eq(wpas_message_get_peer(m), "/some/path");
	t = wpas_message_get_escaped_peer(m);
	ck_assert_str_eq(t, "/some/path");
	free(t);

	wpas_message_set_peer(m, "\0/some/path");
	ck_assert_str_eq(wpas_message_get_peer(m), "");
	ck_assert_str_eq(wpas_message_get_peer(m) + 1, "/some/path");
	t = wpas_message_get_escaped_peer(m);
	ck_assert_str_eq(t, "@abstract:/some/path");
	free(t);

	wpas_message_set_peer(m, NULL);
	ck_assert_ptr_eq((void*)wpas_message_get_peer(m), NULL);
	t = wpas_message_get_escaped_peer(m);
	ck_assert_str_eq(t, "<none>");
	free(t);

	wpas_message_unref(m);

	stop_test_client();
}
END_TEST

START_TEST(msg_append)
{
	struct wpas_message *m;
	struct wpas *w;
	int r;

	w = start_test_client();

	r = wpas_message_new_event(w, "name", 5, &m);
	ck_assert_int_ge(r, 0);

	r = wpas_message_seal(m);
	ck_assert_int_ge(r, 0);

	ck_assert_str_eq(wpas_message_get_raw(m), "<5>name");

	wpas_message_unref(m);

	r = wpas_message_new_event(w, "name", 5, &m);
	ck_assert_int_ge(r, 0);

	r = wpas_message_append(m,
				"suie",
				"string",
				(uint32_t)5,
				(int32_t)1,
				"key",
				"value");
	ck_assert_int_ge(r, 0);

	r = wpas_message_seal(m);
	ck_assert_int_ge(r, 0);

	ck_assert_str_eq(wpas_message_get_raw(m),
			 "<5>name string 5 1 key=value");

	r = wpas_message_skip(m, "suie");
	ck_assert_int_ge(r, 0);

	wpas_message_unref(m);

	stop_test_client();
}
END_TEST

TEST_DEFINE_CASE(msg)
	TEST(msg_invalid_new)
	TEST(msg_new_event)
	TEST(msg_new_request)
	TEST(msg_new_reply)
	TEST(msg_peer)
	TEST(msg_append)
TEST_END_CASE

START_TEST(run_invalid_msg)
{
	struct wpas_message *m;
	int r;

	start_test_client();

	r = wpas_message_new_reply(client, &m);
	ck_assert_int_ge(r, 0);
	r = wpas_call_async(client, m, NULL, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);
	r = wpas_call_async(server, m, NULL, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);
	r = wpas_send(server, m, 0);
	ck_assert_int_lt(r, 0);
	wpas_message_unref(m);

	r = wpas_message_new_event(client, "sth", 0, &m);
	ck_assert_int_ge(r, 0);
	r = wpas_call_async(client, m, NULL, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);
	r = wpas_call_async(server, m, NULL, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);
	r = wpas_send(server, m, 0);
	ck_assert_int_lt(r, 0);
	wpas_message_unref(m);

	r = wpas_message_new_request(client, "sth", &m);
	ck_assert_int_ge(r, 0);
	r = wpas_call_async(server, m, NULL, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);
	r = wpas_send(server, m, 0);
	ck_assert_int_lt(r, 0);
	wpas_message_set_peer(m, "/some/path");
	r = wpas_call_async(client, m, NULL, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);
	wpas_message_unref(m);

	r = wpas_message_new_reply(server, &m);
	ck_assert_int_ge(r, 0);
	r = wpas_call_async(client, m, NULL, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);
	r = wpas_call_async(server, m, NULL, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);
	r = wpas_send(server, m, 0);
	ck_assert_int_lt(r, 0);
	wpas_message_unref(m);

	r = wpas_message_new_event(server, "sth", 0, &m);
	ck_assert_int_ge(r, 0);
	r = wpas_call_async(client, m, NULL, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);
	r = wpas_call_async(server, m, NULL, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);
	r = wpas_send(server, m, 0);
	ck_assert_int_lt(r, 0);
	wpas_message_unref(m);

	r = wpas_message_new_request(server, "sth", &m);
	ck_assert_int_ge(r, 0);
	r = wpas_call_async(server, m, NULL, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);
	r = wpas_send(server, m, 0);
	ck_assert_int_lt(r, 0);
	r = wpas_call_async(client, m, NULL, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);
	wpas_message_set_peer(m, "/some/path");
	r = wpas_call_async(client, m, NULL, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);
	wpas_message_unref(m);

	stop_test_client();
}
END_TEST

START_TEST(run_msg)
{
	struct wpas_message *m;
	int r;

	start_test_client();

	r = wpas_message_new_reply(client, &m);
	ck_assert_int_ge(r, 0);
	r = wpas_send(client, m, 0);
	ck_assert_int_ge(r, 0);
	r = wpas_send(client, m, 0);
	ck_assert_int_lt(r, 0);
	wpas_message_unref(m);

	r = wpas_message_new_reply(client, &m);
	ck_assert_int_ge(r, 0);
	wpas_message_set_peer(m, "/some/peer");
	r = wpas_send(client, m, 0);
	ck_assert_int_ge(r, 0);
	r = wpas_send(client, m, 0);
	ck_assert_int_lt(r, 0);
	wpas_message_unref(m);

	r = wpas_message_new_reply(server, &m);
	ck_assert_int_ge(r, 0);
	r = wpas_send(server, m, 0);
	ck_assert_int_lt(r, 0);
	wpas_message_set_peer(m, "/some/peer");
	r = wpas_send(server, m, 0);
	ck_assert_int_ge(r, 0);
	r = wpas_send(server, m, 0);
	ck_assert_int_lt(r, 0);
	wpas_message_unref(m);

	r = wpas_message_new_request(server, "sth", &m);
	ck_assert_int_ge(r, 0);
	r = wpas_call_async(server, m, NULL, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);
	r = wpas_send(server, m, 0);
	ck_assert_int_lt(r, 0);
	wpas_message_set_peer(m, "/some/peer");
	r = wpas_call_async(server, m, NULL, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);
	r = wpas_send(server, m, 0);
	ck_assert_int_ge(r, 0);
	r = wpas_send(server, m, 0);
	ck_assert_int_lt(r, 0);
	wpas_message_unref(m);

	r = wpas_message_new_request(client, "sth", &m);
	ck_assert_int_ge(r, 0);
	r = wpas_call_async(client, m, NULL, NULL, 0, NULL);
	ck_assert_int_ge(r, 0);
	r = wpas_call_async(client, m, NULL, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);
	wpas_message_unref(m);

	stop_test_client();
}
END_TEST

static int match_fail(struct wpas *w,
		      struct wpas_message *m,
		      void *data)
{
	ck_assert_msg(0, "no CB expected");
	return 0;
}

static int match_count(struct wpas *w,
		       struct wpas_message *m,
		       void *data)
{
	int *expected = data;

	if (!m)
		ck_assert_msg(0, "HUP not expected");

	if (!--*expected)
		sd_event_exit(event, 0);

	return 0;
}

START_TEST(run_send)
{
	struct wpas_message *m;
	int r, expected;

	start_test_client();

	r = wpas_add_match(client, NULL, NULL);
	ck_assert_int_lt(r, 0);
	r = wpas_add_match(client, match_count, &expected);
	ck_assert_int_ge(r, 0);
	r = wpas_add_match(server, match_count, &expected);
	ck_assert_int_ge(r, 0);

	expected = 2;

	r = wpas_message_new_event(client, "sth", 0, &m);
	ck_assert_int_ge(r, 0);
	r = wpas_send(client, m, 0);
	ck_assert_int_ge(r, 0);
	wpas_message_unref(m);

	r = wpas_message_new_request(client, "sth-more", &m);
	ck_assert_int_ge(r, 0);
	r = wpas_call_async(client, m, match_fail, NULL, 0, NULL);
	ck_assert_int_ge(r, 0);
	wpas_message_unref(m);

	r = sd_event_loop(event);
	ck_assert_int_ge(r, 0);

	stop_test_client();
}
END_TEST

static int match_msg(struct wpas *w,
		     struct wpas_message *m,
		     void *data)
{
	struct wpas_message **orig = data;

	if (!m)
		ck_assert_msg(0, "HUP not expected");

	ck_assert_str_eq(wpas_message_get_raw(m),
			 wpas_message_get_raw(*orig));

	sd_event_exit(event, 0);

	return 0;
}

START_TEST(run_parse)
{
	struct wpas_message *m;
	int r;

	start_test_client();

	r = wpas_add_match(server, match_msg, &m);
	ck_assert_int_ge(r, 0);

	r = wpas_message_new_request(client, "sth", &m);
	ck_assert_int_ge(r, 0);
	r = wpas_message_append(m,
				"ssie",
				"some random string\\''\"\"bla",
				"more-string\\data",
				(int32_t)65537,
				"key",
				"value=value=value");
	ck_assert_int_ge(r, 0);
	r = wpas_send(client, m, 0);
	ck_assert_int_ge(r, 0);

	r = sd_event_loop(event);
	ck_assert_int_ge(r, 0);

	wpas_message_unref(m);

	stop_test_client();
}
END_TEST

TEST_DEFINE_CASE(run)
	TEST(run_invalid_msg)
	TEST(run_msg)
	TEST(run_send)
	TEST(run_parse)
TEST_END_CASE

TEST_DEFINE(
	TEST_SUITE(wpa,
		TEST_CASE(bus),
		TEST_CASE(msg),
		TEST_CASE(run),
		TEST_END
	)
)
