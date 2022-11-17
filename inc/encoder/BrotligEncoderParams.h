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
#include "brotli/c/enc/encoder_dict.h"
#include "brotli/c/enc/quality.h"
#include "brotli/c/common/transform.h"
}

#include "common/BrotligCommon.h"

namespace BrotliG
{
    const uint16_t gStaticDictionaryHashWords[32768] = { 0 };
    const uint8_t gStaticDictionaryHashLengths[32768] = { 0 };
    const uint16_t gStaticDictionaryBuckets[32768] = { 0 };

    /* Opaque type for pointer to different possible internal structures containing
       dictionary prepared for the encoder */
    typedef struct BrotliEncoderPreparedDictionaryStruct
        BrotliEncoderPreparedDictionary;

    typedef struct BrotlgiHasherParamsStruct {
        int type;
        int bucket_bits;
        int block_bits;
        int hash_len;
        int num_last_distances_to_check;
    } BrotligHasherParams;

    typedef struct BrotligDistanceParams {
        uint32_t distance_postfix_bits;
        uint32_t num_direct_distance_codes;
        uint32_t alphabet_size_max;
        uint32_t alphabet_size_limit;
        size_t max_distance;
    } BrotligDistanceParams;

    typedef struct BrotligEncoderParams
    {
        BrotliEncoderMode mode;
        int quality;
        int lgwin;
        int lgblock;
        size_t size_hint;
        bool disable_literal_context_modeling;
        bool large_window;
        BrotligHasherParams hasher_params;
        BrotligDistanceParams dist_params;
        BrotliEncoderDictionary dictionary;

        size_t page_size;
        size_t num_bitstreams;
        size_t cmd_group_size;
        size_t swizzle_size;

        BrotligEncoderParams()
        {
            mode = BROTLI_DEFAULT_MODE;
            large_window = false;
            quality = BROTLI_DEFAULT_QUALITY;
            lgwin = BROTLI_DEFAULT_WINDOW;
            lgblock = 0;
            size_hint = 0;
            disable_literal_context_modeling = false;
            page_size = 65536;
            num_bitstreams = 32;
            cmd_group_size = 1;
            swizzle_size = 4;
            BrotligInitEncoderDictionary(&dictionary);
            dist_params.distance_postfix_bits = 0;
            dist_params.num_direct_distance_codes = 0;
            dist_params.alphabet_size_max =
                BROTLI_DISTANCE_ALPHABET_SIZE(0, 0, BROTLI_MAX_DISTANCE_BITS);
            dist_params.alphabet_size_limit = dist_params.alphabet_size_max;
            dist_params.max_distance = BROTLI_MAX_DISTANCE;
        }

        BrotligEncoderParams(
            size_t p_size,
            size_t n_bitstreams,
            size_t c_group_size,
            size_t s_size
        )
        {
            mode = BROTLI_DEFAULT_MODE;
            large_window = false;
            quality = BROTLI_DEFAULT_QUALITY;
            lgwin = BROTLI_DEFAULT_WINDOW;
            lgblock = 0;
            size_hint = 0;
            disable_literal_context_modeling = false;
            page_size = p_size;
            num_bitstreams = n_bitstreams;
            cmd_group_size = c_group_size;
            swizzle_size = s_size;
            BrotligInitEncoderDictionary(&dictionary);
            dist_params.distance_postfix_bits = 0;
            dist_params.num_direct_distance_codes = 0;
            dist_params.alphabet_size_max =
                BROTLI_DISTANCE_ALPHABET_SIZE(0, 0, BROTLI_MAX_DISTANCE_BITS);
            dist_params.alphabet_size_limit = dist_params.alphabet_size_max;
            dist_params.max_distance = BROTLI_MAX_DISTANCE;
        }

        BrotligEncoderParams& operator=(const BrotligEncoderParams& other)
        {
            this->mode = other.mode;
            this->large_window = other.large_window;
            this->quality = other.quality;
            this->lgwin = other.lgwin;
            this->lgblock = other.lgblock;
            this->size_hint = other.size_hint;
            this->disable_literal_context_modeling = other.disable_literal_context_modeling;
            this->page_size = other.page_size;
            this->num_bitstreams = other.num_bitstreams;
            this->cmd_group_size = other.cmd_group_size;
            this->swizzle_size = other.swizzle_size;
            this->dictionary = other.dictionary;
            this->dist_params.distance_postfix_bits = other.dist_params.distance_postfix_bits;
            this->dist_params.num_direct_distance_codes = other.dist_params.num_direct_distance_codes;
            this->dist_params.alphabet_size_max = other.dist_params.alphabet_size_max;
            this->dist_params.alphabet_size_limit = other.dist_params.alphabet_size_limit;
            this->dist_params.max_distance = other.dist_params.max_distance;

            return *this;
        }

        void BrotligInitEncoderDictionary(BrotliEncoderDictionary* dict)
        {
            dict->words = BrotliGetDictionary();
            dict->num_transforms = (uint32_t)BrotliGetTransforms()->num_transforms;

            dict->hash_table_words = gStaticDictionaryHashWords;
            dict->hash_table_lengths = gStaticDictionaryHashLengths;
            dict->buckets = gStaticDictionaryBuckets;
            dict->dict_words = kStaticDictionaryWords;

            dict->cutoffTransformsCount = kCutoffTransformsCount;
            dict->cutoffTransforms = kCutoffTransforms;
        }

        void UpdateParams(size_t input_size)
        {
            size_hint = input_size < (1 << 30) ? input_size : (1u << 30);
            uint32_t tlgwin = BROTLI_MIN_WINDOW_BITS;
            while (BROTLI_MAX_BACKWARD_LIMIT(tlgwin) < (uint64_t)input_size - 16) {
                tlgwin++;
                if (tlgwin == BROTLI_MAX_WINDOW_BITS) break;
            }
            lgwin = tlgwin;

            Sanitize();
            ComputeLgBlock();
            ChooseDistanceParams();
        }

        void Sanitize()
        {
            quality = std::min(BROTLI_MAX_QUALITY, std::max(BROTLI_MIN_QUALITY, quality));
            if (quality <= MAX_QUALITY_FOR_STATIC_ENTROPY_CODES)
            {
                large_window = false;
            }
            if (lgwin < BROTLI_MIN_WINDOW_BITS)
            {
                lgwin = BROTLI_MIN_WINDOW_BITS;
            }
            else
            {
                int max_lgwin = large_window ? BROTLI_LARGE_MAX_WINDOW_BITS : BROTLI_MAX_WINDOW_BITS;
                if (lgwin > max_lgwin) lgwin = max_lgwin;
            }
        }

        void ComputeLgBlock()
        {
            int lgblock_t = lgblock;
            if (quality == FAST_ONE_PASS_COMPRESSION_QUALITY ||
                quality == FAST_TWO_PASS_COMPRESSION_QUALITY) {
                lgblock_t = lgwin;
            }
            else if (quality < MIN_QUALITY_FOR_BLOCK_SPLIT) {
                lgblock_t = 14;
            }
            else if (lgblock_t == 0) {
                lgblock_t = 16;
                if (quality >= 9 && lgwin > lgblock_t) {
                    lgblock_t = std::min(18, lgwin);
                }
            }
            else {
                lgblock_t = BROTLI_MIN(int, BROTLI_MAX_INPUT_BLOCK_BITS,
                    BROTLI_MAX(int, BROTLI_MIN_INPUT_BLOCK_BITS, lgblock_t));
            }

            lgblock = lgblock_t;
        }

        void ChooseDistanceParams()
        {
            uint32_t distance_postfix_bits = 0;
            uint32_t num_direct_distance_codes = 0;

            if (quality >= MIN_QUALITY_FOR_NONZERO_DISTANCE_PARAMS) {
                uint32_t ndirect_msb;
                if (mode == BROTLI_MODE_FONT) {
                    distance_postfix_bits = 1;
                    num_direct_distance_codes = 12;
                }
                else {
                    distance_postfix_bits = dist_params.distance_postfix_bits;
                    num_direct_distance_codes = dist_params.num_direct_distance_codes;
                }
                ndirect_msb = (num_direct_distance_codes >> distance_postfix_bits) & 0x0F;
                if (distance_postfix_bits > BROTLI_MAX_NPOSTFIX ||
                    num_direct_distance_codes > BROTLI_MAX_NDIRECT ||
                    (ndirect_msb << distance_postfix_bits) != num_direct_distance_codes) {
                    distance_postfix_bits = 0;
                    num_direct_distance_codes = 0;
                }
            }

            uint32_t alphabet_size_max;
            uint32_t alphabet_size_limit;
            uint32_t max_distance;

            dist_params.distance_postfix_bits = distance_postfix_bits;
            dist_params.num_direct_distance_codes = num_direct_distance_codes;

            alphabet_size_max = BROTLI_DISTANCE_ALPHABET_SIZE(
                distance_postfix_bits, num_direct_distance_codes, BROTLI_MAX_DISTANCE_BITS);
            alphabet_size_limit = alphabet_size_max;
            max_distance = num_direct_distance_codes + (1U << (BROTLI_MAX_DISTANCE_BITS + distance_postfix_bits + 2)) -
                (1U << (distance_postfix_bits + 2));

            if (large_window) {
                BrotliDistanceCodeLimit limit = BrotliCalculateDistanceCodeLimit(
                    BROTLI_MAX_ALLOWED_DISTANCE, distance_postfix_bits, num_direct_distance_codes);
                alphabet_size_max = BROTLI_DISTANCE_ALPHABET_SIZE(
                    distance_postfix_bits, num_direct_distance_codes, BROTLI_LARGE_MAX_DISTANCE_BITS);
                alphabet_size_limit = limit.max_alphabet_size;
                max_distance = limit.max_distance;
            }

            dist_params.alphabet_size_max = alphabet_size_max;
            dist_params.alphabet_size_limit = alphabet_size_limit;
            dist_params.max_distance = max_distance;
        }
    }BrotligEncoderParams;
}