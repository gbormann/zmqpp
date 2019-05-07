/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file is part of zmqpp.
 * Copyright (c) 2011-2015 Contributors as noted in the AUTHORS file.
 */

/*
 *  Created on: 9 Aug 2011
 *      Author: @benjamg
 */

#include <boost/test/unit_test.hpp>

#include <cstdlib>
#include <array>

#include <boost/lexical_cast.hpp>

#include "zmqpp/exception.hpp"
#include "zmqpp/message.hpp"

BOOST_AUTO_TEST_SUITE( message )

BOOST_AUTO_TEST_CASE( initialising )
{
	zmqpp::message msg;

	BOOST_CHECK_EQUAL(0, msg.parts());
}

BOOST_AUTO_TEST_CASE( throws_exception_reading_invalid_part )
{
	zmqpp::message msg;

	BOOST_CHECK_THROW(msg.get<int>(0), zmqpp::exception);
}

BOOST_AUTO_TEST_CASE( move_construction_supporting )
{
	std::string test;
	zmqpp::message first;
	first.add("string");
	BOOST_CHECK_EQUAL(1, first.parts());

	zmqpp::message second( std::move(first) );
	BOOST_CHECK_EQUAL(1, second.parts());
	BOOST_CHECK_EQUAL(0, first.parts());

	// read cursor
	zmqpp::message boap;
	boap.add("string");
	boap.add("string2");

	boap >> test;
	BOOST_CHECK_EQUAL("string", test);

	zmqpp::message boap2( std::move(boap));
	boap2 >> test;
	BOOST_CHECK_EQUAL("string2", test);
}

BOOST_AUTO_TEST_CASE( move_construction_supporting2 )
{
	zmqpp::message first("string");
	first << "string2";

	BOOST_CHECK_EQUAL(2, first.parts());

	std::string test;
	first >> test;

	zmqpp::message second(std::move(first));
	first.add("str");
	first >> test;
}

BOOST_AUTO_TEST_CASE( move_assignment_supporting )
{
 	zmqpp::message first("string");

 	zmqpp::message second("blah");

	second = std::move(first);

 	BOOST_CHECK_EQUAL(1, second.parts());
 	BOOST_CHECK_EQUAL(0, first.read_cursor());

	zmqpp::message boap("string");
	boap << "string2";
	BOOST_CHECK_EQUAL(2, boap.parts());

	std::string test;
	boap >> test;
	BOOST_CHECK_EQUAL("string", test);

	zmqpp::message boap2;
	boap2 = std::move(boap); // we're testing assignment!
	boap2 >> test;
	BOOST_CHECK_EQUAL("string2", test);
}

BOOST_AUTO_TEST_CASE( move_assignment_supporting2 )
{
	std::string test;
	zmqpp::message first;
	first.add("string");
	first.add("string2");
	BOOST_CHECK_EQUAL(2, first.parts());
	first >> test;
	BOOST_CHECK_EQUAL("string", test);

	zmqpp::message second;
	second = std::move(first);
	first.add("str");
	first >> test;
	BOOST_CHECK_EQUAL("str", test);
}

BOOST_AUTO_TEST_CASE( copyable )
{
	zmqpp::message second;

	{
		zmqpp::message first("string");
		BOOST_CHECK_EQUAL(1, first.parts());

		second = first.copy();
	}

	BOOST_REQUIRE_EQUAL(1, second.parts());
	BOOST_CHECK_EQUAL(strlen("string"), second.size(0));
	BOOST_CHECK_EQUAL("string", second.get<std::string>(0));
}

#ifndef ZMQPP_IGNORE_LAMBDA_FUNCTION_TESTS
BOOST_AUTO_TEST_CASE( move_part_heapwrapped )
{
	const std::string msg_txt("tests");
	const std::size_t data_size = 6;
	uint8_t* data = new uint8_t[data_size];
	memset(data, 0, data_size);
	memcpy(data, msg_txt.c_str(), msg_txt.size());

	bool called = false;
	auto release_func = [data, &called](void* val) {
		uint8_t* val_ptr = static_cast<uint8_t*>(val);
		BOOST_CHECK_EQUAL(val_ptr, data);
		delete[](val_ptr);
		called = true;
	};
	{
		zmqpp::message msg_out;
		msg_out.move(data, msg_txt.size(), release_func, true);

		BOOST_REQUIRE_EQUAL(1, msg_out.parts());
		BOOST_CHECK_EQUAL(msg_txt.size(), msg_out.size(0));
		BOOST_CHECK_EQUAL(msg_txt, msg_out.get<std::string>(0));
	}
	BOOST_CHECK(called);
}

