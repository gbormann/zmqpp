/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file is part of zmqpp.
 * Copyright (c) 2011-2015 Contributors as noted in the AUTHORS file.
 */

/**
 * \file
 *
 * \date   9 Aug 2011
 * \author Ben Gray (\@benjamg)
 */

#ifndef ZMQPP_MESSAGE_HPP_
#define ZMQPP_MESSAGE_HPP_

#include <cassert>
#include <cstring>
#include <deque>
#include <functional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include <zmq.h>

#include "compatibility.hpp"
#include "endian.hpp"
#include "frame.hpp"
#include "signal.hpp"

// TODO Count down the read_cursor, or consume parts instead.

namespace zmqpp
{
/*
 * REMARK: The interface is a hodgepodge of styles, if not a complete mess.
 * Is it a consequence of too many cooks?
 *
 * It might be better to separate major styles into separate implementations.
 *
 * With the portable endiannes conversion, most of the composition and
 * extraction methods are one-liners, so it remains to be decided whether it is
 * worth factoring out common implementations into a base class or a CRTP-based
 * implementation (as long as it is not RT polymorphism!).
 */
/**
 * \brief a zmq message with optional multipart support
 *
 * A zmq message is made up of one or more parts which are sent together to
 * the target endpoints. zmq guarantees either the whole message or none
 * of the message will be delivered.
 */
class ZMQPP_EXPORT message
{
public:
	/**
	 * \brief callback to release user allocated data.
	 *
	 * The release function will be called on any void* moved part.
	 * It must be thread safe to the extent that the callback may occur on
	 * one of the context threads.
	 *
	 * The function called will be passed a single variable which is the
	 * pointer to the memory allocated.
	 */
	typedef std::function<void (void*)> release_function;

	message();
	~message();

	template <typename T, typename ...Args>
	message(T content, Args &&...args)
	  : message()
	{
  	
		add(content, std::forward<Args>(args)...);
	}

	size_t parts() const;
	size_t size(std::size_t part) const;

	// Warn: If a pointer type is requested the message (well zmq) still 'owns'
	// the data and will release it when the message object is freed on the sending
	// side. On the receiving side, we are responsible!!
	/**
	 * Access a message part of primitive type by index.
	 *
	 * Usage: double price = mkt_update.get<double>(1);
	 *
	 * The method throws an exception when requesting a conversion to a type that
	 * is not supported out of the box. If this happens, there are two ways
	 * around this: either you request a typed pointer or you provide a
	 * template specialisation.
	 *
	 * Alternatively, if you have no knowledge of the type, you could request a
	 * byte pointer or as a last resort a raw pointer using the raw_data
	 * interface.
	 *
	 * @param part - part index in multi-frame message
	 * @throws exception - when attempting to convert the part to an unsupported type.
	 *
	 * @see raw_data
	 */
	template<typename Type>
	Type get(std::size_t part) const
	{
		if (std::is_pointer<Type>::value)
			return static_cast<Type>(raw_data(part));
		throw exception("Attempting to convert a message part to an unknown type.");
	}

	// Warn: The message (well zmq) still 'owns' the data and will release it
	// when the message object is freed.
	template<typename Type>
	ZMQPP_DEPRECATED("Pointers are not transportable outside process address space. Please consider using object handles or a serialisation framework in combination with zmqpp.")
	void get(Type** content, std::size_t part) const
	{
		*content = get<Type*>(part);
	}

	template<int part=0, typename T, typename ...Args>
	void extract(T& next_content, Args&...args)
	{
		assert(part < parts());
		next_content = get<T>(part);
		extract<part+1>(args...);
	}

	template<int part=0, typename T>
	void extract(T& next_content)
	{
		assert(part < parts());
		next_content = get<T>(part);
	}

	/*
	 * REMARK: The performance advantage of no-copy through move is largely
	 * undone by the dynamic allocation. At high message rates with different
	 * allocators/pools, this might fragment the heap.
	 *
	 * If we insist on managed releasers, at least we can do better by caching
	 * the allocated object, either transparantly using the hash_code or by
	 * a registration process that returns a cookie per registered releaser.
	 *
	 * In any case, in low latency environments, we want to avoid unnecessary
	 * dynamic allocation. Hence the added possibility to pass a releaser
	 * as is (without breaking existing client code), leaving the guarantee of
	 * the releaser life cycle to overlap with the raw message life cycle
	 * to the caller.
	 */
	/**
	  * Move operators will take ownership of message parts without copying.
	  *
	  * @param content - a void* content pointer
	  * @param size
	  * @param release
	  * @param managed_releaser if <b>true</b> (default case), keep the
	  *					release_function alive by wrapping it in a dynamically allocated
	  *					copy until the raw msg data is released.
	  *													if <b>false</b>, it is assumed that the releaser
	  *         object's life cycle extends at least beyond the zmq raw message's.
	  * @note The underlying release function call wrapped by the std::function
	  * object obviously has to stay in scope throughout, regardless. However
	  * the unmanaged releaser version saves an allocation/deallocation per
	  * message _part_.
	  */
	  void move(void* content, std::size_t size,
	  		const release_function& release,
	  		bool managed_releaser = true);

