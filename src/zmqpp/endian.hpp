/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file is part of zmqpp.
 * Copyright (c) 2011-2016 Contributors as noted in the AUTHORS file.
 */

/**
 * \file
 *
 * \date   7 Dec 2016
 * \author Guy Bormann
 */

#if !defined(ZMQPP_ENDIAN_HPP)
#define ZMQPP_ENDIAN_HPP

#include <algorithm>
#include <cstdint>
#include <iterator>

/**
  * Portable and platform-agnostic endianness conversion with native
  * 'optimisation' if available. It has the same operations counts (bit
  * masking, or'ing and shifting) as a semi-portable byte swap algorithm,
  * minus the endianness detection hairiness.
  *
  * See: "The byte ordering fallacy" blog article by Rob Spike.
  *
  * Using GCC/Linux on AMD64, native is in relative terms about 1.7x
  * (to BE byte stream) resp. 3.3x faster (from BE byte stream) than portable
  * in 64bit mode, and about 1.4 resp. 2.1x faster in 32bit compatibility mode.
  * That is, both for native word size of the mode and at -O2 optimisation
  * level.
  *
  * The portable converter takes almost twice as long for 64bit words in
  * 32bit compatibility mode as compared to native 64 bit mode.
  * On the other hand, native conversion is only slightly slower for 64bit
  * words in 32bit compatibility mode as compared to native 64bit mode.
  *
  * However, in absolute terms, on a 1.8GHz AMD laptop, the slowest conversion
  * still takes significantly less than 10ns/conversion (5.7ns on mains,
  * ~7.9ns on battery).
  *
  * To support other data types than native machine words (but of the same
  * size) it uses a struct type overlay in a union to stay on the safe side
  * of the Standard, although the phrase 'active member' (i.e. the member
  * last written to) of unions is probably misinterpreted by language
  * lawyers.
  *
  * (IMO, it just means that you can't expect all the union members to
  * keep bit representations that are compatible with their respective type
  * when you change the union value through one of the other members.
  *
  * If not, how would one be able to access the bit representations if
  * C and C++ are supposed to be systems programming languages???
  *
  * After all, some architectures don't allow access to individual bytes
  * through pointer manipulations, whilst bit shifting is almost universal.
  * But to enable the latter you need access to machine word representation.)
  *
  * If FORCE_PORTABLE is declared, either as a macro argument to the compiler
  * or in a config.hpp header, the portable implementations are used for all
  * supported conversions. This allows testing or benchmarking the portable
  * implementation on a new target platform.
  *
  * December 2016
  * Guy Bormann
  */

// TODO Relegate platform-specific selection macros to a config.hpp header (f.i. using autoconf).
// #include "config.hpp"

/*
 * NOTE: There is no need to check for endianness when mapping to
 * platform-specific implementations because the platform headers are supposed
 * to be properly configured, either through macro magic or by mapping to
 * relevant intrinsics or library implementations.
 *
 * REMARK: using GCC as a naming model because it is explicit and clear
 * in its intentions.
 *
 * I.e. map platform-specific conversion macros, functions and/or intrinsics
 * to:
 *	htobe16/be16toh	for 16bit words
 *	htobe32/be32toh for 32bit words
 *	htobe64/be64toh for 64bit words
 *
 * Types that map to words of sizes that are multiples of 2 bytes are taken
 * care of by the portable converters through partial specialisations using
 * type punning.
 *
 * So, there really is no need for ad-hoc byte swapping converters for
 * these types, and float/doubles in particular.
 */
#if !defined(__WORDSIZE)
#error __WORDSIZE is not defined. Please define __WORDSIZE for your platform in number of bits (e.g. 32 or 64)!
#endif

#if defined(__GNUC__) // Platform byte ordering support for GCC-supported targets (tested on amd64 GNU/Linux)

# include <endian.h>

#elif defined( __APPLE__) // Platform byte ordering support for Apple OS X

# include <libkern/OSByteOrder.h>

// TODO Check with header and complete for 16bit and 64bit on __APPLE__
# define htobe32(x) OSSwapHostToBigInt32(x)
# define be32toh(x) OSSwapBigInt32ToHost(x)

//#elif
// TODO Add other platform-specific support, such as 16 bit embedded systems and systems without POSIX-compliant IP networking API
#else
/*
 * If no platform-specific support is supplied at this stage, try falling
 * back to conversion functions from POSIX-compliant IP networking API.
 */