BOOST_AUTO_TEST_CASE( move_part_automanaged )
{
	const std::string msg_txt("tests");
	const std::size_t data_size = 6;
	uint8_t* data = new uint8_t[data_size];
	memset(data, 0, data_size);
	memcpy(data, msg_txt.c_str(), msg_txt.size());

	bool called = false;
	// inside or outside scope, doesn't matter as long as it's declared before the consuming message :
	zmqpp::message::release_function lambda_fctor(
		[data, &called](void* val) {
			uint8_t* val_ptr = static_cast<uint8_t*>(val);
			BOOST_CHECK_EQUAL(val_ptr, data);
			delete[](val_ptr);
			called = true;
		}
	); // this defeats the practicality of lambdas though...
	{
		zmqpp::message msg_out;
		msg_out.move(data, msg_txt.size(), lambda_fctor, false);

		BOOST_REQUIRE_EQUAL(1, msg_out.parts());
		BOOST_CHECK_EQUAL(msg_txt.size(), msg_out.size(0));
		BOOST_CHECK_EQUAL(msg_txt, msg_out.get<std::string>(0));
	}

	BOOST_CHECK(called);
}
#endif

class move_part_releaser
{
public:
	move_part_releaser(uint8_t* const data)
	  : m_data(data), m_called(false) { }

	bool is_called() { return m_called; }
	void reset() { m_called = false; }

	void operator()(void* val) {
		uint8_t* val_ptr = static_cast<uint8_t*>(val);
		BOOST_CHECK_EQUAL(val_ptr, m_data);
		delete[](val_ptr);
		m_called = true;
	}

private:
	uint8_t* const m_data;
	bool m_called;
};

BOOST_AUTO_TEST_CASE( move_part_functor)
{
	const std::string msg_txt("tests");
	const std::size_t data_size = 6;
	uint8_t* data = new uint8_t[data_size];
	memset(data, 0, data_size);
	memcpy(data, msg_txt.c_str(), msg_txt.size());

	move_part_releaser release_func(data); // Looks like std::function<> needs an l-value=>all very verbose :-/
	// inside or outside scope, doesn't matter as long as it's declared before the consuming message :
	zmqpp::message::release_function fctor(release_func);
	{
		zmqpp::message msg_out;
		msg_out.move(data, msg_txt.size(), fctor, false);

		BOOST_REQUIRE_EQUAL(1, msg_out.parts());
		BOOST_CHECK_EQUAL(msg_txt.size(), msg_out.size(0));
		BOOST_CHECK_EQUAL(msg_txt, msg_out.get<std::string>(0));
	}
	BOOST_CHECK(fctor.target<move_part_releaser>()->is_called());
}

BOOST_AUTO_TEST_CASE( copy_part )
{
	std::string msg_text("tests");

	{ // Check survival of data after msg goes out of scope
		zmqpp::message msg_out;
		msg_out.add_raw(msg_text.c_str(), msg_text.size());

		BOOST_REQUIRE_EQUAL(1, msg_out.parts());
		BOOST_CHECK_EQUAL(strlen("tests"), msg_out.size(0));
		BOOST_CHECK_EQUAL("tests", msg_out.get<std::string>(0));
	}

	BOOST_CHECK_EQUAL("tests", msg_text);
}

BOOST_AUTO_TEST_CASE( add_const_void )
{
	std::string msg_text("tests");

	{
		zmqpp::message msg_out;
		msg_out.add_raw(static_cast<void const*>(msg_text.c_str()), msg_text.size());

		BOOST_REQUIRE_EQUAL(1, msg_out.parts());
		BOOST_CHECK_EQUAL(strlen("tests"), msg_out.size(0));
		BOOST_CHECK_EQUAL("tests", msg_out.get<std::string>(0));
	}

	BOOST_CHECK_EQUAL("tests", msg_text);
}