	// Raw move data operation, useful with data structures more than anything else.
	/**
	 * Move operation to move typed objects allocated with default 'new.'
	 * The library takes ownership.
	 *
	 * If combined with a serialiser/deserialiser such as Google Protocol Buffers
	 * this allows a powerful way to transfer larger data structures.
	 */
	template<typename Object>
	void move(Object *content)
	{
		add_nocopy(content, sizeof(Object), &deleter_callback<Object>);
	}

	// Copy operators will take copies of any data
	template<typename Type, typename ...Args>
	void add(Type content, Args &&...args)
	{
		*this << content;
		add(std::forward<Args>(args)...);
	}

	template<typename Type>
	void add(Type content)
	{
		*this << content;
	}

	// Copy operators will take copies of any data with a given size
	template<typename Type>
	void add_raw(const Type* content, std::size_t data_size = sizeof(Type))
	{
		_parts.emplace_back(content, data_size);
	}

	// Use exact data part, neither zmqpp nor 0mq will copy, alter or delete
	// this data. It must remain valid for at least the lifetime of the
	// 0mq message, recommended only with const data.
	template<typename Type>
	ZMQPP_DEPRECATED("Use add_nocopy() or add_nocopy_const() instead.")
	void add_const(const Type *content, std::size_t data_size = sizeof(Type))
	{
		add_nocopy_const(content, data_size, nullptr, nullptr);
	}

	/*
	 * REMARK: Why make the const/non-const differentiation in the method name
	 * rather than the signature (as is customary)
	 */
	/**
	 * Add a no-copy frame.
	 *
	 * This means that neither zmqpp nor libzmq will make a copy of the
	 * data. The pointed-to data must remain valid for the lifetime of
	 * the underlying zmq_msg_t. Note that you cannot always know about
	 * this lifetime, so be careful.
	 *
	 * @param content The pointed-to data that will be sent in the message.
	 * @param data_size The number of bytes pointed-to by "content".
	 * @param ffn The free function called by libzmq when it doesn't need
	 * your buffer anymore. It defaults to nullptr, meaning your data
	 * will not be freed.
	 * @param hint A hint to help your free function do its job.
	 *
	 * @note This is similar to what `move()` does. While `move()` provides
	 * a type-safe deleter (at the significant cost of 1 memory allocation)
	 * add_nocopy let you pass the low-level callback that libzmq will invoke.
	 *
	 * @note The free function must be thread-safe as it can be invoke from
	 * any of libzmq's context threads.
	 *
	 * @see add_nocopy_const
	 * @see move
	 */
	template<typename Type>
	void add_nocopy(Type *content, std::size_t data_size = sizeof(Type),
					zmq_free_fn *ffn = nullptr, void *hint = nullptr)
	{
		static_assert(!std::is_const<Type>::value,
                  "Data part must not be const. Use add_nocopy_const() instead (and read its documentation)");
		_parts.emplace_back(content, data_size, ffn, hint);
	}

	/**
	 * Add a no-copy frame where pointed-to data are const.
	 *
	 * This means that neither zmqpp nor libzmq will make a copy of the
	 * data. The pointed-to data must remain valid for the lifetime of
	 * the underlying zmq_msg_t. Note that you cannot always know about
	 * this lifetime, so be careful.
	 *
	 * @warning About constness: The library will cast away constness from
	 * your pointer. However, it promises that both libzmq and zmqpp will
	 * not alter the pointed-to data. *YOU* must however be careful: zmqpp or libzmq
	 * will happily return a non-const pointer to your data. It's your responsibility
	 * to not modify it.
	 *
	 * @param content The pointed-to data that will be send in the message.
	 * @param data_size The number of byte pointed-to by "content".
	 * @param ffn The free function called by libzmq when it doesn't need
	 * your buffer anymore. It defaults to nullptr, meaning your data won't be freed.
	 * @param hint A hint to help your free function do its job.
	 *
	 * @note The free function must be thread-safe as it can be invoke from
	 * any of libzmq's context threads.
	 *
	 * @see add_nocopy
	 */
	template<typename Type>
	void add_nocopy_const(const Type* content, 
	                      std::size_t data_size = sizeof(Type),
                        zmq_free_fn *ffn = nullptr, void *hint = nullptr)
	{
		add_nocopy(const_cast<Type*>(content), data_size, ffn, hint);
	}

#if (ZMQ_VERSION_MAJOR >= 4) && ((ZMQ_VERSION_MAJOR >= 2) && ZMQ_BUILD_DRAFT_API)
	/**
	 * Specify a group for the message to be sent via radio
	 *
	 * \param group the group that the message belongs to
	 * \return true if group was set successfully, false if there are no parts or not set successfully
	 */
	bool set_group(const std::string& group);
#endif

