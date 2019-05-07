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

#include <array>
#include <list>
#include <memory>
#include <string>

#include <boost/test/unit_test.hpp>

#include "zmqpp/context.hpp"
#include "zmqpp/socket.hpp"
#include "zmqpp/message.hpp"
#include "zmqpp/signal.hpp"

BOOST_AUTO_TEST_SUITE( socket )

const int bubble_poll_timeout = 1;
const int max_poll_timeout = 100;

void bubble_subscriptions(zmqpp::socket& socket)
{
	zmq_pollitem_t item = { socket, 0, ZMQ_POLLIN, 0 };
	int result = zmq_poll(&item, 1, bubble_poll_timeout);
	BOOST_REQUIRE_MESSAGE(0 == result, "polling command failed to timeout during subscription bubble");
}

void wait_for_socket(zmqpp::socket& socket)
{
	zmq_pollitem_t item = { socket, 0, ZMQ_POLLIN, 0 };
	int result = zmq_poll(&item, 1, max_poll_timeout);
	BOOST_REQUIRE_MESSAGE(result >= 0, "polling command returned without expected value: " << zmq_strerror(zmq_errno()));
	BOOST_REQUIRE_MESSAGE(0 != result, "polling command returned with timeout after " << max_poll_timeout << " milliseconds");
	BOOST_REQUIRE_MESSAGE(1 == result, "polling command claims " << result << " sockets have events but we only gave it one");
	BOOST_REQUIRE_MESSAGE(item.revents & ZMQ_POLLIN, "events do not match expected POLLIN event: " << item.revents);
}

BOOST_AUTO_TEST_CASE( socket_creation )
{
	zmqpp::context context;
	zmqpp::socket socket(context, zmqpp::socket_type::pull);
}

BOOST_AUTO_TEST_CASE( socket_creation_bad_type )
{
	zmqpp::context context;
	BOOST_CHECK_THROW(zmqpp::socket socket(context, static_cast<zmqpp::socket_type>(-1)), zmqpp::zmq_internal_exception);
}

BOOST_AUTO_TEST_CASE( valid_socket )
{
	zmqpp::context context;
	zmqpp::socket socket(context, zmqpp::socket_type::pull);
	socket.bind("inproc://test");

	zmqpp::message msg_in;
	BOOST_CHECK(!socket.receive(msg_in, true));
}

BOOST_AUTO_TEST_CASE( valid_move_supporting )
{
	zmqpp::context context;

	zmqpp::socket original(context, zmqpp::socket_type::pull);
	original.bind("inproc://test");

	zmqpp::socket clone(std::move(original));

	zmqpp::message msg_in;
	BOOST_CHECK(!clone.receive(msg_in, true));
}

BOOST_AUTO_TEST_CASE( simple_pull_push )
{
	zmqpp::context context;

	zmqpp::socket puller(context, zmqpp::socket_type::pull);
	puller.bind("inproc://test");

	zmqpp::socket pusher(context, zmqpp::socket_type::push);
	pusher.connect("inproc://test");

	std::string msg_txt("hello world!");
	zmqpp::message msg_out(msg_txt);

	BOOST_CHECK(pusher.send(msg_out));

	wait_for_socket(puller);

	zmqpp::message msg_in;
	BOOST_CHECK(puller.receive(msg_in));

	BOOST_CHECK_EQUAL(1, msg_in.parts());
	BOOST_CHECK_EQUAL(msg_txt, msg_in.get<std::string>(0));
}

BOOST_AUTO_TEST_CASE( simple_receive_raw )
{
	zmqpp::context context;
	char buf[64];
	size_t len = sizeof(buf);

	zmqpp::socket puller(context, zmqpp::socket_type::pull);
	puller.bind("inproc://test");

	zmqpp::socket pusher(context, zmqpp::socket_type::push);
	pusher.connect("inproc://test");

	std::string msg_txt("hello world!");
	zmqpp::message msg_out(msg_txt);

	BOOST_CHECK(pusher.send(msg_out));

	wait_for_socket(puller);

	BOOST_CHECK(puller.receive_raw(buf, len));

	std::string msg_in_txt(buf, len);
	BOOST_CHECK_EQUAL(msg_txt, msg_in_txt);
	BOOST_CHECK(!puller.has_more_parts());
}

