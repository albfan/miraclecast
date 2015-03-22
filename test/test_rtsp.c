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
#include "rtsp.h"

static bool debug = false;

START_TEST(bus_invalid_ops)
{
	struct rtsp *bus;
	int r;

	r = rtsp_open(NULL, -1);
	ck_assert_int_lt(r, 0);
	r = rtsp_open(&bus, -1);
	ck_assert_int_lt(r, 0);
	r = rtsp_open(NULL, 0);
	ck_assert_int_lt(r, 0);

	rtsp_ref(NULL);
	rtsp_unref(NULL);

	ck_assert(rtsp_is_dead(NULL));

	r = rtsp_attach_event(NULL, NULL, 0);
	ck_assert_int_lt(r, 0);

	rtsp_detach_event(NULL);

	r = rtsp_add_match(NULL, NULL, NULL);
	ck_assert_int_lt(r, 0);

	rtsp_remove_match(NULL, NULL, NULL);

	r = rtsp_send(NULL, NULL);
	ck_assert_int_lt(r, 0);

	r = rtsp_call_async(NULL, NULL, NULL, NULL, 0, NULL);
	ck_assert_int_lt(r, 0);

	rtsp_call_async_cancel(NULL, 0);
}
END_TEST

static int bus_open_match_fn(struct rtsp *bus,
			     struct rtsp_message *m,
			     void *data)
{
	return 0;
}

START_TEST(bus_open)
{
	struct rtsp *bus;
	int r, fd;

	fd = dup(0);
	ck_assert_int_ge(fd, 0);

	r = rtsp_open(&bus, fd);
	ck_assert_int_ge(r, 0);

	ck_assert(!rtsp_is_dead(bus));

	rtsp_ref(bus);
	rtsp_unref(bus);

	ck_assert(!rtsp_is_dead(bus));

	r = rtsp_add_match(bus, NULL, NULL);
	ck_assert_int_lt(r, 0);

	rtsp_remove_match(bus, NULL, NULL);

	r = rtsp_add_match(bus, bus_open_match_fn, NULL);
	ck_assert_int_ge(r, 0);

	rtsp_remove_match(bus, bus_open_match_fn, NULL);

	r = rtsp_attach_event(bus, NULL, 0);
	ck_assert_int_ge(r, 0);

	rtsp_detach_event(bus);

	rtsp_unref(bus);

	/* rtsp takes ownership over @fd, verify that */

	ck_assert_int_lt(dup(fd), 0);

	/* try again with implicit detach during destruction */

	fd = dup(0);
	ck_assert_int_ge(fd, 0);

	r = rtsp_open(&bus, fd);
	ck_assert_int_ge(r, 0);

	r = rtsp_add_match(bus, bus_open_match_fn, NULL);
	ck_assert_int_ge(r, 0);

	r = rtsp_attach_event(bus, NULL, 0);
	ck_assert_int_ge(r, 0);

	rtsp_unref(bus);
}
END_TEST

TEST_DEFINE_CASE(bus)
	TEST(bus_invalid_ops)
	TEST(bus_open)
TEST_END_CASE

