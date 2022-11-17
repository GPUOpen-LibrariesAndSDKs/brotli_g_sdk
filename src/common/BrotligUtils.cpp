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

#include <cassert>

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

void BrotliG::EncodeWindowBits(int lgwin, bool large_window,
    uint16_t* last_bytes, uint8_t* last_bytes_bits) {
    /*if (large_window) {
        *last_bytes = (uint16_t)(((lgwin & 0x3F) << 8) | 0x11);
        *last_bytes_bits = 14;
    }
    else {
        if (lgwin == 16) {
            *last_bytes = 0;
            *last_bytes_bits = 1;
        }
        else if (lgwin == 17) {
            *last_bytes = 1;
            *last_bytes_bits = 7;
        }
        else if (lgwin > 17) {
            *last_bytes = (uint16_t)(((lgwin - 17) << 1) | 0x01);
            *last_bytes_bits = 4;
        }
        else {
            *last_bytes = (uint16_t)(((lgwin - 8) << 4) | 0x01);
            *last_bytes_bits = 7;
        }
    }*/
    assert(lgwin >= 10 && lgwin <= 24);
    *last_bytes = (uint8_t)lgwin;
    *last_bytes_bits = 8;
}

/* Wraps 64-bit input position to 32-bit ring-buffer position preserving
   "not-a-first-lap" feature. */
uint32_t BrotliG::WrapPosition(uint64_t position) {
    uint32_t result = (uint32_t)position;
    uint64_t gb = position >> 30;
    if (gb > 2) {
        /* Wrap every 2GiB; The first 3GB are continuous. */
        result = (result & ((1u << 30) - 1)) | ((uint32_t)((gb - 1) & 1) + 1) << 30;
    }
    return result;
}

uint32_t BrotliG::Log2Floor(uint32_t x) {
    uint32_t result = 0;
    while (x) {
        x >>= 1;
        ++result;
    }
    return result;
}

bool BrotliG::ShouldCompress(
    const uint8_t* data, const size_t mask, const uint64_t last_flush_pos,
    const size_t bytes, const size_t num_literals, const size_t num_commands) {
    /* TODO: find more precise minimal block overhead. */
    if (bytes <= 2) return false;
    if (num_commands < (bytes >> 8) + 2) {
        if ((double)num_literals > 0.99 * (double)bytes) {
            uint32_t literal_histo[256] = { 0 };
            static const uint32_t kSampleRate = 13;
            static const double kMinEntropy = 7.92;
            const double bit_cost_threshold =
                (double)bytes * kMinEntropy / kSampleRate;
            size_t t = (bytes + kSampleRate - 1) / kSampleRate;
            uint32_t pos = (uint32_t)last_flush_pos;
            size_t i;
            for (i = 0; i < t; i++) {
                ++literal_histo[data[pos & mask]];
                pos += kSampleRate;
            }
            if (BitsEntropy(literal_histo, 256) > bit_cost_threshold) {
                return false;
            }
        }
    }
    return true;
}

/* Decide if we want to use a more complex static context map containing 13
   context values, based on the entropy reduction of histograms over the
   first 5 bits of literals. */
