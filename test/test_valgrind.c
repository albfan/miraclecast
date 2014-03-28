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

/* Dummy which just leaks memory. Used to verify valgrind memcheck. */

#include "test_common.h"

START_TEST(test_valgrind)
{
	void *p;

	p = malloc(0x100);
	ck_assert(!!p);
}
END_TEST

TEST_DEFINE_CASE(misc)
	TEST(test_valgrind)
TEST_END_CASE

TEST_DEFINE(
	TEST_SUITE(valgrind,
		TEST_CASE(misc),
		TEST_END
	)
)