# if defined(_WIN32) || defined(__WINE__) // Platform byte ordering support for Windows
# include <winsock2.h>
# else
# include <netinet/in.h>
# endif

# if !defined(htobe16) // so that we can stick to platform-specific versions
#   define htobe16(x) htons(x)
#   define be16toh(x) nstoh(x)
# endif

# if !defined(htobe32) // so that we can stick to platform-specific versions
#   define htobe32(x) htonl(x)
#   define be32toh(x) nltoh(x)
# endif

#endif

/*
 * NOTE: From here on, if we don't have a platform-specific version or mapping
 * available, we automatically fall back to the portable implementation.
 *
 * Please observe that platform-specific support can be partial, f.i.
 * available for 32 bit words but not for 64 bit words.
 */

#include "exception.hpp"

namespace zmqpp
{
  // SEE ALSO Convenience functions after the details namespace!
  namespace details
  {
    // To Big Endian byte stream
    template<typename _ByteIter,
             typename _N2BType,
             int bl = sizeof(_N2BType)>
    struct to_be_bytestream_converter
    {
      /**
       *  @param dst an appropriately sized transmit buffer
       *  @throws exception if conversion to BE byte stream is not supported for this type
       */
      void operator()(_N2BType x, _ByteIter dst) const
      { 
        throw exception("To Big Endian conversion not supported for this type.");
      }
    };

    // Partial specialisation for 32 bit types that are not plain 32 bit words.
    template<typename _ByteIter, typename _N2BType>
    struct to_be_bytestream_converter<_ByteIter, _N2BType, 4>
    {
      void operator()(_N2BType x, _ByteIter dst) const
      {
        union
        {
          _N2BType _x;
          struct
          {
            uint32_t _i; // byte-for-byte integer representation
          } _wd;
        } r {x};

        // be explicit or you'll be right back here!
        to_be_bytestream_converter<_ByteIter, uint32_t>()(r._wd._i, dst);
      }
    };

    // Partial specialisation for 64 bit types that are not plain 64 bit words
    template<typename _ByteIter, typename _N2BType>
    struct to_be_bytestream_converter<_ByteIter, _N2BType, 8>
    {
      void operator()(_N2BType x, _ByteIter dst) const
      {
        union
        {
          _N2BType _x;
          struct
          {
            uint64_t _i; // byte-for-byte integer representation
          } _wd;
        } r {x};

        // be explicit or you'll be right back here!
        to_be_bytestream_converter<_ByteIter, uint64_t>()(r._wd._i, dst);
      }
    };

    // Partial specialisations for 16 bit words
    template<typename _ByteIter>
    struct to_be_bytestream_converter<_ByteIter, uint16_t, 2>
    {
      void operator()(uint16_t x, _ByteIter dst) const
      {
# if defined(htobe16) && !defined(ZMQPP_FORCE_PORTABLE)
        // make std::copy_n available, allow for specialisations
        using std::copy_n;
        union
        {
          uint16_t _x;
          struct
          {
            uint8_t _ba[2];
          };
        } r { htobe16(x) };
        copy_n(r._ba, 2, dst);
# else
        *dst++ = (x >>  8) & 0xFF;
        *dst++ =  x        & 0xFF;
# endif
      }
    };

    template<typename _ByteIter>
    struct to_be_bytestream_converter<_ByteIter, int16_t, 2>
    {
      void operator()(int16_t x, _ByteIter dst) const
      {
        to_be_bytestream_converter<_ByteIter, uint16_t>()(
            static_cast<uint16_t>(x), dst
        );
      }
    };

    // Partial specialisations for 32 bit words
    template<typename _ByteIter>
    struct to_be_bytestream_converter<_ByteIter, uint32_t, 4>
    {
      void operator()(uint32_t x, _ByteIter dst) const
      {
# if defined(htobe32) && !defined(ZMQPP_FORCE_PORTABLE)
        using std::copy_n;
        union
        {
          uint32_t _x;
          struct
          {
            uint8_t _ba[4];
          };
        } r { htobe32(x) };
        copy_n(r._ba, 4, dst);
# else
        *dst++ = (x >> 24) & 0xFF;
        *dst++ = (x >> 16) & 0xFF;
        *dst++ = (x >>  8) & 0xFF;
        *dst++ =  x        & 0xFF;
# endif
      }
    };