bool BrotliG::ShouldUseComplexStaticContextMap(const uint8_t* input,
    size_t start_pos, size_t length, size_t mask, int quality, size_t size_hint,
    size_t* num_literal_contexts, const uint32_t** literal_context_map,
    uint32_t* arena) {
    static const uint32_t kStaticContextMapComplexUTF8[64] = {
      11, 11, 12, 12, /* 0 special */
      0, 0, 0, 0, /* 4 lf */
      1, 1, 9, 9, /* 8 space */
      2, 2, 2, 2, /* !, first after space/lf and after something else. */
      1, 1, 1, 1, /* " */
      8, 3, 3, 3, /* % */
      1, 1, 1, 1, /* ({[ */
      2, 2, 2, 2, /* }]) */
      8, 4, 4, 4, /* :; */
      8, 7, 4, 4, /* . */
      8, 0, 0, 0, /* > */
      3, 3, 3, 3, /* [0..9] */
      5, 5, 10, 5, /* [A-Z] */
      5, 5, 10, 5,
      6, 6, 6, 6, /* [a-z] */
      6, 6, 6, 6,
    };
    BROTLI_UNUSED(quality);
    /* Try the more complex static context map only for long data. */
    if (size_hint < (1 << 20)) {
        return BROTLI_FALSE;
    }
    else {
        const size_t end_pos = start_pos + length;
        /* To make entropy calculations faster, we collect histograms
           over the 5 most significant bits of literals. One histogram
           without context and 13 additional histograms for each context value. */
        uint32_t* BROTLI_RESTRICT const combined_histo = arena;
        uint32_t* BROTLI_RESTRICT const context_histo = arena + 32;
        uint32_t total = 0;
        double entropy[3];
        size_t dummy;
        size_t i;
        ContextLut utf8_lut = BROTLI_CONTEXT_LUT(CONTEXT_UTF8);
        memset(arena, 0, sizeof(arena[0]) * 32 * 14);
        for (; start_pos + 64 <= end_pos; start_pos += 4096) {
            const size_t stride_end_pos = start_pos + 64;
            uint8_t prev2 = input[start_pos & mask];
            uint8_t prev1 = input[(start_pos + 1) & mask];
            size_t pos;
            /* To make the analysis of the data faster we only examine 64 byte long
               strides at every 4kB intervals. */
            for (pos = start_pos + 2; pos < stride_end_pos; ++pos) {
                const uint8_t literal = input[pos & mask];
                const uint8_t context = (uint8_t)kStaticContextMapComplexUTF8[
                    BROTLI_CONTEXT(prev1, prev2, utf8_lut)];
                ++total;
                ++combined_histo[literal >> 3];
                ++context_histo[(context << 5) + (literal >> 3)];
                prev2 = prev1;
                prev1 = literal;
            }
        }
        entropy[1] = ShannonEntropy(combined_histo, 32, &dummy);
        entropy[2] = 0;
        for (i = 0; i < 13; ++i) {
            entropy[2] += ShannonEntropy(context_histo + (i << 5), 32, &dummy);
        }
        entropy[0] = 1.0 / (double)total;
        entropy[1] *= entropy[0];
        entropy[2] *= entropy[0];
        /* The triggering heuristics below were tuned by compressing the individual
           files of the silesia corpus. If we skip this kind of context modeling
           for not very well compressible input (i.e. entropy using context modeling
           is 60% of maximal entropy) or if expected savings by symbol are less
           than 0.2 bits, then in every case when it triggers, the final compression
           ratio is improved. Note however that this heuristics might be too strict
           for some cases and could be tuned further. */
        if (entropy[2] > 3.0 || entropy[1] - entropy[2] < 0.2) {
            return BROTLI_FALSE;
        }
        else {
            *num_literal_contexts = 13;
            *literal_context_map = kStaticContextMapComplexUTF8;
            return BROTLI_TRUE;
        }
    }
}

void BrotliG::DecideOverLiteralContextModeling(const uint8_t* input,
    size_t start_pos, size_t length, size_t mask, int quality, size_t size_hint,
    size_t* num_literal_contexts, const uint32_t** literal_context_map,
    uint32_t* arena) {
    if (quality < MIN_QUALITY_FOR_CONTEXT_MODELING || length < 64) {
        return;
    }
    else if (BrotliG::ShouldUseComplexStaticContextMap(
        input, start_pos, length, mask, quality, size_hint,
        num_literal_contexts, literal_context_map, arena)) {
        /* Context map was already set, nothing else to do. */
    }
    else {
        /* Gather bi-gram data of the UTF8 byte prefixes. To make the analysis of
           UTF8 data faster we only examine 64 byte long strides at every 4kB
           intervals. */
        const size_t end_pos = start_pos + length;
        uint32_t* BROTLI_RESTRICT const bigram_prefix_histo = arena;
        memset(bigram_prefix_histo, 0, sizeof(arena[0]) * 9);
        for (; start_pos + 64 <= end_pos; start_pos += 4096) {
            static const int lut[4] = { 0, 0, 1, 2 };
            const size_t stride_end_pos = start_pos + 64;
            int prev = lut[input[start_pos & mask] >> 6] * 3;
            size_t pos;
            for (pos = start_pos + 1; pos < stride_end_pos; ++pos) {
                const uint8_t literal = input[pos & mask];
                ++bigram_prefix_histo[prev + lut[literal >> 6]];
                prev = lut[literal >> 6] * 3;
            }
        }
        BrotliG::ChooseContextMap(quality, &bigram_prefix_histo[0], num_literal_contexts,
            literal_context_map);
    }
}