BOOST_AUTO_TEST_CASE( simple_receive_raw_short_buf )
{
	zmqpp::context context;
	char buf[64];
	size_t len = 5;

	memset(buf, 0xee, sizeof(buf)); // init

	zmqpp::socket puller(context, zmqpp::socket_type::pull);
	puller.bind("inproc://test");

	zmqpp::socket pusher(context, zmqpp::socket_type::push);
	pusher.connect("inproc://test");

	zmqpp::message msg_out("hello world!");
	BOOST_CHECK(pusher.send(msg_out));

	wait_for_socket(puller);

	BOOST_CHECK(puller.receive_raw(buf, len));

	BOOST_CHECK_EQUAL(5, len);
	BOOST_CHECK_EQUAL(0xee, buf[5] & 0xff);
	BOOST_CHECK_EQUAL(0xee, buf[6] & 0xff);

	std::string message(buf, len);
	BOOST_CHECK_EQUAL("hello", message);
	BOOST_CHECK(!puller.has_more_parts());
}

BOOST_AUTO_TEST_CASE( multipart_pair )
{
	zmqpp::context context;

	zmqpp::socket alpha(context, zmqpp::socket_type::pair);
	alpha.bind("inproc://test");

	zmqpp::socket omega(context, zmqpp::socket_type::pair);
	omega.connect("inproc://test");

	zmqpp::message msg_out("hello");
	msg_out << "world" << "!";
	BOOST_CHECK(alpha.send(msg_out));

	wait_for_socket(omega);

	zmqpp::message msg_in;
	BOOST_CHECK(omega.receive(msg_in));
	BOOST_REQUIRE(msg_in.parts() > 1);
	BOOST_CHECK_EQUAL("hello", msg_in.get<std::string>(0));
	BOOST_CHECK_EQUAL("world", msg_in.get<std::string>(1));
	BOOST_CHECK_EQUAL("!", msg_in.get<std::string>(2));
}

BOOST_AUTO_TEST_CASE( subscribe_via_option )
{
	zmqpp::context context;

	zmqpp::socket publisher(context, zmqpp::socket_type::publish);
	publisher.bind("inproc://test");

	zmqpp::socket subscriber(context, zmqpp::socket_type::subscribe);
	subscriber.connect("inproc://test");
	subscriber.set(zmqpp::socket_option::subscribe, "watch1");

	zmqpp::message msg1("watch0");
	msg1 << "contents0";
	zmqpp::message msg2("watch1");
	msg2 << "contents1";
	BOOST_CHECK(publisher.send(msg1));
	BOOST_CHECK(publisher.send(msg2));

	wait_for_socket(subscriber);

	zmqpp::message msg_in;
	BOOST_CHECK(subscriber.receive(msg_in));
	BOOST_REQUIRE(msg_in.parts() > 1);
	BOOST_CHECK_EQUAL("watch1", msg_in.get<std::string>(0));
	BOOST_CHECK_EQUAL("contents1", msg_in.get<std::string>(1));
}