BOOST_AUTO_TEST_CASE( add_const_char_star )
{
	std::string msg_text("tests");

	{
		zmqpp::message msg_out;
		msg_out.add_raw(msg_text.c_str(), msg_text.size());

		BOOST_REQUIRE_EQUAL(1, msg_out.parts());
		BOOST_CHECK_EQUAL(strlen("tests"), msg_out.size(0));
		BOOST_CHECK_EQUAL("tests", msg_out.get<std::string>(0));
	}

	BOOST_CHECK_EQUAL("tests", msg_text);
}

BOOST_AUTO_TEST_CASE( add_char_literal_and_size_t )
{
	zmqpp::message msg_out;
	msg_out.add_raw("tests", strlen("tests"));

	BOOST_REQUIRE_EQUAL(1, msg_out.parts());
	BOOST_CHECK_EQUAL(strlen("tests"), msg_out.size(0));
	BOOST_CHECK_EQUAL("tests", msg_out.get<std::string>(0));
}

BOOST_AUTO_TEST_CASE( add_char_literal_and_number )
{
	const int meaning_of_life = 42;
	zmqpp::message msg_out;

	msg_out.add("tests", meaning_of_life);

	BOOST_REQUIRE_EQUAL(2, msg_out.parts());
	BOOST_CHECK_EQUAL(strlen("tests"), msg_out.size(0));
	BOOST_CHECK_EQUAL("tests", msg_out.get<std::string>(0));

	BOOST_REQUIRE_EQUAL(sizeof(int), msg_out.size(1));
	BOOST_CHECK_EQUAL(meaning_of_life, msg_out.get<int>(1));
}
BOOST_AUTO_TEST_CASE( add_number )
{
	const int nr_of_the_beast = 666;
	zmqpp::message msg_out;

	msg_out.add(nr_of_the_beast);

	BOOST_REQUIRE_EQUAL(1, msg_out.parts());
	BOOST_REQUIRE_EQUAL(sizeof(int), msg_out.size(0));
	BOOST_CHECK_EQUAL(nr_of_the_beast, msg_out.get<int>(0));
}

BOOST_AUTO_TEST_CASE( copy_part_string )
{
	std::string part("tests");

	{
		zmqpp::message msg_out;
		msg_out.add(part);

		BOOST_REQUIRE_EQUAL(1, msg_out.parts());
		BOOST_CHECK_EQUAL(strlen("tests"), msg_out.size(0));
		BOOST_CHECK_EQUAL("tests", msg_out.get<std::string>(0));
	}

	BOOST_CHECK_EQUAL("tests", part);
}

BOOST_AUTO_TEST_CASE( multi_part_message )
{
	std::string msg_txt1("this is the first part");
	std::string msg_txt2("some other content here");
	std::string msg_txt3("and finally");

	zmqpp::message msg_out;
	msg_out.add(msg_txt1);
	msg_out.add(msg_txt2);
	msg_out.add(msg_txt3);

	BOOST_REQUIRE_EQUAL(3, msg_out.parts());
	BOOST_CHECK_EQUAL(msg_txt1.size(), msg_out.size(0));
	BOOST_CHECK_EQUAL(msg_txt1, msg_out.get<std::string>(0));
	BOOST_CHECK_EQUAL(msg_txt2.size(), msg_out.size(1));
	BOOST_CHECK_EQUAL(msg_txt2, msg_out.get<std::string>(1));
	BOOST_CHECK_EQUAL(msg_txt3.size(), msg_out.size(2));
	BOOST_CHECK_EQUAL(msg_txt3, msg_out.get<std::string>(2));
}

BOOST_AUTO_TEST_CASE( stream_throws_exception )
{
	zmqpp::message msg_out;
	std::string part;

	BOOST_CHECK_THROW(msg_out >> part, zmqpp::exception);
}

BOOST_AUTO_TEST_CASE( stream_output_string )
{
	zmqpp::message msg_in;
	msg_in.add("part");

	std::string part;
	msg_in >> part;

	BOOST_CHECK_EQUAL("part", part);
}

BOOST_AUTO_TEST_CASE( stream_copy_input_c_string )
{
	zmqpp::message msg_out;
	msg_out << "test part";

	BOOST_REQUIRE_EQUAL(1, msg_out.parts());
	BOOST_CHECK_EQUAL(strlen("test part"), msg_out.size(0));
	BOOST_CHECK_EQUAL("test part", msg_out.get<std::string>(0));
}

