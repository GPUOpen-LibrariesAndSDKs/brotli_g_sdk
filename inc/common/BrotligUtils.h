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

#include "BrotligCommon.h"

namespace BrotliG
{
    size_t ComputeFileSize(std::string filename);

    void EncodeWindowBits(int lgwin, bool large_window,
        uint16_t* last_bytes, uint8_t* last_bytes_bits);

    /* Wraps 64-bit input position to 32-bit ring-buffer position preserving
       "not-a-first-lap" feature. */
    uint32_t WrapPosition(uint64_t position);

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

    bool ShouldCompress(
        const uint8_t* data, const size_t mask, const uint64_t last_flush_pos,
        const size_t bytes, const size_t num_literals, const size_t num_commands);

    /* Decide if we want to use a more complex static context map containing 13
       context values, based on the entropy reduction of histograms over the
       first 5 bits of literals. */
    bool ShouldUseComplexStaticContextMap(const uint8_t* input,
        size_t start_pos, size_t length, size_t mask, int quality, size_t size_hint,
        size_t* num_literal_contexts, const uint32_t** literal_context_map,
        uint32_t* arena);

    void DecideOverLiteralContextModeling(const uint8_t* input,
        size_t start_pos, size_t length, size_t mask, int quality, size_t size_hint,
        size_t* num_literal_contexts, const uint32_t** literal_context_map,
        uint32_t* arena);

    /* Decide about the context map based on the ability of the prediction
       ability of the previous byte UTF8-prefix on the next byte. The
       prediction ability is calculated as Shannon entropy. Here we need
       Shannon entropy instead of 'BitsEntropy' since the prefix will be
       encoded with the remaining 6 bits of the following byte, and
       BitsEntropy will assume that symbol to be stored alone using Huffman
       coding. */
    void ChooseContextMap(int quality,
        uint32_t* bigram_histo,
        size_t* num_literal_contexts,
        const uint32_t** literal_context_map);

    void MoveToFront(std::vector<uint8_t>& v, size_t index);
    std::vector<uint32_t> MoveToFrontTransform(const std::vector<uint32_t>& in);

    /* Finds runs of zeros in v[0..in_size) and replaces them with a prefix code of
       the run length plus extra bits (lower 9 bits is the prefix code and the rest
       are the extra bits). Non-zero values in v[] are shifted by
       *max_length_prefix. Will not create prefix codes bigger than the initial
       value of *max_run_length_prefix. The prefix code of run length L is simply
       Log2Floor(L) and the number of extra bits is the same as the prefix code. */
    void RunLengthCodeZeros(std::vector<uint32_t> v, size_t& out_size, uint32_t& max_run_length_prefix);

    uint16_t BrotligReverse16Bits(uint16_t bits);
    uint16_t BrotligReverseBits(size_t num_bits, uint16_t bits);

    void EncodeMLen(
        size_t length,
        uint64_t& bits,
        size_t& numbits,
        uint64_t& nibblebits
    );

    uint32_t Mask32(uint32_t n);

    std::string ByteToBinaryString(uint8_t byte);
    std::string ToBinaryString(uint16_t code, size_t codelen);
}