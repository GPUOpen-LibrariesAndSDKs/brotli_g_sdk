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
#include "brotli/c/dec/bit_reader.h"
}

#include <cassert>

#include "common/BrotligConstants.h"

namespace BrotliG
{
    static const uint32_t BrotligBitMask[33] = { 0x00000000,
       0x00000001, 0x00000003, 0x00000007, 0x0000000F,
       0x0000001F, 0x0000003F, 0x0000007F, 0x000000FF,
       0x000001FF, 0x000003FF, 0x000007FF, 0x00000FFF,
       0x00001FFF, 0x00003FFF, 0x00007FFF, 0x0000FFFF,
       0x0001FFFF, 0x0003FFFF, 0x0007FFFF, 0x000FFFFF,
       0x001FFFFF, 0x003FFFFF, 0x007FFFFF, 0x00FFFFFF,
       0x01FFFFFF, 0x03FFFFFF, 0x07FFFFFF, 0x0FFFFFFF,
       0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, 0xFFFFFFFF
    };

    class BrotligDeswizzler
    {
    public:
        BrotligDeswizzler()
        {
            Reset();
        }

        ~BrotligDeswizzler()
        {
            Reset();
        }

        void Reset()
        {
            for (size_t i = 0; i < BROTLIG_MAX_NUM_BITSTREAMS; ++i)
            {
                m_bufs[i] = 0;
                m_bitposs[i] = 0;
                m_nexts[i] = nullptr;
            }

            m_curindex = 0;
        }

        void Initialize(size_t num_bitstreams)
        {
            m_numbitstreams = num_bitstreams;
            m_curindex = 0;
        }

        void SetReader(size_t index, const uint8_t* stream)
        {
            m_nexts[index] = stream;

            m_bufs[index] = Load(index);
            m_bitposs[index] = 0;
            m_nexts[index] += 8;
        }

        inline uint32_t ReadNoConsume(uint32_t n)
        {
            if (n == 0) return 0;
            FillBitWindow(n);
            return (uint32_t)GetUnmasked() & Mask(n);
        }

        inline void Consume(uint32_t n)
        {
            m_bitposs[m_curindex] += n;
        }

        inline uint32_t ReadAndConsume(uint32_t n)
        {
            if (n == 0) return 0;
            FillBitWindow(n);
            uint32_t v = (uint32_t)GetUnmasked() & Mask(n);
            Consume(n);
            return v;
        }

        inline uint32_t ReadNoConsume16()
        {
            FillBitWindow16();
            return (uint32_t)GetUnmasked() & 0x0000FFFF;
        }

        inline uint32_t ReadNoConsume15()
        {
            FillBitWindow16();
            return (uint32_t)GetUnmasked() & 0x00007FFF;
        }

        inline uint32_t ReadNoConsume10()
        {
            FillBitWindow16();
            return (uint32_t)GetUnmasked() & 0x000003FF;
        }

        inline uint32_t ReadNoConsume9()
        {
            FillBitWindow16();
            return (uint32_t)GetUnmasked() & 0x000001FF;
        }

        inline void BSSwitch()
        {
            ++m_curindex;
        }

        inline void BSReset()
        {
            m_curindex = 0;
        }

    private:
        inline uint64_t Load(size_t index)
        {
            return *(uint64_t*)(m_nexts[index]);
        }

        inline uint64_t GetUnmasked()
        {
            return m_bufs[m_curindex] >> m_bitposs[m_curindex];
        }

        inline void FillBitWindow16()
        {
            if (m_bitposs[m_curindex] >= 48)
            {
                m_bufs[m_curindex] >>= 48;
                m_bitposs[m_curindex] ^= 48;
                m_bufs[m_curindex] |= Load(m_curindex) << 16;
                m_nexts[m_curindex] += 6;
            }
        }

        inline void FillBitWindow(uint32_t n)
        {
            if (n <= 8)
            {
                if (m_bitposs[m_curindex] >= 56)
                {
                    m_bufs[m_curindex] >>= 56;
                    m_bitposs[m_curindex] ^= 56;
                    m_bufs[m_curindex] |= Load(m_curindex) << 8;
                    m_nexts[m_curindex] += 7;
                }
            }
            else if (n <= 16)
            {
                if (m_bitposs[m_curindex] >= 48)
                {
                    m_bufs[m_curindex] >>= 48;
                    m_bitposs[m_curindex] ^= 48;
                    m_bufs[m_curindex] |= Load(m_curindex) << 16;
                    m_nexts[m_curindex] += 6;
                }
            }
            else
            {
                if (m_bitposs[m_curindex] >= 32)
                {
                    m_bufs[m_curindex] >>= 32;
                    m_bitposs[m_curindex] ^= 32;
                    m_bufs[m_curindex] |= Load(m_curindex) << 32;
                    m_nexts[m_curindex] += 4;
                }
            }
        }

        inline uint32_t Mask(uint32_t n)
        {
            return BrotligBitMask[n];
        }

        size_t m_numbitstreams;
        uint64_t m_bufs[BROTLIG_MAX_NUM_BITSTREAMS];
        uint32_t m_bitposs[BROTLIG_MAX_NUM_BITSTREAMS];

        const uint8_t* m_nexts[BROTLIG_MAX_NUM_BITSTREAMS];

        uint32_t m_curindex : BROTLIG_DEFAULT_NUM_BITSTREAMS_LOG;
    };
}