BOOST_AUTO_TEST_CASE( stream_copy_input_string )
{
	const std::string part("test part");
	zmqpp::message msg_out;
	msg_out << part;

	BOOST_REQUIRE_EQUAL(1, msg_out.parts());
	BOOST_CHECK_EQUAL(part.size(), msg_out.size(0));
	BOOST_CHECK_EQUAL(part, msg_out.get<std::string>(0));
}

BOOST_AUTO_TEST_CASE( stream_multiple_parts )
{
	zmqpp::message msg;
	msg << "test part";
	msg << 42;

	BOOST_REQUIRE_EQUAL(2, msg.parts());

	std::string part1;
	uint32_t part2;

	msg >> part1;
	msg >> part2;

	BOOST_CHECK_EQUAL("test part", part1);
	BOOST_CHECK_EQUAL(42, part2);
}

BOOST_AUTO_TEST_CASE( output_stream_resetable )
{
	zmqpp::message msg;
	msg << "test part";

	std::string first;
	msg >> first;

	BOOST_CHECK_EQUAL("test part", first);

	msg.reset_read_cursor();

	std::string second;
	msg >> second;

	BOOST_CHECK_EQUAL("test part", second);
}

BOOST_AUTO_TEST_CASE( many_part_queue_check )
{
	std::array<std::string, 150> parts;
	for (std::size_t i = 0; i < parts.size(); ++i)
		parts[i] = ((i % 2) == 0) ?
			("message frame " + boost::lexical_cast<std::string>(i + 1))
			: ("this part is a much longer test frame, message frame " + boost::lexical_cast<std::string>(i + 1));

	zmqpp::message msg;
	for (std::size_t loop = 0; loop < parts.size(); ++loop)
	{
		msg << parts[loop];

		for( std::size_t i = 0; i <= loop; ++i )
			BOOST_REQUIRE_MESSAGE(
				parts[i].compare(msg.get<std::string>(i)) == 0,
				"invalid frame " << i << " on loop " << loop
				<< ": '" << msg.get<std::string>(i) << "' != '" << parts[i] << "'");
	}
}

BOOST_AUTO_TEST_CASE( reserve_zmq_frame )
{
	const std::string msg_txt("hello world");

	zmqpp::message msg_out;
	zmq_msg_t& raw = msg_out.raw_new_msg(msg_txt.size());
	void* data = zmq_msg_data( &raw );
	memcpy( data, msg_txt.c_str(), msg_txt.size() );

	BOOST_REQUIRE_EQUAL( 1, msg_out.parts() );
	BOOST_CHECK_EQUAL( msg_txt, msg_out.get<std::string>(0) );
}

BOOST_AUTO_TEST_CASE( push_end_of_frame_queue )
{
	std::array<std::string, 2> parts = {{
		"test frame 1",
		"a much much longer test frame 2 to go over the small message size limitation"
	}};

	zmqpp::message msg_out;
	msg_out.push_back( parts[0] );
	msg_out.push_back( parts[1] );

	BOOST_REQUIRE_EQUAL( parts.size(), msg_out.parts() );
	for( std::size_t i = 0; i < parts.size(); ++i )
	{
		BOOST_CHECK_EQUAL( parts[i].size(), msg_out.size(i) );
		BOOST_CHECK_EQUAL( parts[i], msg_out.get<std::string>(i) );
	}
}

BOOST_AUTO_TEST_CASE( pop_end_of_frame_queue )
{
	std::array<std::string, 3> parts = {{
		"a long test frame 1 to go over the small message size limitation",
		"another frame 2",
		"some final frame 3"
	}};

	zmqpp::message msg_out;
	msg_out << parts[0] << parts[1] << parts[2];

	BOOST_REQUIRE_EQUAL( parts.size(), msg_out.parts() );

	msg_out.pop_back();
	BOOST_REQUIRE_EQUAL( parts.size() - 1, msg_out.parts() );
	for( std::size_t i = 0; i < parts.size() - 1; ++i )
	{
		BOOST_CHECK_EQUAL( parts[i].size(), msg_out.size(i) );
		BOOST_CHECK_EQUAL( parts[i], msg_out.get<std::string>(i) );
	}
}