/* Decide about the context map based on the ability of the prediction
   ability of the previous byte UTF8-prefix on the next byte. The
   prediction ability is calculated as Shannon entropy. Here we need
   Shannon entropy instead of 'BitsEntropy' since the prefix will be
   encoded with the remaining 6 bits of the following byte, and
   BitsEntropy will assume that symbol to be stored alone using Huffman
   coding. */
void BrotliG::ChooseContextMap(int quality,
    uint32_t* bigram_histo,
    size_t* num_literal_contexts,
    const uint32_t** literal_context_map) {
    static const uint32_t kStaticContextMapContinuation[64] = {
      1, 1, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    static const uint32_t kStaticContextMapSimpleUTF8[64] = {
      0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };

    uint32_t monogram_histo[3] = { 0 };
    uint32_t two_prefix_histo[6] = { 0 };
    size_t total;
    size_t i;
    size_t dummy;
    double entropy[4];
    for (i = 0; i < 9; ++i) {
        monogram_histo[i % 3] += bigram_histo[i];
        two_prefix_histo[i % 6] += bigram_histo[i];
    }
    entropy[1] = ShannonEntropy(monogram_histo, 3, &dummy);
    entropy[2] = (ShannonEntropy(two_prefix_histo, 3, &dummy) +
        ShannonEntropy(two_prefix_histo + 3, 3, &dummy));
    entropy[3] = 0;
    for (i = 0; i < 3; ++i) {
        entropy[3] += ShannonEntropy(bigram_histo + 3 * i, 3, &dummy);
    }

    total = monogram_histo[0] + monogram_histo[1] + monogram_histo[2];
    BROTLI_DCHECK(total != 0);
    entropy[0] = 1.0 / (double)total;
    entropy[1] *= entropy[0];
    entropy[2] *= entropy[0];
    entropy[3] *= entropy[0];

    if (quality < MIN_QUALITY_FOR_HQ_CONTEXT_MODELING) {
        /* 3 context models is a bit slower, don't use it at lower qualities. */
        entropy[3] = entropy[1] * 10;
    }
    /* If expected savings by symbol are less than 0.2 bits, skip the
       context modeling -- in exchange for faster decoding speed. */
    if (entropy[1] - entropy[2] < 0.2 &&
        entropy[1] - entropy[3] < 0.2) {
        *num_literal_contexts = 1;
    }
    else if (entropy[2] - entropy[3] < 0.02) {
        *num_literal_contexts = 2;
        *literal_context_map = kStaticContextMapSimpleUTF8;
    }
    else {
        *num_literal_contexts = 3;
        *literal_context_map = kStaticContextMapContinuation;
    }
}

void BrotliG::MoveToFront(std::vector<uint8_t>& v, size_t index)
{
    uint8_t value = v.at(index);
    size_t i = 0;
    for (i = index; i != 0; --i)
    {
        v.at(i) = v.at(i - 1);
    }
    v.at(0) = value;
}

std::vector<uint32_t> BrotliG::MoveToFrontTransform(const std::vector<uint32_t>& in)
{
    std::vector<uint32_t> out;
    if (in.size() == 0)
        return out;

    std::vector<uint8_t> mtf;
    uint32_t max_value = *std::max_element(in.begin(), in.end());
    out.resize(in.size());

    assert(max_value < 256u);
    for (size_t i = 0; i <= max_value; ++i)
    {
        mtf.push_back((uint8_t)i);
    }

    {
        std::vector<uint8_t>::iterator pos;
        for (size_t i = 0; i < in.size(); ++i)
        {
            pos = std::find(mtf.begin(), mtf.end(), in.at(i));
            assert(pos != mtf.end());
            uint32_t index = static_cast<uint32_t>(pos - mtf.begin());
            out.at(i) = index;
            MoveToFront(mtf, index);
        }
    }

    return out;
}

void BrotliG::RunLengthCodeZeros(
    std::vector<uint32_t> v,
    size_t& out_size,
    uint32_t& max_run_length_prefix)
{
    uint32_t max_reps = 0;
    size_t i = 0;
    uint32_t max_prefix = 0;
    size_t in_size = v.size();
    for (i = 0; i < in_size;)
    {
        uint32_t reps = 0;
        for (; i < in_size && v[i] != 0; ++i);
        for (; i < in_size && v[i] == 0; ++i) {
            ++reps;
        }
        max_reps = std::max(reps, max_reps);
    }

    max_prefix = max_reps > 0 ? Log2FloorNonZero(max_reps) : 0;
    max_prefix = std::min(max_prefix, max_run_length_prefix);
    max_run_length_prefix = max_prefix;
    out_size = 0;

    for (i = 0; i < in_size;)
    {
        assert(out_size <= i);
        if (v.at(i) != 0)
        {
            v.at(out_size) = v.at(i) + max_run_length_prefix;
            ++i;
            ++out_size;
        }
        else
        {
            uint32_t reps = 1;
            size_t k = 0;
            for (k = i + 1; k < in_size && v[k] == 0; ++k)
            {
                ++reps;
            }
            i += reps;
            while (reps != 0)
            {
                if (reps < (2u << max_prefix))
                {
                    uint32_t run_length_prefix = Log2FloorNonZero(reps);
                    const uint32_t extra_bits = reps - (1u << run_length_prefix);
                    v.at(out_size) = run_length_prefix + (extra_bits << 9);
                    ++out_size;
                    break;
                }
                else
                {
                    const uint32_t extra_bits = (1u << max_prefix) - 1u;
                    v.at(out_size) = max_prefix + (extra_bits << 9);
                    reps -= (2u << max_prefix) - 1u;
                    ++out_size;
                }
            }
        }
    }
}

#define R2(n) n, n + 2 * 64, n + 1 * 64, n + 3 * 64

#define R4(n)                                              \
    R2(n), R2(n + 2 * 16), R2(n + 1 * 16), R2(n + 3 * 16)

#define R6(n)                                              \
    R4(n), R4(n + 2 * 4), R4(n + 1 * 4), R4(n + 3 * 4)

uint16_t reverseBitsLookuptable[256]
= { R6(0), R6(2), R6(1), R6(3) };

uint16_t BrotliG::BrotligReverse16Bits(uint16_t bits)
{
    return reverseBitsLookuptable[bits & 0xff] << 8
        | reverseBitsLookuptable[(bits >> 8) & 0xff];
}

uint16_t BrotliG::BrotligReverseBits(size_t num_bits, uint16_t bits) {
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

void BrotliG::EncodeMLen(
    size_t length,
    uint64_t& bits,
    size_t& numbits,
    uint64_t& nibblebits)
{
    size_t lg = (length == 1) ? 1 : Log2FloorNonZero((uint32_t)(length - 1)) + 1;
    size_t mnibbles = (lg < 16 ? 16 : (lg + 3)) / 4;
    assert(length > 0);
    assert(length <= (1 << 24));
    assert(lg <= 24);
    nibblebits = mnibbles - 4;
    numbits = mnibbles * 4;
    bits = length - 1;
}

uint32_t BrotliG::Mask32(uint32_t n)
{
    return (1u << n) - 1;
}

std::string BrotliG::ByteToBinaryString(uint8_t byte)
{
    std::string binaryStr = "";
    for(size_t i = 0;i < 8; ++i)
    {
        binaryStr += (byte & (1 << i)) > 0 ? "1" : "0";
    }

    std::reverse(binaryStr.begin(), binaryStr.end());

    return binaryStr;
}

std::string BrotliG::ToBinaryString(uint16_t code, size_t codelen)
{
    std::string codeStr = "";
    for (size_t i = 0; i < codelen; ++i)
    {
        codeStr += (code & (1 << i)) > 0 ? "1" : "0";
    }

    return codeStr;
}