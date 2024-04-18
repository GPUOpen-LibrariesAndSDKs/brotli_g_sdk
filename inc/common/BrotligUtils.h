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

/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#pragma once

extern "C" {
#include "brotli/c/include/brotli/types.h"
#include "brotli/c/common/constants.h"
#include "brotli/c/enc/entropy_encode.h"
#include "brotli/c/enc/quality.h"
#include "brotli/c/common/context.h"
#include "brotli/c/enc/bit_cost.h"
}

#include "common/BrotligCommon.h"
#include "common/BrotligReverseBits.h"

namespace BrotliG
{
    size_t ComputeFileSize(std::string filename);

    uint32_t Log2Floor(uint32_t x);

    template<typename T>
    static inline uint32_t CountBits(T a)
    {
        uint32_t n = 0;

        while (0 == (a & 1) && n < sizeof(T) * 8)
        {
            a >>= 1;
            ++n;
        }

        return n;
    }

    inline uint16_t BrotligReverseBits15(uint16_t bits)
    {
        return sBrotligReverseBits15[bits];
    }

    inline uint16_t BrotligReverseBits(size_t num_bits, uint16_t bits)
    {
        static const size_t kLut[16] = {  /* Pre-reversed 4-bit values. */
            0x00, 0x08, 0x04, 0x0C, 0x02, 0x0A, 0x06, 0x0E,
            0x01, 0x09, 0x05, 0x0D, 0x03, 0x0B, 0x07, 0x0F
        };

        size_t retval = kLut[bits & 0x0F];
        size_t i;
        for (i = 4; i < num_bits; i += 4) {
            retval <<= 4;
            bits = (uint16_t)(bits >> 4);
            retval |= kLut[bits & 0x0F];
        }
        retval >>= ((0 - num_bits) & 0x03);
        return (uint16_t)retval;
    }

    uint32_t Mask32(uint32_t n);

    uint32_t GetNumberOfProcessorsThreads();

    inline uint32_t RoundUp(uint32_t n, uint32_t m)
    {
        return ((n + m - 1) / m) * m;
    }

    void ComputeRLECodes(size_t size, uint8_t* data, uint8_t* rle_codes, size_t& num_rle_codes, uint8_t* rle_extra_bits, size_t& num_rle_extra_bits);
}