BOOST_AUTO_TEST_CASE( push_front_of_frame_queue )
{
	std::array<std::string, 3> parts = {{
		"a long test frame 1 to go over the small message size limitation",
		"another frame 2",
		"some final frame 3"
	}};

	zmqpp::message msg_out;
	msg_out << parts[1] << parts[2];

	BOOST_REQUIRE_EQUAL( parts.size() - 1, msg_out.parts() );

	msg_out.push_front( parts[0] );
	BOOST_REQUIRE_EQUAL( parts.size(), msg_out.parts() );
	for( std::size_t i = 0; i < parts.size(); ++i )
	{
		BOOST_CHECK_EQUAL( parts[i].size(), msg_out.size(i) );
		BOOST_CHECK_EQUAL( parts[i], msg_out.get<std::string>(i) );
	}
}

BOOST_AUTO_TEST_CASE( pop_front_of_frame_queue )
{
	std::array<std::string, 3> parts = {{
		"a long test frame 1 to go over the small message size limitation",
		"another frame 2",
		"some final frame 3"
	}};

	zmqpp::message msg_out;
	msg_out << parts[0] << parts[1] << parts[2];

	BOOST_REQUIRE_EQUAL( parts.size(), msg_out.parts() );

	msg_out.pop_front();
	BOOST_REQUIRE_EQUAL( parts.size() - 1, msg_out.parts() );
	for( std::size_t i = 0; i < parts.size() - 1; ++i )
	{
		BOOST_CHECK_EQUAL( parts[i + 1].size(), msg_out.size(i) );
		BOOST_CHECK_EQUAL( parts[i + 1], msg_out.get<std::string>(i) );
	}
}

BOOST_AUTO_TEST_CASE( add_const_part )
{
	std::string msg_str("tests");
	const char* msg_txt = msg_str.c_str();
	{
		zmqpp::message msg_out;
		msg_out.add_const(msg_txt, msg_str.size());

		BOOST_REQUIRE_EQUAL(1, msg_out.parts());
		BOOST_CHECK_EQUAL(msg_str.size(), msg_out.size(0));
		BOOST_CHECK_EQUAL(msg_str, msg_out.get<std::string>(0));
	}

	BOOST_CHECK_EQUAL("tests", msg_str);
}

BOOST_AUTO_TEST_CASE( add_nocopy )
{
    const std::string const_msg_txt("hello");
    std::string msg_txt("a_longer_hello");

    const char *const_data = const_msg_txt.c_str();
    uint8_t* data = new uint8_t[15];
    memcpy(data, msg_txt.c_str(), msg_txt.size());

    zmqpp::message msg;
    //msg.add_nocopy(const_data, 5); should trigger static assert.

    msg.add_nocopy_const(const_data, 5);
    msg.add_nocopy(data, 14);

    // This is what you MAY NOT do, even though it compiles fine.
    //char *raw = static_cast<char *>(zmq_msg_data(&msg.raw_msg(0)));
    //raw[0] = 'c'; // crash

    // However you can do that on non-const data.
    char *raw = static_cast<char *>(zmq_msg_data(&msg.raw_msg(1)));
    raw[0] = '_';

    BOOST_REQUIRE_EQUAL(2, msg.parts());
    BOOST_CHECK_EQUAL(5, msg.size(0));
    BOOST_CHECK_EQUAL(14, msg.size(1));
    BOOST_CHECK_EQUAL(const_msg_txt, msg.get<std::string>(0));
    msg_txt.at(0) = '_';
    BOOST_CHECK_EQUAL("__longer_hello", msg.get<std::string>(1)); // we changed 1 byte

    delete[] data;
}

BOOST_AUTO_TEST_CASE( remove )
{
    std::size_t partRemoved = 1;
    std::array<std::string, 3> parts = {{
        "a long test frame 1 to go over the small message size limitation",
        "another frame 2",
        "some final frame 3"
    }};

    zmqpp::message msg_out;
    msg_out << parts[0] << parts[1] << parts[2];

    BOOST_REQUIRE_EQUAL( parts.size(), msg_out.parts() );

    msg_out.remove(partRemoved);
    BOOST_REQUIRE_EQUAL( parts.size() - 1, msg_out.parts() );
    for( std::size_t i = 0; i < parts.size() - 1; ++i )
    {
        if (i == partRemoved)
            continue;

        BOOST_CHECK_EQUAL( parts[i].size(), msg_out.size(i) );
        BOOST_CHECK_EQUAL( parts[i], msg_out.get<std::string>(i) );
    }
}

BOOST_AUTO_TEST_SUITE_END()
