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

#include <cassert>

#include "common/BrotligConstants.h"

#include "BrotligUtils.h"

size_t BrotliG::ComputeFileSize(std::string filename)
{
    std::ifstream file(filename, std::ios::in | std::ios::binary);
    if (!file.is_open())
        return 0;
    const auto fileStart = file.tellg();
    file.seekg(0, std::ios::end);
    const auto fileEnd = file.tellg();
    file.seekg(0, std::ios::beg);

    size_t fileSize = static_cast<size_t>(fileEnd - fileStart);

    file.close();

    return fileSize;
}

uint32_t BrotliG::Log2Floor(uint32_t x) {
    uint32_t result = 0;
    while (x) {
        x >>= 1;
        ++result;
    }
    return result;
}

uint32_t BrotliG::Mask32(uint32_t n)
{
    return (1u << n) - 1;
}

uint32_t BrotliG::GetNumberOfProcessorsThreads()
{
#ifndef _WIN32
    //    return sysconf(_SC_NPROCESSORS_ONLN);
    return std::thread::hardware_concurrency();
#else
    // Figure out how many cores there are on this machine
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return (sysinfo.dwNumberOfProcessors);
#endif
}

static void ComputeRLEZerosReps(
    size_t reps,
    uint8_t rle_codes[],
    size_t& num_rle_codes,
    uint8_t rle_extra_bits[],
    size_t& num_rle_extra_bits)
{
    size_t minus = 0;
    if (reps == 11)
    {
        rle_codes[num_rle_codes++] = 0;
        rle_extra_bits[num_rle_extra_bits++] = 0;
        --reps;
    }

    if (reps < 3)
    {
        while (reps--)
        {
            rle_codes[num_rle_codes++] = 0;
            rle_extra_bits[num_rle_extra_bits++] = 0;
        }
    }
    else
    {
        while (true)
        {
            minus = (reps > 10) ? 10 : reps;
            reps -= minus;
            rle_codes[num_rle_codes++] = BROTLI_REPEAT_ZERO_CODE_LENGTH;
            rle_extra_bits[num_rle_extra_bits++] = uint8_t(minus - 3);
            if (reps < 3) break;
        }

        while (reps--)
        {
            rle_codes[num_rle_codes++] = 0;
            rle_extra_bits[num_rle_extra_bits++] = 0;
        }
    }
}

void ComputeRLEReps(
    const uint8_t prev_value,
    const uint8_t value,
    size_t reps,
    uint8_t rle_codes[],
    size_t& num_rle_codes,
    uint8_t rle_extra_bits[],
    size_t& num_rle_extra_bits
)
{
    assert(reps > 0);
    size_t minus = 0;
    if (prev_value != value)
    {
        rle_codes[num_rle_codes++] = value;
        rle_extra_bits[num_rle_extra_bits++] = 0;
        --reps;
    }

    if (reps == 7)
    {
        rle_codes[num_rle_codes++] = value;
        rle_extra_bits[num_rle_extra_bits++] = 0;
        --reps;
    }

    if (reps < 3)
    {
        while (reps--)
        {
            rle_codes[num_rle_codes++] = value;
            rle_extra_bits[num_rle_extra_bits++] = 0;
        }
    }
    else
    {

        while (true)
        {
            minus = (reps > 6) ? 6 : reps;
            reps -= minus;
            rle_codes[num_rle_codes++] = BROTLI_REPEAT_PREVIOUS_CODE_LENGTH;
            rle_extra_bits[num_rle_extra_bits++] = uint8_t(minus - 3);
            if (reps < 3) break;
        }

        while (reps--)
        {
            rle_codes[num_rle_codes++] = value;
            rle_extra_bits[num_rle_extra_bits++] = 0;
        }
    }
}

void BrotliG::ComputeRLECodes(
    size_t size, 
    uint8_t* data, 
    uint8_t* rle_codes, 
    size_t& num_rle_codes,
    uint8_t* rle_extra_bits, 
    size_t& num_rle_extra_bits)
{
    bool use_rle_for_non_zeros = true;
    bool use_rle_for_zeros = true;

    // Compute rle codes
    uint8_t value = 0, prev_value = BROTLI_INITIAL_REPEATED_CODE_LENGTH;
    size_t i = 0, k = 0, reps = 0;
    for (i = 0; i < size;)
    {
        value = data[i];

        reps = 1;
        if (i == 0)
        {
            rle_codes[num_rle_codes++] = value;
            rle_extra_bits[num_rle_extra_bits++] = 0;
        }
        else
        {
            if ((value != 0 && use_rle_for_non_zeros) || (value == 0 && use_rle_for_zeros))
                for (k = i + 1; k < size && data[k] == value; ++k) ++reps;

            if (value == 0)
            {
                ComputeRLEZerosReps(
                    reps,
                    rle_codes,
                    num_rle_codes,
                    rle_extra_bits,
                    num_rle_extra_bits
                );
            }
            else
            {
                ComputeRLEReps(
                    prev_value,
                    value,
                    reps,
                    rle_codes,
                    num_rle_codes,
                    rle_extra_bits,
                    num_rle_extra_bits
                );
            }
        }

        prev_value = value;
        i += reps;
    }
}
