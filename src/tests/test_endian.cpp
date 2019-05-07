/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file is part of zmqpp.
 * Copyright (c) 2011-2015 Contributors as noted in the AUTHORS file.
 */

/*
 *  Created on: 8 Aug 2011
 *      Author: @benjamg
 */

#include <boost/test/unit_test.hpp>

#include "zmqpp/endian.hpp"

BOOST_AUTO_TEST_SUITE( endian )

BOOST_AUTO_TEST_CASE( to_BE_64bit )
{
	uint64_t host = 0xdeadbeef10204080;
	uint64_t network = 0; // local place to dump bytes (instead of on byte stream)

	uint8_t* byte_ptr = reinterpret_cast<uint8_t*>(&network);
	zmqpp::to_be(host, byte_ptr);


	BOOST_CHECK_EQUAL(0xde, *byte_ptr++);
	BOOST_CHECK_EQUAL(0xad, *byte_ptr++);
	BOOST_CHECK_EQUAL(0xbe,	*byte_ptr++);
	BOOST_CHECK_EQUAL(0xef, *byte_ptr++);
	BOOST_CHECK_EQUAL(0x10,	*byte_ptr++);
	BOOST_CHECK_EQUAL(0x20,	*byte_ptr++);
	BOOST_CHECK_EQUAL(0x40,	*byte_ptr++);
	BOOST_CHECK_EQUAL(0x80,	*byte_ptr++);
}

BOOST_AUTO_TEST_CASE( roundtrippable_64bit )
{
	uint64_t host = 0xdeadbeef10204080;
	uint64_t network = 0; // local place to dump bytes (instead of on byte stream)

	uint8_t* byte_ptr = reinterpret_cast<uint8_t*>(&network);
	zmqpp::to_be(host, byte_ptr);

	BOOST_CHECK_EQUAL(host, zmqpp::from_be<uint64_t>(byte_ptr));
}

BOOST_AUTO_TEST_SUITE_END()