    template<typename _ByteIter>
    struct to_be_bytestream_converter<_ByteIter, int32_t, 4>
    {
      void operator()(int32_t x, _ByteIter dst) const
      {
        to_be_bytestream_converter<_ByteIter, uint32_t>()(
            static_cast<uint32_t>(x), dst
        );
      }
    };

    // Partial specialisations for 64 bit words
    template<typename _ByteIter>
    struct to_be_bytestream_converter<_ByteIter, uint64_t, 8>
    {
      void operator()(uint64_t x, _ByteIter dst) const
      {
# if defined(htobe64) && !defined(ZMQPP_FORCE_PORTABLE)
        using std::copy_n;
        union
        {
          uint64_t _x;
          struct
          {
            uint8_t _ba[8];
          };
        } r { htobe64(x) };
        copy_n(r._ba, 8, dst);
# else
      /*
       * On AMD64 it appears to be slightly faster to split shifting in a
       * high and a low 32bit word. The generated code seems to better match
       * the register allocation (revealing a microcode implementation of
       * 64 bit ops, perhaps?).
       *
       * On your platform there might be no difference or it might even be
       * faster to do explicit 64bit shifts (i.e. directly shifting by >> 56
       * to get at the high byte, etc.).
       */
#   if __WORDSIZE == 64
        uint64_t hi { x >> 32 };
        uint64_t lo { x };
#   else
        uint32_t hi { static_cast<uint32_t>(x >> 32) };
        uint32_t lo { static_cast<uint32_t>(x & 0x00000000FFFFFFFFull) };
#   endif
        *dst++ = (hi >> 24) & 0xFF;
        *dst++ = (hi >> 16) & 0xFF;
        *dst++ = (hi >>  8) & 0xFF;
        *dst++ =  hi        & 0xFF;
        *dst++ = (lo >> 24) & 0xFF;
        *dst++ = (lo >> 16) & 0xFF;
        *dst++ = (lo >>  8) & 0xFF;
        *dst++ =  lo        & 0xFF;
# endif
      }
    };

    template<typename _ByteIter>
    struct to_be_bytestream_converter<_ByteIter, int64_t, 8>
    {
      void operator()(int64_t x, _ByteIter dst) const
      {
        to_be_bytestream_converter<_ByteIter, uint64_t>()(
            static_cast<uint64_t>(x), dst
        );
      }
    };

    // From Big Endian byte stream
    template<typename _ByteIter,
             typename _N2BType,
             int bl = sizeof(_N2BType)>
    struct from_be_bytestream_converter
    {
      /**
       * @param src an appropriately sized receive buffer
       * @throws exception throws if conversion from BE byte stream to type is not supported
       */
      constexpr _N2BType operator()(_ByteIter src) const
      {
        throw exception("From Big Endian conversion not supported for this type.");
      }
    };

    // Partial specialisation for 32 bit types that are not plain 32 bit words.
    template<typename _ByteIter, typename _N2BType>
    struct from_be_bytestream_converter<_ByteIter, _N2BType, 4>
    {
      _N2BType operator()(_ByteIter src) const
      {
        union
        {
          _N2BType _x;
          struct
          {
            uint32_t _i; // byte-for-byte integer representation
          } _wd;
        } w;

        w._wd._i = from_be_bytestream_converter<_ByteIter, uint32_t>()(src); // be explicit or you'll be back here in no time!
        return w._x;
      }
    };

    // Partial specialisation for 64 bit types that are not plain 64 bit words.
    template<typename _ByteIter, typename _N2BType>
    struct from_be_bytestream_converter<_ByteIter, _N2BType, 8>
    {
      _N2BType operator()(_ByteIter src) const
      {
        union
        {
          _N2BType _x;
          struct
          {
            uint64_t _i;
          } _wd;
        } w;

        w._wd._i = from_be_bytestream_converter<_ByteIter, uint64_t>()(src);
        return w._x;
      }
    };

    // Partial specialisations for 16 bit words
    template<typename _ByteIter>
    struct from_be_bytestream_converter<_ByteIter, uint16_t, 2>
    {
      uint16_t operator()(_ByteIter src) const
      {
# if defined(be16toh) && !defined(ZMQPP_FORCE_PORTABLE)
        using std::copy_n;
        union
        {
          uint16_t _x;
          struct
          {
            uint8_t _ba[2];
          };
        } w;
        copy_n(src, 2, w._ba);
        return be16toh(w._x);
# else
        return   (static_cast<uint16_t>(*src++) << 8)
               |  static_cast<uint16_t>(*src++);
# endif
      }
    };