BOOST_AUTO_TEST_CASE( subscribe_helpers )
{
	zmqpp::context context;

	zmqpp::socket publisher(context, zmqpp::socket_type::publish);
	publisher.bind("inproc://test");

	zmqpp::socket subscriber(context, zmqpp::socket_type::subscribe);
	subscriber.connect("inproc://test");
	subscriber.subscribe("watch1");
	subscriber.subscribe("watch2");

	zmqpp::message msg1("watch0");
	msg1 << "contents0";
	zmqpp::message msg2("watch1");
	msg2 << "contents1";
	zmqpp::message msg3("watch2");
	msg3 << "contents2";
	zmqpp::message msg4("watch3");
	msg4 << "contents3";

	zmqpp::message msg1_b = msg1.copy();
	zmqpp::message msg2_b = msg2.copy();
	zmqpp::message msg3_b = msg3.copy();

	BOOST_CHECK(publisher.send(msg1));
	BOOST_CHECK(publisher.send(msg2));
	BOOST_CHECK(publisher.send(msg3));
	BOOST_CHECK(publisher.send(msg4));

	wait_for_socket(subscriber);

	zmqpp::message msg_in;
	BOOST_CHECK(subscriber.receive(msg_in));
	BOOST_REQUIRE(msg_in.parts() > 1);
	BOOST_CHECK_EQUAL("watch1", msg_in.get<std::string>(0));
	BOOST_CHECK_EQUAL("contents1", msg_in.get<std::string>(1));

	wait_for_socket(subscriber);

	BOOST_CHECK(subscriber.receive(msg_in));
	BOOST_REQUIRE(msg_in.parts() > 1);
	BOOST_CHECK_EQUAL("watch2", msg_in.get<std::string>(0));
	BOOST_CHECK_EQUAL("contents2", msg_in.get<std::string>(1));

	subscriber.unsubscribe("watch1");
	bubble_subscriptions(subscriber);

	BOOST_CHECK(publisher.send(msg1_b));
	BOOST_CHECK(publisher.send(msg2_b));
	BOOST_CHECK(publisher.send(msg3_b));

	wait_for_socket(subscriber);

	BOOST_CHECK(subscriber.receive(msg_in));
	BOOST_REQUIRE(msg_in.parts() > 1);
	BOOST_CHECK_EQUAL("watch2", msg_in.get<std::string>(0));
	BOOST_CHECK_EQUAL("contents2", msg_in.get<std::string>(1));
}

BOOST_AUTO_TEST_CASE( subscribe_helpers_multitopic_method )
{
	std::list<std::string> topics = { "watch1", "watch2" };

	zmqpp::context context;

	zmqpp::socket publisher(context, zmqpp::socket_type::publish);
	publisher.bind("inproc://test");

	zmqpp::socket subscriber(context, zmqpp::socket_type::subscribe);
	subscriber.connect("inproc://test");
	subscriber.subscribe(topics.begin(), topics.end());

	zmqpp::message msg1("watch0");
	msg1 << "contents0";
	zmqpp::message msg2("watch1");
	msg2 << "contents1";
	zmqpp::message msg3("watch2");
	msg3 << "contents2";
	zmqpp::message msg4("watch3");
	msg4 << "contents3";
	BOOST_CHECK(publisher.send(msg1));
	BOOST_CHECK(publisher.send(msg2));
	BOOST_CHECK(publisher.send(msg3));
	BOOST_CHECK(publisher.send(msg4));

	wait_for_socket(subscriber);

	zmqpp::message msg_in;
	BOOST_CHECK(subscriber.receive(msg_in));
	BOOST_REQUIRE(msg_in.parts() > 1);
	BOOST_CHECK_EQUAL("watch1", msg_in.get<std::string>(0));
	BOOST_CHECK_EQUAL("contents1", msg_in.get<std::string>(1));

	wait_for_socket(subscriber);

	BOOST_CHECK(subscriber.receive(msg_in));
	BOOST_REQUIRE(msg_in.parts() > 1);
	BOOST_CHECK_EQUAL("watch2", msg_in.get<std::string>(0));
	BOOST_CHECK_EQUAL("contents2", msg_in.get<std::string>(1));
}