START_TEST(msg_new_invalid)
{
	char data[128] = { };
	struct rtsp *bus;
	struct rtsp_message *m;
	int r, fd;

	fd = dup(0);
	ck_assert_int_ge(fd, 0);
	r = rtsp_open(&bus, fd);
	ck_assert_int_ge(r, 0);

	/* request messages */

	m = TEST_INVALID_PTR;
	r = rtsp_message_new_request(NULL, &m, "method", "uri");
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);
	r = rtsp_message_new_request(bus, NULL, "method", "uri");
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);
	r = rtsp_message_new_request(bus, &m, "", "uri");
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);
	r = rtsp_message_new_request(bus, &m, NULL, "uri");
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);
	r = rtsp_message_new_request(bus, &m, "method", "");
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);
	r = rtsp_message_new_request(bus, &m, "method", NULL);
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);

	r = rtsp_message_new_request(bus, &m, "method", "uri");
	ck_assert_int_ge(r, 0);
	ck_assert(m != TEST_INVALID_PTR);
	rtsp_message_unref(m);

	/* reply-for messages */

	m = TEST_INVALID_PTR;
	r = rtsp_message_new_reply(NULL, &m, 1, 200, "OK");
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);
	r = rtsp_message_new_reply(bus, NULL, 1, 200, "OK");
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);
	r = rtsp_message_new_reply(bus, &m, 0, 200, "OK");
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);
	r = rtsp_message_new_reply(bus, &m, 1, RTSP_ANY_CODE, "OK");
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);

	r = rtsp_message_new_reply(bus, &m, 1, 200, "OK");
	ck_assert_int_ge(r, 0);
	ck_assert(m != TEST_INVALID_PTR);
	rtsp_message_unref(m);

	/* data messages */

	m = TEST_INVALID_PTR;
	r = rtsp_message_new_data(NULL, &m, 0, data, sizeof(data));
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);
	r = rtsp_message_new_data(bus, NULL, 0, data, sizeof(data));
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);
	r = rtsp_message_new_data(bus, &m, RTSP_ANY_CHANNEL, data, sizeof(data));
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);
	r = rtsp_message_new_data(bus, &m, 0, NULL, sizeof(data));
	ck_assert_int_lt(r, 0);
	ck_assert(m == TEST_INVALID_PTR);

	r = rtsp_message_new_data(bus, &m, 0, data, sizeof(data));
	ck_assert_int_ge(r, 0);
	ck_assert(m != TEST_INVALID_PTR);
	rtsp_message_unref(m);

	/* invalid ops */

	rtsp_message_ref(NULL);
	rtsp_message_unref(NULL);

	ck_assert_int_eq(rtsp_message_get_type(NULL), 0);
	ck_assert(!rtsp_message_get_method(NULL));
	ck_assert(!rtsp_message_get_uri(NULL));
	ck_assert_int_eq(rtsp_message_get_code(NULL), RTSP_ANY_CODE);
	ck_assert(!rtsp_message_get_phrase(NULL));
	ck_assert_int_eq(rtsp_message_get_channel(NULL), RTSP_ANY_CHANNEL);
	ck_assert(!rtsp_message_get_payload(NULL));
	ck_assert_int_eq(rtsp_message_get_payload_size(NULL), 0);

	ck_assert(!rtsp_message_is_request(NULL, NULL, NULL));
	ck_assert(!rtsp_message_is_reply(NULL, RTSP_ANY_CODE, NULL));
	ck_assert(!rtsp_message_is_data(NULL, RTSP_ANY_CHANNEL));

	ck_assert(!rtsp_message_get_bus(NULL));
	ck_assert(!rtsp_message_get_cookie(NULL));
	ck_assert(!rtsp_message_is_sealed(NULL));

	rtsp_unref(bus);
}
END_TEST

struct recipe {
	unsigned int type;

	union {
		struct {
			const char *method;
			const char *uri;
		} request;

		struct {
			uint64_t cookie;
			unsigned int code;
			const char *phrase;
		} reply;

		struct {
			unsigned int channel;
			void *payload;
			size_t size;
		} data;
	};

	const char *types;
	union {
		void *ptr;
		int32_t i32;
		uint32_t u32;
	} args[128];

	const char *raw;
	size_t rawlen;
	const void *equivalents[128];
	size_t eq_sizes[128];
};

