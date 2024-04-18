// Brotli-G SDK 1.2
// 
// Copyright(c) 2022 - 2024 Advanced Micro Devices, Inc. All rights reserved.
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#pragma once

extern "C" {
#include "brotli/c/common/platform.h"
#include "brotli/c/enc/fast_log.h"
}

#include "common/BrotligCommon.h"

namespace BrotliG
{
    class BrotligBitWriterLSB
    {
    public:
        BrotligBitWriterLSB()
        {
            m_storage = nullptr;
            m_curbitpos = 0;
        }

        ~BrotligBitWriterLSB()
        {
            m_storage = nullptr;
            m_curbitpos = 0;
        }

        void SetStorage(uint8_t* storage)
        {
            m_storage = storage;
        }

        void SetPosition(size_t pos)
        {
            m_curbitpos = pos;
        }

        size_t& GetPosition()
        {
            return m_curbitpos;
        }

        uint8_t* GetStorage()
        {
            return m_storage;
        }

        void Write(size_t n_bits, uint64_t bits)
        {
            // Starting from the rightmost bit, read n_bits, one at a time, right to left
            size_t bitsremaining = n_bits;
            while (bitsremaining > 0)
            {
                unsigned char* p = &m_storage[m_curbitpos >> 3];
                // From the current bit position, find the bit position in the current byte from the right
                size_t bitposinbyte = (m_curbitpos & 7);
                size_t bitsempty = 8 - bitposinbyte;
                size_t extractlen = (bitsremaining > bitsempty) ? bitsempty : bitsremaining;

                // isolate "extractlen" rightmost bits from shiftedBits
                uint64_t mask = (static_cast<uint64_t>(1u) << extractlen) - 1;
                uint64_t extractedbits = bits & mask;
                bits >>= extractlen;

                // shift bits into position
                extractedbits <<= bitposinbyte;
                unsigned char val = ((unsigned char)extractedbits);

                (*p) |= val;

                bitsremaining -= extractlen;
                m_curbitpos += extractlen;
            }
        }

        void AlignToNextDWord()
        {
            size_t cur_pos = m_curbitpos;
            size_t remainder = cur_pos % 32;
            if (remainder)
            {
                Write(32 - remainder, 0);
            }

            assert(m_curbitpos % 32 == 0);
        }

        void AlignToNextByte()
        {
            size_t cur_pos = m_curbitpos;
            size_t remainder = cur_pos % 8;
            if (remainder)
            {
                Write(8 - remainder, 0);
            }

            assert(m_curbitpos % 8 == 0);
        }

        static void StoreVanLenUint8(
            size_t n,
            BrotligBitWriterLSB* bw
        )
        {
            if (n == 0)
                bw->Write(1, 0);
            else
            {
                size_t nbits = Log2FloorNonZero(n);
                bw->Write(1, 1);
                bw->Write(3, nbits);
                bw->Write(nbits, n - ((size_t)1 << nbits));
            }
        }

    private:
        uint8_t* m_storage;
        size_t m_curbitpos;
    };

    class BrotligBitWriterMSB
    {
    public:
        BrotligBitWriterMSB()
        {
            m_storage = nullptr;
            m_curbitpos = 0;
        }

        ~BrotligBitWriterMSB()
        {
            m_storage = nullptr;
            m_curbitpos = 0;
        }

        void SetStorage(uint8_t* storage)
        {
            m_storage = storage;
        }

        void SetPosition(size_t pos)
        {
            m_curbitpos = pos;
        }

        size_t& GetPosition()
        {
            return m_curbitpos;
        }

        uint8_t* GetStorage()
        {
            return m_storage;
        }

        void Write(size_t n_bits, uint64_t bits)
        {
            // shift all the bits to the left
            bits <<= 64 - n_bits;
            
            // Starting from the 'n_bits' leftmost bit, read n_bits, one at a time, left to right
            size_t bitsremaining = n_bits;
            while (bitsremaining > 0)
            {
                unsigned char* p = &m_storage[m_curbitpos >> 3];
                // From the current bit position, find the bit position in the current byte from the left
                size_t bitposinbyte = (m_curbitpos & 7);
                size_t bitsempty = 8 - bitposinbyte;
                size_t extractLen = (bitsremaining > bitsempty) ? bitsempty : bitsremaining;

                // isolate "extractlen" leftmost bits from shiftedbits
                uint64_t mask = static_cast<uint64_t>(0xFFFFFFFFFFFFFFFF) & ~((static_cast<uint64_t>(1u) << (64 - extractLen)) - 1);
                uint64_t extractedbits = bits & mask;
                bits <<= extractLen;

                // shift bits into position
                extractedbits >>= 56 + bitposinbyte;
                unsigned char val = ((unsigned char)extractedbits);

                (*p) |= val;

                bitsremaining -= extractLen;
                m_curbitpos += extractLen;
            }
        }
    private:
        uint8_t* m_storage;
        size_t m_curbitpos;
    };
}