BOOST_AUTO_TEST_CASE( sending_messages )
{
	zmqpp::context context;

	zmqpp::socket pusher(context, zmqpp::socket_type::push);
	pusher.bind("inproc://test");

	zmqpp::socket puller(context, zmqpp::socket_type::pull);
	puller.connect("inproc://test");

	std::string part("another world");
	zmqpp::message msg_out;

	msg_out.add("hello world!");
	msg_out.add(part);

	pusher.send(msg_out);
	BOOST_CHECK_EQUAL(0, msg_out.parts());

	wait_for_socket(puller);

	zmqpp::message msg_in;
	BOOST_CHECK(puller.receive(msg_in));
	BOOST_REQUIRE_EQUAL(2, msg_in.parts());
	BOOST_CHECK_EQUAL("hello world!", msg_in.get<std::string>(0));
	BOOST_CHECK_EQUAL("another world", msg_in.get<std::string>(1));
}

BOOST_AUTO_TEST_CASE( receiving_messages )
{
	zmqpp::context context;

	zmqpp::socket pusher(context, zmqpp::socket_type::push);
	pusher.bind("inproc://test");

	zmqpp::socket puller(context, zmqpp::socket_type::pull);
	puller.connect("inproc://test");

	zmqpp::message msg_out;
	std::string part("another world");

	msg_out.add("hello world!");
	msg_out.add(part);

	pusher.send(msg_out);
	BOOST_CHECK_EQUAL(0, msg_out.parts());

	wait_for_socket(puller);

	zmqpp::message msg_in;
	BOOST_CHECK(puller.receive(msg_in));
	BOOST_REQUIRE_EQUAL(2, msg_in.parts());
	BOOST_CHECK_EQUAL("hello world!", msg_in.get<std::string>(0));
	BOOST_CHECK_EQUAL("another world", msg_in.get<std::string>(1));
}

BOOST_AUTO_TEST_CASE( receive_over_old_messages )
{
	zmqpp::context context;

	zmqpp::socket pusher( context, zmqpp::socket_type::push );
	pusher.bind( "inproc://test" );

	zmqpp::socket puller( context, zmqpp::socket_type::pull );
	puller.connect( "inproc://test");

	// remember, we are testing receiving messages sent with old send() method!
	pusher.send( "first message" );
	pusher.send( "second message" );

	wait_for_socket( puller );

	zmqpp::message msg_in;
	BOOST_CHECK( puller.receive( msg_in ) );
	BOOST_REQUIRE_EQUAL( 1, msg_in.parts() );
	BOOST_CHECK_EQUAL( "first message", msg_in.get<std::string>(0) );

	BOOST_CHECK( puller.receive( msg_in ) );
	BOOST_REQUIRE_EQUAL( 1, msg_in.parts() );
	BOOST_CHECK_EQUAL( "second message", msg_in.get<std::string>(0) );
}

BOOST_AUTO_TEST_CASE( cleanup_safe_with_pending_data )
{
	zmqpp::context context;

	zmqpp::socket pusher(context, zmqpp::socket_type::push);
	pusher.bind("inproc://test");

	zmqpp::socket puller(context, zmqpp::socket_type::pull);
	puller.connect("inproc://test");

	zmqpp::message msg_out;
	std::string part("another world");

	msg_out.add("hello world!");
	msg_out.add(part);

	pusher.send(msg_out);
	BOOST_CHECK_EQUAL(0, msg_out.parts());
}

BOOST_AUTO_TEST_CASE( multitarget_puller )
{
	std::vector<std::string> endpoints = { "inproc://test1", "inproc://test2" };

	zmqpp::context context;

	zmqpp::socket pusher1(context, zmqpp::socket_type::push);
	pusher1.bind(endpoints[0]);

	zmqpp::socket pusher2(context, zmqpp::socket_type::push);
	pusher2.bind(endpoints[1]);

	zmqpp::socket puller(context, zmqpp::socket_type::pull);
	puller.connect(endpoints.begin(), endpoints.end());

	zmqpp::message msg1("hello world!");
	zmqpp::message msg2("a test message");
	BOOST_CHECK(pusher1.send(msg1));
	BOOST_CHECK(pusher2.send(msg2));

	wait_for_socket(puller);

	zmqpp::message msg_in;
	BOOST_CHECK( puller.receive( msg_in ) );
	BOOST_REQUIRE_EQUAL( 1, msg_in.parts() );
	BOOST_CHECK_EQUAL( "hello world!", msg_in.get<std::string>(0) );

	BOOST_CHECK( puller.receive( msg_in ) );
	BOOST_REQUIRE_EQUAL( 1, msg_in.parts() );
	BOOST_CHECK_EQUAL( "a test message", msg_in.get<std::string>(0) );
}