static void verify_recipe(struct recipe *rec, struct rtsp_message *m)
{
	size_t i;
	int32_t i32;
	uint32_t u32;
	const char *str;
	char t;
	int r;
	bool in_header = false;

	ck_assert_int_eq(rtsp_message_get_type(m), rec->type);

	switch (rec->type) {
	case RTSP_MESSAGE_REQUEST:
		ck_assert_str_eq(rtsp_message_get_method(m),
				 rec->request.method);
		ck_assert_str_eq(rtsp_message_get_uri(m),
				 rec->request.uri);
		break;
	case RTSP_MESSAGE_REPLY:
		ck_assert_int_eq(rtsp_message_get_code(m), rec->reply.code);
		ck_assert_str_eq(rtsp_message_get_phrase(m), rec->reply.phrase);
		break;
	case RTSP_MESSAGE_DATA:
		ck_assert_int_eq(rtsp_message_get_channel(m),
				 rec->data.channel);
		ck_assert_int_eq(rtsp_message_get_payload_size(m),
				 rec->data.size);
		ck_assert(!memcmp(rtsp_message_get_payload(m),
				  rec->data.payload,
				  rec->data.size));
		break;
	default:
		ck_assert(false);
		abort();
	}

	for (i = 0; rec->types && rec->types[i]; ++i) {
		t = rec->types[i];
		switch (t) {
		case RTSP_TYPE_STRING:
			r = rtsp_message_read_basic(m, t, &str);
			ck_assert_int_ge(r, 0);
			ck_assert(!!str);
			ck_assert_str_eq(str, rec->args[i].ptr);
			break;
		case RTSP_TYPE_INT32:
			r = rtsp_message_read_basic(m, t, &i32);
			ck_assert_int_ge(r, 0);
			ck_assert_int_eq(i32, rec->args[i].i32);
			break;
		case RTSP_TYPE_UINT32:
			r = rtsp_message_read_basic(m, t, &u32);
			ck_assert_int_ge(r, 0);
			ck_assert_int_eq(u32, rec->args[i].u32);
			break;
		case RTSP_TYPE_RAW:
			/* we cannot read TYPE_RAW outside headers */
			if (in_header) {
				r = rtsp_message_read_basic(m, t, &str);
				ck_assert_int_ge(r, 0);
				ck_assert(!!str);
				ck_assert_str_eq(str, rec->args[i].ptr);
			}

			break;
		case RTSP_TYPE_HEADER_START:
			in_header = true;
			r = rtsp_message_read_basic(m, t, rec->args[i].ptr);
			ck_assert_int_ge(r, 0);
			break;
		case RTSP_TYPE_HEADER_END:
			in_header = false;
			/* fallthrough */
		case RTSP_TYPE_BODY_START:
		case RTSP_TYPE_BODY_END:
			r = rtsp_message_read_basic(m, t, NULL);
			ck_assert_int_ge(r, 0);
			break;
		default:
			ck_assert(false);
			abort();
		}
	}
}

static struct rtsp_message *create_from_recipe(struct rtsp *bus,
					       struct recipe *rec)
{
	struct rtsp_message *m;
	void *raw;
	size_t i, rawlen;
	char t;
	int r;

	ck_assert(!!rec);

	switch (rec->type) {
	case RTSP_MESSAGE_REQUEST:
		r = rtsp_message_new_request(bus,
					     &m,
					     rec->request.method,
					     rec->request.uri);
		ck_assert_int_ge(r, 0);
		break;
	case RTSP_MESSAGE_REPLY:
		r = rtsp_message_new_reply(bus,
					   &m,
					   rec->reply.cookie ? : 1,
					   rec->reply.code,
					   rec->reply.phrase);
		ck_assert_int_ge(r, 0);
		break;
	case RTSP_MESSAGE_DATA:
		r = rtsp_message_new_data(bus,
					  &m,
					  rec->data.channel,
					  rec->data.payload,
					  rec->data.size);
		ck_assert_int_ge(r, 0);
		break;
	default:
		ck_assert(false);
		abort();
	}

	for (i = 0; rec->types && rec->types[i]; ++i) {
		t = rec->types[i];
		switch (t) {
		case RTSP_TYPE_INT32:
			r = rtsp_message_append_basic(m,
						      t,
						      rec->args[i].i32);
			break;
		case RTSP_TYPE_UINT32:
			r = rtsp_message_append_basic(m,
						      t,
						      rec->args[i].u32);
			break;
		default:
			r = rtsp_message_append_basic(m,
						      t,
						      rec->args[i].ptr);
			break;
		}

		ck_assert_int_ge(r, 0);
	}

	r = rtsp_message_set_cookie(m, 1);
	ck_assert_int_ge(r, 0);
	r = rtsp_message_seal(m);
	ck_assert_int_ge(r, 0);

	/* compare to @raw */

	raw = rtsp_message_get_raw(m);
	ck_assert(!!raw);
	rawlen = rtsp_message_get_raw_size(m);

