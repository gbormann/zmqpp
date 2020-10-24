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
 *      Author: Ben Gray (@benjamg)
 */

#include <iostream>
#include <iomanip>

#include <algorithm>
#include <cassert>
#include <cstring>

#include "exception.hpp"
#include "message.hpp"

namespace zmqpp
{
message::message()
	: _read_cursor(0)
{
}

message::~message() { } // calls _parts default destructor

size_t message::parts() const { return _parts.size(); }

/*
 * The two const_casts in size and raw_data are a little bit hacky
 * but neither of these methods called this way actually modify data
 * so accurately represent the intent of these calls.
 */

size_t message::size(std::size_t part /* = 0 */) const
{
	if(part >= _parts.size())
	{
		throw exception("attempting to request a message part outside the valid range");
	}

	return _parts[part].size();
}

void const* message::raw_data(std::size_t part /* = 0 */) const
{
	if(part >= _parts.size())
	{
		throw exception("attempting to request a message part outside the valid range");
	}

	return _parts[part].data();
}

zmq_msg_t& message::raw_msg(std::size_t part /* = 0 */)
{
	if(part >= _parts.size())
	{
		throw exception("attempting to request a message part outside the valid range");
	}

	return _parts[part].msg();
}

zmq_msg_t& message::raw_new_msg(std::size_t reserve_data_size)
{
  if (0 == reserve_data_size)
	  _parts.emplace_back();
  else
	  _parts.emplace_back(reserve_data_size);

	return _parts.back().msg();
}

uint8_t* message::raw_new_data(std::size_t reserve_data_size)
{
	_parts.emplace_back(reserve_data_size);

	return _parts.back().bytes();
}

uint8_t* message::raw_new_data_front(std::size_t reserve_data_size)
{
	_parts.emplace_front(reserve_data_size);

	return _parts.front().bytes();
}

// Move operators will take ownership of message parts without copying
void message::move(void* content, std::size_t size,
                   const message::release_function& release,
                   bool managed_releaser)
{
	add_nocopy(content, size,
		(managed_releaser ? &release_callback : &release_only_callback),
		(managed_releaser ? new release_function(release) : const_cast<release_function*>(&release)));
}

void message::push_front(void const* content, std::size_t size)
  { _parts.emplace_front(content, size); }

void message::push_front(char const* c_string)
  { push_front(c_string, strlen(c_string)); }

void message::push_front(std::string const& string)
  { push_front(string.data(), string.size()); }

void message::pop_front()
  { _parts.pop_front(); } // advantage of std::deque

void message::push_back(void const* content, std::size_t data_size)
  {	add_raw( const_cast<void*>(content), data_size ); }

void message::pop_back()
  { _parts.pop_back(); }

void message::remove(std::size_t part)
  { _parts.erase( _parts.begin() + part ); }

message::message(message&& source) NOEXCEPT
{
	using std::swap;
	swap(*this, source);
}

message& message::operator=(message&& rhs) NOEXCEPT
{
	using std::swap;
	reset_read_cursor();
	swap(*this, rhs);
	return *this;
}

// TODO Track send status!
void message::copy(const message& source)
{
	_read_cursor = source._read_cursor;
	_parts.resize(source.parts());
	for (std::size_t ndx = 0; ndx < parts(); ++ndx)
		_parts[ndx] = source._parts[ndx].copy();
}

message message::copy() const
{
	message msg;
	msg.copy(*this);
	return msg;
}

// Used for internal tracking
void message::sent(std::size_t part)
{
	// sanity check
	assert(!_parts[part].is_sent());
	_parts[part].mark_sent();
}

// Note that these releasers are not thread safe, the only safety is provided by
// the socket class taking ownership so no updates can happen while zmq does its thing
// If used in a custom class this has to be dealt with.
//
// GB: It actually is thread-safe if the user-provided release
//     callback is reentrant!
void message::release_callback(void* data, void* hint)
{
	release_only_callback(data, hint);
	delete static_cast<release_function*>(hint);
}

void message::release_only_callback(void* data, void* hint)
{
	(*static_cast<release_function*>(hint))(data);
}

bool message::is_signal() const
{
	return parts() == 1
        	&& size(0) == sizeof(signal) 
        	&& static_cast<signal>(get<int64_t>(0) >> 8) == signal::header;
}

#if (ZMQ_VERSION_MAJOR == 4 && ZMQ_VERSION_MINOR >= 1)
bool message::get_property(const std::string &property, std::string &out)
{
	zmq_msg_t *zmq_raw_msg;
	try
	{
		zmq_raw_msg = &raw_msg();
	}
	catch (zmqpp::exception const&) // empty
	{
		return false;
	}

	const char *property_value = zmq_msg_gets(zmq_raw_msg, property.c_str());
	if (property_value == NULL)
	{
		// EINVAL is the only error code
		assert(errno == EINVAL);
		return false;
	}

	out = std::string(property_value);
	return true;
}
#endif

#if (ZMQ_VERSION_MAJOR >= 4) && ((ZMQ_VERSION_MAJOR >= 2) && ZMQ_BUILD_DRAFT_API)
bool message::set_group(const std::string& group)
{
	// Nothing was set
	if (_parts.empty())
		return false;

	// Use a loop to set, although in theory this message should not be multipart
	for (size_t i = 0; i < _parts.size(); i++)
	{
		if (zmq_msg_set_group(&raw_msg(i), group.c_str()) < 0)
			return false;
	}

	return true;
}
#endif
}