BOOST_AUTO_TEST_CASE( test_receive_send_signals )
{
    zmqpp::context ctx;
    zmqpp::socket p1(ctx, zmqpp::socket_type::pair);
    zmqpp::socket p2(ctx, zmqpp::socket_type::pair);

    p1.bind("inproc://test");
    p2.connect("inproc://test");

    p1.send(zmqpp::signal::test);
    p1.send("....");
    p1.send(zmqpp::signal::stop);

    zmqpp::signal s;
    std::string str;

    p2.receive(s);
    BOOST_CHECK_EQUAL(zmqpp::signal::test, s);
    p2.receive(str);
    p2.send(zmqpp::signal::test);
    p2.receive(s);
    BOOST_CHECK_EQUAL(zmqpp::signal::stop, s);

    p1.receive(s);
    BOOST_CHECK_EQUAL(zmqpp::signal::test, s);
}

BOOST_AUTO_TEST_CASE( test_wait )
{
    zmqpp::context ctx;
    zmqpp::socket p1(ctx, zmqpp::socket_type::pair);
    zmqpp::socket p2(ctx, zmqpp::socket_type::pair);

    p1.bind("inproc://test");
    p2.connect("inproc://test");

    p1.send(zmqpp::signal::test);
    p1.send("....");
    p1.send("___");
    p1.send(zmqpp::signal::stop);

    // test wait(): the non signal message must be discarded.
    BOOST_CHECK_EQUAL(zmqpp::signal::test, p2.wait());
    BOOST_CHECK_EQUAL(zmqpp::signal::stop, p2.wait());
}

BOOST_AUTO_TEST_CASE( test_signal_block_noblock )
{
    zmqpp::context ctx;
    zmqpp::socket p1(ctx, zmqpp::socket_type::pair);
    zmqpp::socket p2(ctx, zmqpp::socket_type::pair);

    p1.bind("inproc://test");

    BOOST_CHECK_EQUAL(false, p1.send(zmqpp::signal::test, true)); //noblock
    //p1.send(zmqpp::signal::test); // would block indefinitely
    p2.connect("inproc://test");

    zmqpp::signal sig;
    BOOST_CHECK_EQUAL(false, p1.receive(sig, true)); //noblock
    p1.send(zmqpp::signal::test);
    BOOST_CHECK_EQUAL(true, p2.receive(sig, true)); //noblock
}

#ifndef TRAVIS_CI_BUILD //do not run when building on travis-ci (this cause oom error and kill the test process)
BOOST_AUTO_TEST_CASE( sending_large_messages_string )
{
	zmqpp::context context;

	zmqpp::socket pusher(context, zmqpp::socket_type::push);
	pusher.bind("inproc://test");

	zmqpp::socket puller(context, zmqpp::socket_type::pull);
	puller.connect("inproc://test");

	std::string message;
    const size_t bytes_to_send = static_cast<size_t>(1024 * 1024 * 1024);
    message.reserve(bytes_to_send);
    for (size_t i = 0; i < bytes_to_send; i++)
    {
        message.push_back('A' + (i % 26));
    }

	BOOST_CHECK(pusher.send(message));

	zmq_pollitem_t item = { puller, 0, ZMQ_POLLIN, 0 };
    const int poll_timeout = 1000000;
	int result = zmq_poll(&item, 1, poll_timeout);
	BOOST_REQUIRE_MESSAGE(result >= 0, "polling command returned without expected value: " << zmq_strerror(zmq_errno()));
	BOOST_REQUIRE_MESSAGE(0 != result, "polling command returned with timeout after " << poll_timeout << " milliseconds");
	BOOST_REQUIRE_MESSAGE(1 == result, "polling command claims " << result << " sockets have events but we only gave it one");
	BOOST_REQUIRE_MESSAGE(item.revents & ZMQ_POLLIN, "events do not match expected POLLIN event: " << item.revents);

	zmqpp::message msg_in;

	BOOST_CHECK(puller.receive(msg_in));
	BOOST_CHECK_EQUAL(0, message.compare(msg_in.get<std::string>(0)));
}
#endif

