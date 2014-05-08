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
 * Test Helper
 * This header includes all kinds of helpers for testing. It tries to include
 * everything required and provides simple macros to avoid duplicating code in
 * each test. We try to keep tests as small as possible and move everything that
 * might be common here.
 *
 * We avoid sticking to our usual coding conventions (including headers in
 * source files, etc. ..) and instead make this the most convenient we can.
 */

#ifndef TEST_COMMON_H
#define TEST_COMMON_H

#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include <unistd.h>
#include "shl_dlist.h"
#include "shl_htable.h"
#include "shl_log.h"
#include "shl_macro.h"
#include "shl_util.h"

/* lower address-space is protected from user-allocation, so this is invalid */
#define TEST_INVALID_PTR ((void*)0x10)

#define TEST_DEFINE_CASE(_name)					\
	static TCase *test_create_case_##_name(void)		\
	{							\
		TCase *tc;					\
								\
		tc = tcase_create(#_name);			\

#define TEST(_name) tcase_add_test(tc, _name);

#define TEST_END_CASE						\
		return tc;					\
	}							\

#define TEST_END NULL

#define TEST_CASE(_name) test_create_case_##_name

static inline Suite *test_create_suite(const char *name, ...)
{
	Suite *s;
	va_list list;
	TCase *(*fn)(void);

	s = suite_create(name);

	va_start(list, name);
	while ((fn = va_arg(list, TCase *(*)(void))))
		suite_add_tcase(s, fn());
	va_end(list);

	return s;
}

#define TEST_SUITE(_name, ...) test_create_suite((#_name), ##__VA_ARGS__)

static inline int test_run_suite(Suite *s)
{
	int ret;
	SRunner *sr;

	sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	ret = srunner_ntests_failed(sr);
	srunner_free(sr);

	return ret;
}

#define TEST_DEFINE(_suite) \
	int main(int argc, char **argv) \
	{ \
		return test_run_suite(_suite); \
	}

#endif /* TEST_COMMON_H */