    template<typename _ByteIter>
    struct from_be_bytestream_converter<_ByteIter, int16_t, 2>
    {
      int32_t operator()(_ByteIter src) const
      {
        return static_cast<int16_t>(
            from_be_bytestream_converter<_ByteIter, uint16_t>()(src)
        );
      }
    };

    // Partial specialisations for 32 bit words
    template<typename _ByteIter>
    struct from_be_bytestream_converter<_ByteIter, uint32_t, 4>
    {
      uint32_t operator()(_ByteIter src) const
      {
# if defined(be32toh) && !defined(ZMQPP_FORCE_PORTABLE)
        using std::copy_n;
        union
        {
          uint32_t _x;
          struct
          {
            uint8_t _ba[4];
          };
        } w;
        copy_n(src, 4, w._ba);
        return be32toh(w._x);
# else
        return   (static_cast<uint32_t>(*src++) << 24)
               | (static_cast<uint32_t>(*src++) << 16)
               | (static_cast<uint32_t>(*src++) << 8)
               |  static_cast<uint32_t>(*src++);
# endif
      }
    };

    template<typename _ByteIter>
    struct from_be_bytestream_converter<_ByteIter, int32_t, 4>
    {
      int32_t operator()(_ByteIter src) const
      {
        return static_cast<int32_t>(
            from_be_bytestream_converter<_ByteIter, uint32_t>()(src)
        );
      }
    };

    // Partial specialisations for 64 bit words
    template<typename _ByteIter>
    struct from_be_bytestream_converter<_ByteIter, uint64_t, 8>
    {
      uint64_t operator()(_ByteIter src) const
      {
# if defined(be64toh) && !defined(ZMQPP_FORCE_PORTABLE)
        using std::copy_n;
        union
        {
          uint64_t _x;
          struct
          {
            uint8_t _ba[8];
          };
        } w;
        copy_n(src, 8, w._ba);
        return be64toh(w._x);
# else
#   if __WORDSIZE == 64
        return (( (static_cast<uint64_t>(*src++) << 24)
                | (static_cast<uint64_t>(*src++) << 16)
                | (static_cast<uint64_t>(*src++) <<  8)
                | (static_cast<uint64_t>(*src++)      )
                ) << 32)
                | (static_cast<uint64_t>(*src++) << 24)
                | (static_cast<uint64_t>(*src++) << 16)
                | (static_cast<uint64_t>(*src++) <<  8)
                |  static_cast<uint64_t>(*src++);
#   else
        return (static_cast<uint64_t>(
                    (static_cast<uint32_t>(*src++) << 24)
                  | (static_cast<uint32_t>(*src++) << 16)
                  | (static_cast<uint32_t>(*src++) <<  8)
                  | (static_cast<uint32_t>(*src++)      )
                                     ) << 32)
                |
                static_cast<uint64_t>(
                    (static_cast<uint32_t>(*src++) << 24)
                  | (static_cast<uint32_t>(*src++) << 16)
                  | (static_cast<uint32_t>(*src++) <<  8)
                  |  static_cast<uint32_t>(*src++));
#   endif
# endif
      }
    };

    template<typename _ByteIter>
    struct from_be_bytestream_converter<_ByteIter, int64_t, 8>
    {
      int64_t operator()(_ByteIter src) const
      {
        return static_cast<int64_t>(
            from_be_bytestream_converter<_ByteIter, uint64_t>()(src)
        );
      }
    };
  }

  /**
   * Convenience functions
   *
   * Use these! Let the compiler handle the automatic type magic.
   * You'll have to specify the return type for from_be, though :
   * e.g. double price = from_be<double>(source_byte_stream_iter)
   */
  template<typename _N2BType, typename _ByteIter>
  void to_be(_N2BType x, _ByteIter dst)
  {
    details::to_be_bytestream_converter<_ByteIter, _N2BType>()(x, dst);
  }

  template<typename _N2BType, typename _ByteIter>
  _N2BType from_be(_ByteIter src)
  {
    return details::from_be_bytestream_converter<_ByteIter, _N2BType>()(src);
  }
}

#endif