	// Stream reader style
	void reset_read_cursor();

	// Making them friend helps ADL
	// reader
	template<typename Type>
	friend message& operator>>(message& msg, Type& content)
	{
		content = msg.get<Type>(msg._read_cursor++);
		return msg;
	}

	// Writers - these all use copy styles
	/**
	 * @throws exception if conversion to BE byte stream is not supported
	 */
	template<class _Type>
	friend message& operator<<(message& msg, _Type content)
	{
		to_be(content, msg.raw_new_data(sizeof(_Type)));
		return msg;
	}

	// Queue manipulation
	void push_front(void const* content, std::size_t size);
	void push_front(char const* c_string);
	void push_front(std::string const& string);

	template<class _Type>
	void push_front(_Type content)
	{
		to_be(content, raw_new_data_front(sizeof(_Type)));
	}

	void pop_front();

	void push_back(void const* content, std::size_t data_size);

	template<typename _Type>void push_back(_Type content) {	*this << content;	}

	void pop_back();

	/** Use with moderation since it requires copying part to close the gap! */
	void remove(std::size_t part);

	// Move supporting
	message(message&& source) NOEXCEPT; // resets the read cursor on the source so cannot be const
	message& operator=(message&& rhs) NOEXCEPT;

	// Copy support : resets the read cursor on the source so cannot be const
	message copy() const; // copy-out => would require an additional move/assignment: deprecate??
	void copy(const message& source); // copy-in

	// Used for internal tracking
	void sent(std::size_t part);

	// Access to raw zmq details
	void const* raw_data(std::size_t part = 0) const;
	zmq_msg_t& raw_msg(std::size_t part = 0);
	zmq_msg_t& raw_new_msg(std::size_t reserve_data_size = 0);
	uint8_t* raw_new_data(std::size_t reserve_data_size);
	uint8_t* raw_new_data_front(std::size_t reserve_data_size);

	/**
	 * Check if the message is a signal.
	 * If the message has 1 part, has the correct size and if the 7 first bytes match
	 * the signal header we consider the message a signal.
	 * @return true if the message is a signal, false otherwise
	 */
	bool is_signal() const;

	/**
	 * Gets the read cursor for stream-style reading.
	 */
	size_t read_cursor() const NOEXCEPT { return _read_cursor; }

	/**
	 * Gets the remaining number of parts in the message.
	 */
	size_t remaining() const NOEXCEPT { return  _parts.size() - _read_cursor; }

	/**
	 * Moves the read cursor to the next element.
	 * @return the new read_cursor
	 */
	size_t next() NOEXCEPT { return ++_read_cursor; }

	// Move to private section when reached End-Of-Life.
	template<class _Type>
	ZMQPP_DEPRECATED("Use explicit template instantiation get<type>(part) instead.")
	void get(_Type& content, std::size_t part) const
	{
		content = get<_Type>(part);
	}

#if (ZMQ_VERSION_MAJOR == 4 && ZMQ_VERSION_MINOR >= 1)
	/**
	* Attemps to retrieve a metadata property from a message.
	* The underlying call is `zmq_msg_gets()`.
	*
	* @note The message MUST have at least one frame, otherwise this wont work.
	*/
	bool get_property(const std::string &property, std::string &out);
#endif

	friend void swap(message& lhs, message& rhs)
	{
		std::swap(lhs._parts, rhs._parts);
		std::swap(lhs._read_cursor, rhs._read_cursor);
	}

private:
	static void release_callback(void* data, void* hint);
	static void release_only_callback(void* data, void* hint);

	template<typename Object>
	static void deleter_callback(void* data)
	{
		delete static_cast<Object*>(data);
	}