#if (ZMQ_VERSION_MAJOR >= 4)
BOOST_AUTO_TEST_CASE( test_simple_monitor )
{
    zmqpp::context ctx;
    zmqpp::socket server(ctx, zmqpp::socket_type::push);
    server.bind("tcp://*:55443");

    server.monitor("inproc://test_monitor", zmqpp::event::all);

    zmqpp::socket monitor(ctx, zmqpp::socket_type::pair);
    monitor.connect("inproc://test_monitor");

    zmqpp::socket client(ctx, zmqpp::socket_type::pull);
    client.connect("tcp://localhost:55443");

    // Receive event accepted
    {
        zmqpp::message msg_in;
        BOOST_CHECK( monitor.receive( msg_in ) );
        BOOST_REQUIRE_EQUAL(2, msg_in.parts());

#if (ZMQ_VERSION_MINOR >= 1)
        const uint8_t *ptr = reinterpret_cast<const uint8_t *>(msg_in.raw_data(0));
        uint16_t ev = *(reinterpret_cast<const uint16_t *>(ptr));
        // uint32_t value = *(reinterpret_cast<const uint32_t *>(ptr + 2));
        BOOST_CHECK_EQUAL( zmqpp::event::accepted, ev );
        BOOST_CHECK_EQUAL("tcp://0.0.0.0:55443", msg_in.get<std::string>(1));
        // value is the underlying file descriptor. we cannot check its value against anything meaningful
#else
        zmq_event_t const* event = static_cast<zmq_event_t const*>( msg_in.raw_data(0) );
        BOOST_CHECK_EQUAL( zmqpp::event::accepted, event->event );
        BOOST_CHECK_EQUAL( 0, event->value );
        BOOST_CHECK_EQUAL("tcp://0.0.0.0:55443", msg_in.get<std::string>(1));
#endif
    }

    server.unmonitor();

    zmqpp::socket client2(ctx, zmqpp::socket_type::pull);
    client2.connect("tcp://localhost:55443");

    // Receive event monitor_stopped
    {
        zmqpp::message_t message;
        BOOST_CHECK( monitor.receive( message ) );
        BOOST_REQUIRE_EQUAL(2, message.parts());

#if (ZMQ_VERSION_MINOR >= 1)
        const uint8_t *ptr = reinterpret_cast<const uint8_t *>(message.raw_data(0));
        uint16_t ev = *(reinterpret_cast<const uint16_t *>(ptr));
        // uint32_t value = *(reinterpret_cast<const uint32_t *>(ptr + 2));
        BOOST_CHECK_EQUAL( zmqpp::event::monitor_stopped, ev );
#else
        zmq_event_t const* event = static_cast<zmq_event_t const*>( message.raw_data(0) );
        BOOST_CHECK_EQUAL( zmqpp::event::monitor_stopped, event->event );
#endif
    }

    // Receive nothing else
    {
        zmqpp::message msg_in;
        BOOST_CHECK( !monitor.receive( msg_in, true ) );
    }

}
#endif

BOOST_AUTO_TEST_SUITE_END()
