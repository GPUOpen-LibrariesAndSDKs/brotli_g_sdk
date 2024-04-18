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

#include <cassert>

extern "C" {
#include "brotli/c/common/platform.h"
}

#include "common/BrotligCommon.h"
#include "common/BrotligConstants.h"

namespace BrotliG
{
    class BrotligBitReaderLSB
    {
    public:
        BrotligBitReaderLSB()
        {
            Reset();
        }

        ~BrotligBitReaderLSB()
        {
            Reset();
        }

        void Reset()
        {
            m_buf = 0;
            m_bitcnt = 0;
            m_input = m_next = nullptr;
            m_inputsize = 0;
        }

        void Initialize(const uint8_t* input, size_t inputsize)
        {
            m_input = m_next = input;
            m_inputsize = inputsize;

            m_buf = (uint64_t)Load();
            m_bitcnt = minbitcnt;
            m_next += 4;
        }

        inline uint32_t ReadNoConsume(uint32_t n) {
            return (uint32_t)m_buf & BrotligBitMask[n];
        }

        inline uint32_t ReadNoConsume() { return (uint32_t)m_buf; }

        inline uint32_t ReadAndConsume(uint32_t n)
        {
            uint32_t bits = (uint32_t)m_buf & BrotligBitMask[n];
            Consume(n);
            return bits;
        }

        inline void Consume(uint32_t n)
        {
            m_buf >>= n;
            m_bitcnt -= n;
            
            if (m_bitcnt < minbitcnt)
            {
                m_buf |= (uint64_t)Load() << (m_bitcnt);
                m_bitcnt += minbitcnt;
                m_next += 4;
            }
        }

        inline uint16_t ReadNoConsume16() { return (uint16_t)m_buf; }

        inline const uint8_t* GetInput()
        {
            return m_input;
        }

    private:
        const uint32_t BrotligBitMask[33] = { 0x00000000,
            0x00000001, 0x00000003, 0x00000007, 0x0000000F,
            0x0000001F, 0x0000003F, 0x0000007F, 0x000000FF,
            0x000001FF, 0x000003FF, 0x000007FF, 0x00000FFF,
            0x00001FFF, 0x00003FFF, 0x00007FFF, 0x0000FFFF,
            0x0001FFFF, 0x0003FFFF, 0x0007FFFF, 0x000FFFFF,
            0x001FFFFF, 0x003FFFFF, 0x007FFFFF, 0x00FFFFFF,
            0x01FFFFFF, 0x03FFFFFF, 0x07FFFFFF, 0x0FFFFFFF,
            0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, 0xFFFFFFFF
        };

        inline uint32_t Load()
        {
            return *(uint32_t*)(m_next);
        }

        static constexpr uint32_t minbitcnt = 32;

        uint64_t m_buf;
        size_t m_bitcnt;

        const uint8_t* m_input;
        const uint8_t* m_next;
        size_t m_inputsize;
    };

    class BrotligBitReaderMSB
    {
    public:
        void Reset()
        {
            m_buf = 0;
            m_bitcnt = 0;
            m_input = m_next = nullptr;
            m_inputsize = 0;
        }

        void Initialize(const uint8_t* input, size_t inputsize)
        {
            m_input = m_next = input;
            m_inputsize = inputsize;

            m_buf = (uint64_t)Load() << minbitcnt;
            m_bitcnt = minbitcnt;
            m_next += 4;
        }

        inline uint32_t ReadNoConsume(uint32_t n) {
            return (uint32_t)((m_buf & BrotligBitMask[n]) >> (2 * minbitcnt - n));
        }

        inline uint32_t ReadNoConsume() { return (uint32_t)m_buf; }

        inline uint32_t ReadAndConsume(uint32_t n)
        {
            uint32_t bits = (uint32_t)((m_buf & BrotligBitMask[n]) >> (2 * minbitcnt - n));
            Consume(n);
            return bits;
        }

        inline void Consume(uint32_t n)
        {
            m_buf <<= n;
            m_bitcnt -= n;

            if (m_bitcnt < minbitcnt)
            {
                m_buf |= (uint64_t)Load() << (minbitcnt - m_bitcnt);
                m_bitcnt += minbitcnt;
                m_next += 4;
            }
        }

        inline uint16_t ReadNoConsume16() { return (uint16_t)m_buf; }

        inline const uint8_t* GetInput()
        {
            return m_input;
        }

    private:
        const uint64_t BrotligBitMask[33] = { 0x0000000000000000,
            0x8000000000000000, 0xC000000000000000, 0xE000000000000000, 0xF000000000000000,
            0xF800000000000000, 0xFC00000000000000, 0xFE00000000000000, 0xFF00000000000000,
            0xFF80000000000000, 0xFFC0000000000000, 0xFFE0000000000000, 0xFFF0000000000000,
            0xFFF8000000000000, 0xFFFC000000000000, 0xFFFE000000000000, 0xFFFF000000000000,
            0xFFFF800000000000, 0xFFFFC00000000000, 0xFFFFE00000000000, 0xFFFFF00000000000,
            0xFFFFF80000000000, 0xFFFFFC0000000000, 0xFFFFFE0000000000, 0xFFFFFF0000000000,
            0xFFFFFF8000000000, 0xFFFFFFC000000000, 0xFFFFFFE000000000, 0xFFFFFFF000000000,
            0xFFFFFFF800000000, 0xFFFFFFFC00000000, 0xFFFFFFFE00000000, 0xFFFFFFFF00000000
        };

        inline uint32_t Load()
        {
            uint32_t t = 0;
            t |= (uint32_t)m_next[0] << 24;
            t |= (uint32_t)m_next[1] << 16;
            t |= (uint32_t)m_next[2] << 8;
            t |= (uint32_t)m_next[3];

            return t;
        }

        static constexpr uint32_t minbitcnt = 32;

        uint64_t m_buf;
        size_t m_bitcnt;

        const uint8_t* m_input;
        const uint8_t* m_next;
        size_t m_inputsize;
    };
}