	if (debug)
		fprintf(stderr, "---------EXPECT---------\n%s\n----------GOT-----------\n%s\n-----------END----------\n", (char*)rec->raw, (char*)raw);

	ck_assert_int_eq(rawlen, rec->rawlen ? : strlen(rec->raw));
	ck_assert(!memcmp(raw, rec->raw, rawlen));

	return m;
}

static struct rtsp_message *create_from_recipe_and_verify(struct rtsp *bus,
							  struct recipe *rec)
{
	struct rtsp_message *m, *eq;
	const void *e;
	size_t i;
	int r;

	m = create_from_recipe(bus, rec);

	for (i = 0; i < SHL_ARRAY_LENGTH(rec->equivalents); ++i) {
		e = rec->equivalents[i];
		if (!e)
			break;

		r = rtsp_message_new_from_raw(bus,
					      &eq,
					      e,
					      rec->eq_sizes[i] ? : strlen(e));
		ck_assert_int_ge(r, 0);

		verify_recipe(rec, eq);

		rtsp_message_unref(eq);
	}

	return m;
}

static struct recipe recipes[] = {
	{
		.type = RTSP_MESSAGE_REQUEST,
		.request = { .method = "METHOD", .uri = "http://URI" },

		.raw = "METHOD http://URI RTSP/1.0\r\n"
		       "CSeq: 1\r\n"
		       "\r\n",

		.equivalents = {
			" METHOD  http://URI           RTSP/1.0 \r\n\r\n",
			" METHOD  http://URI           RTSP/1.0 \r\r",
			" METHOD  http://URI           RTSP/1.0 \n\n",
			" METHOD  http://URI           RTSP/1.0 \n\r\n",
			" METHOD  http://URI           RTSP/1.0 \n\r",
		},
	},
	{
		.type = RTSP_MESSAGE_REPLY,
		.reply = { .code = 200, .phrase = "OK" },

		.raw = "RTSP/1.0 200 OK\r\n"
		       "CSeq: 1\r\n"
		       "\r\n",

		.equivalents = {
			"  RTSP/1.0   200   OK  \r\n",
			"  RTSP/1.0   200   OK  ",
			"  RTSP/1.0   200   OK  \r",
		},
	},
	{
		.type = RTSP_MESSAGE_DATA,
		.data = { .channel = 5, .payload = "asdf", .size = 4 },

		.raw = "$\005\000\004asdf",
		.rawlen = 8,
	},
	{
		.type = RTSP_MESSAGE_REQUEST,
		.request = { .method = "METHOD", .uri = "http://URI" },
		.types = "<sui><&>&{<sui><&>&}",
		.args = {
			{ .ptr = "header1" },
			{ .ptr = "string" },
			{ .u32 = 10 },
			{ .i32 = -5 },
			{ },
			{ .ptr = "header2" },
			{ .ptr = "raw value" },
			{ },
			{ .ptr = "raw header 3 :as full line" },
			{ },
			{ .ptr = "body-header1" },
			{ .ptr = "body string" },
			{ .u32 = 10 },
			{ .i32 = -5 },
			{ },
			{ .ptr = "body-header2" },
			{ .ptr = "body raw value" },
			{ },
			{ .ptr = "body raw header 3 :as full line" },
			{ },
		},

		.raw = "METHOD http://URI RTSP/1.0\r\n"
		       "header1: string 10 -5\r\n"
		       "header2: raw value\r\n"
		       "raw header 3 :as full line\r\n"
		       "Content-Length: 98\r\n"
		       "Content-Type: text/parameters\r\n"
		       "CSeq: 1\r\n"
		       "\r\n"
		       "body-header1: \"body string\" 10 -5\r\n"
		       "body-header2: body raw value\r\n"
		       "body raw header 3 :as full line\r\n",

		.equivalents = {
			"METHOD http://URI RTSP/1.0\r\n"
			"header1: string 10 -5\r\n"
			"header2: raw value\r\n"
			"raw header 3 :as full line\r\n"
			"Content-Length: 98\r"
			"Content-Type: text/parameters\r\n"
			"\r"
			"body-header1: \"body string\" 10 -5\r\n"
			"body-header2: body raw value\r\n"
			"body raw header 3 :as full line\r\n",

			"METHOD http://URI RTSP/1.0\r\n"
			"header1: string 10 -5\r\n"
			"header2: raw value\r\n"
			"raw header 3 :as full line\r\n"
			"Content-Length: 98\n"
			"Content-Type: text/parameters\r\n"
			"\n"
			"body-header1: \"body string\" 10 -5\r\n"
			"body-header2: body raw value\r\n"
			"\n"
			"body raw header 3 :as full line\r\n",

			"METHOD http://URI RTSP/1.0\r\n"
			"   header1   : string 10 -5\r\n"
			"header2: raw value\r\n"
			"raw header 3 :as full line\r\n"
			"      Content-Length   :    98   \r"
			"Content-Type: text/parameters\r\n"
			"\r\n"
			"body-header1:     \"body string\"    10   -5\r\n"
			"\n\r"
			"   body-header2   :    body raw value  \r\n"
			"body raw header 3 :as full line\r\n",
		},
	},
};

