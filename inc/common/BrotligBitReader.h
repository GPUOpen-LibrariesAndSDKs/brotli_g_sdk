// Brotli-G SDK 1.0
// 
// Copyright(c) 2022 Advanced Micro Devices, Inc. All rights reserved.
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

#include "BrotligCommon.h"
#include "BrotligConstants.h"

namespace BrotliG
{
    class BrotligBitReader
    {
    public:
        BrotligBitReader()
        {
            m_buf = 0;
            m_bitcnt = 0;
            m_next = 0;
            m_curbitpos = 0;
            m_input = nullptr;
            m_inputsize = 0;
        }

        ~BrotligBitReader()
        {
            m_buf = 0;
            m_bitcnt = 0;
            m_next = 0;
            m_curbitpos = 0;
            m_input = nullptr;
            m_inputsize = 0;
        }

        uint32_t load4(size_t bytepos)
        {
            uint32_t loaded = 0;
            memcpy(&loaded, &m_input[bytepos], BROTLIG_DWORD_SIZE_BYTES);

            return loaded;
        }

        void Initialize(const uint8_t* input, size_t inputsize, size_t start)
        {
            m_input = input;
            m_inputsize = inputsize;

            m_buf = (uint64_t)load4(start);
            m_bitcnt = minbitcnt;
            m_next = start + 4;
            m_curbitpos = 0;
        }

        void Refill()
        {
            bool p = m_bitcnt < minbitcnt;
            if (p)
            {
                m_buf |= (uint64_t)load4(m_next) << (m_bitcnt);
                m_bitcnt += minbitcnt;
                m_next += 4;
            }
        }

        void Consume(uint32_t n)
        {
            m_buf >>= n;
            m_bitcnt -= n;
            m_curbitpos += n;
            Refill();
        }

        uint32_t Mask(uint32_t n)
        {
            uint64_t temp = (uint64_t)1 << (uint64_t)n;
            temp = temp - 1;
            return (uint32_t)temp;
        }

        uint32_t ReadNoConsume(uint32_t n) {
            uint32_t bits = (uint32_t)m_buf & Mask(n);
            return bits;
        }

        uint32_t ReadNoConsume() { return (uint32_t)m_buf; }

        uint32_t ReadAndConsume(uint32_t n)
        {
            uint32_t bits = (uint32_t)m_buf & Mask(n);
            Consume(n);
            return bits;
        }

        void AlignToNextDWord()
        {
            size_t cur_pos = m_curbitpos;
            size_t remainder = cur_pos % 32;
            if (remainder)
            {
                Consume(static_cast<uint32_t>(32 - remainder));
            }

            assert(m_curbitpos % 32 == 0);
        }

        static uint32_t LoadVarLenUint8(
            BrotligBitReader* br)
        {
            uint32_t length = 0;
            uint32_t bits = br->ReadAndConsume(1);
            if (bits != 0)
            {
                uint32_t nbits = br->ReadAndConsume(3);
                uint32_t rem = br->ReadAndConsume(nbits);
                length = (1u << nbits) + rem;
            }

            return length;
        }

        size_t GetCurBitPos()
        {
            return m_curbitpos;
        }

        const uint8_t* GetInput()
        {
            return m_input;
        }

    private:
        static constexpr uint32_t minbitcnt = 32;

        uint64_t m_buf;
        size_t m_bitcnt, m_next, m_curbitpos;

        const uint8_t* m_input;
        size_t m_inputsize;
    };
}