	typedef std::deque<frame> parts_type;
	parts_type _parts;
	std::size_t _read_cursor;
};

// Specialisations
// Generic get<>

// Rely on NRVO for std::string
template<>
inline std::string message::get<std::string>(std::size_t part /* = 0 */) const
{
	return std::string(static_cast<const char*>(raw_data(part)), size(part));
}

template<>
inline char message::get<char>(std::size_t part) const
{
	assert(sizeof(char) == size(part));

	return *static_cast<const char*>(raw_data(part));
}

template<>
inline const char message::get<const char>(std::size_t part) const
{
	assert(sizeof(const char) == size(part));

	return *static_cast<const char*>(raw_data(part));
}

template<>
inline int message::get<int>(std::size_t part) const
{
	assert(sizeof(int) == size(part));

	const uint8_t* bytes = static_cast<const uint8_t*>(raw_data(part));
	return from_be<int>(bytes);
}

template<>
inline unsigned int message::get<unsigned int>(std::size_t part) const
{
	assert(sizeof(unsigned int) == size(part));

	const uint8_t* bytes = static_cast<const uint8_t*>(raw_data(part));
	return from_be<unsigned int>(bytes);
}

template<>
inline long message::get<long>(std::size_t part) const
{
	assert(sizeof(long) == size(part));

	const uint8_t* bytes = static_cast<const uint8_t*>(raw_data(part));
	return from_be<long>(bytes);
}

template<>
inline unsigned long message::get<unsigned long>(std::size_t part) const
{
	assert(sizeof(unsigned long) == size(part));

	const uint8_t* bytes = static_cast<const uint8_t*>(raw_data(part));
	return from_be<unsigned long>(bytes);
}

template<>
inline int8_t message::get<int8_t>(std::size_t part) const
{
	assert(sizeof(int8_t) == size(part));

	return *static_cast<const int8_t*>(raw_data(part));
}

template<>
inline int16_t message::get<int16_t>(std::size_t part) const
{
	assert(sizeof(int16_t) == size(part));

	const uint8_t* bytes = static_cast<const uint8_t*>(raw_data(part));
	return from_be<int16_t>(bytes);
}

template<>
inline signal message::get<signal>(std::size_t part) const
{
	assert(sizeof(signal) == size(part));
  
	const uint8_t* bytes = static_cast<const uint8_t*>(raw_data(part));
	return static_cast<signal>(from_be<int64_t>(bytes));
}

template<>
inline uint8_t message::get<uint8_t>(std::size_t part) const
{
	assert(sizeof(uint8_t) == size(part));

	return *static_cast<const uint8_t*>(raw_data(part));
}

template<>
inline uint16_t message::get<uint16_t>(std::size_t part) const
{
	assert(sizeof(uint16_t) == size(part));

	const uint8_t* bytes = static_cast<const uint8_t*>(raw_data(part));
	return from_be<uint16_t>(bytes);
}

template<>
inline float message::get<float>(std::size_t part) const
{
	assert(sizeof(float) == size(part));

	const uint8_t* bytes = static_cast<const uint8_t*>(raw_data(part));
	return from_be<float>(bytes);
}

template<>
inline double message::get<double>(std::size_t part) const
{
	assert(sizeof(double) == size(part));

	const uint8_t* bytes = static_cast<const uint8_t*>(raw_data(part));
	return from_be<double>(bytes);
}

template<>
inline bool message::get<bool>(std::size_t part) const
{
	assert(sizeof(uint8_t) == size(part));

	return *static_cast<const uint8_t*>(raw_data(part)) > 0;
}

// Generic call-by-ref get<>
template<>
inline void message::get<std::string>(std::string& std_string, std::size_t part) const
{
	std_string.assign(static_cast<const char*>(raw_data(part)), size(part));
}

// Generic operator<< <>
template<>
inline message& operator<< <uint8_t>(message& msg, uint8_t byte)
{
	msg.add_raw(&byte);
	return msg;
}

template<>
inline message& operator<< <int8_t>(message& msg, int8_t ch)
{
	return msg << static_cast<uint8_t>(ch);
}

template<>
inline message& operator<< <signal>(message& msg, signal sig)
{
  return msg << static_cast<int64_t>(sig);
}

template<>
inline message& operator<< <bool>(message& msg, bool truth)
{
  return msg << static_cast<uint8_t>(truth ? 1 : 0);
}

template<>
inline message& operator<< <const char*>(message& msg, const char* c_string)
{
	msg.add_raw(c_string, std::strlen(c_string));
	return msg;
}

template<>
inline message& operator<< <std::string>(message& msg,
                                         std::string std_string)
{
	return msg << std_string.c_str();
}

// Generic push_front<>
template<>
inline void message::push_front<uint8_t>(uint8_t byte)
{
	push_front(&byte, sizeof(uint8_t));
}

template<>
inline void message::push_front<int8_t>(int8_t ch)
{
	push_front(static_cast<uint8_t>(ch));
}

template<>
inline void message::push_front<signal>(signal sig)
{
	push_front(static_cast<int64_t>(sig));
}

template<>
inline void message::push_front<bool>(bool truth)
{
	push_front(static_cast<uint8_t>(truth ? 1 : 0));
}

template<>
inline void message::push_front<std::string>(std::string str)
{
	push_front(str.c_str());
}
}

#endif /* ZMQPP_MESSAGE_HPP_ */