START_TEST(msg_new)
{
	struct rtsp *bus;
	struct rtsp_message *m;
	size_t i;
	int r, fd;

	fd = dup(0);
	ck_assert_int_ge(fd, 0);
	r = rtsp_open(&bus, fd);
	ck_assert_int_ge(r, 0);

	for (i = 0; i < SHL_ARRAY_LENGTH(recipes); ++i) {
		m = create_from_recipe_and_verify(bus, &recipes[i]);
		rtsp_message_unref(m);
	}

	rtsp_unref(bus);
}
END_TEST

TEST_DEFINE_CASE(msg)
	TEST(msg_new_invalid)
	TEST(msg_new)
TEST_END_CASE

static struct rtsp *server, *client;
static sd_event *event;

static struct rtsp *start_test_client(void)
{
	int r, fds[2] = { };

	r = sd_event_default(&event);
	ck_assert_int_ge(r, 0);

	r = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
	ck_assert_int_ge(r, 0);
	ck_assert_int_ge(fds[0], 0);
	ck_assert_int_ge(fds[1], 0);

	r = rtsp_open(&server, fds[0]);
	ck_assert_int_ge(r, 0);

	r = rtsp_attach_event(server, event, 0);
	ck_assert_int_ge(r, 0);

	r = rtsp_open(&client, fds[1]);
	ck_assert_int_ge(r, 0);

	r = rtsp_attach_event(client, event, 0);
	ck_assert_int_ge(r, 0);

	return client;
}

static void stop_test_client(void)
{
	rtsp_unref(client);
	client = NULL;
	rtsp_unref(server);
	server = NULL;
	sd_event_unref(event);
	event = NULL;
}

static int match_recipe(struct rtsp *bus,
			struct rtsp_message *m,
			void *data)
{
	struct recipe **rec = data;

	ck_assert(!!rec);
	ck_assert(!!*rec);
	ck_assert(!!m);
	verify_recipe(*rec, m);
	*rec = NULL;

	return 0;
}

START_TEST(run_all)
{
	struct recipe *rec;
	struct rtsp_message *m;
	int r, i;

	start_test_client();

	r = rtsp_add_match(server, match_recipe, &rec);
	ck_assert_int_ge(r, 0);

	for (i = 0; i < SHL_ARRAY_LENGTH(recipes); ++i) {
		rec = &recipes[i];
		if (rec->type == RTSP_MESSAGE_REPLY)
			continue;

		m = create_from_recipe(client, rec);
		r = rtsp_send(client, m);
		ck_assert_int_ge(r, 0);

		do {
			r = sd_event_run(event, (uint64_t)-1);
		} while (rec);

		ck_assert_int_ge(r, 0);

		rtsp_message_unref(m);
	}

	stop_test_client();
}
END_TEST

TEST_DEFINE_CASE(run)
	TEST(run_all)
TEST_END_CASE

TEST_DEFINE(
	TEST_SUITE(rtsp,
		TEST_CASE(bus),
		TEST_CASE(msg),
		TEST_CASE(run),
		TEST_END
	)
)
