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
#include "brotli/c/common/transform.h"

#include "brotli/c/enc/command.h"
#include "brotli/c/enc/ringbuffer.h"
#include "brotli/c/enc/hash.h"
#include "brotli/c/enc/quality.h"
#include "brotli/c/enc/metablock.h"

#include "brotli/encode.h"
}

#include "common/BrotligSwizzler.h"
#include "common/BrotligDataConditioner.h"

#include "DataStream.h"

namespace BrotliG
{
    const uint16_t gStaticDictionaryHashWords[32768] = { 0 };
    const uint8_t gStaticDictionaryHashLengths[32768] = { 0 };
    const uint16_t gStaticDictionaryBuckets[32768] = { 0 };
    
    typedef struct BrotligEncoderParams
    {
        BrotliEncoderMode mode;
        int quality;
        int lgwin;

        size_t page_size;
        size_t num_bitstreams;
        size_t cmd_group_size;
        size_t swizzle_size;

        BrotligEncoderParams()
        {
            mode = BROTLI_DEFAULT_MODE;
            quality = BROTLI_DEFAULT_QUALITY;
            lgwin = BROTLI_DEFAULT_WINDOW;

            page_size = BROTLIG_DEFAULT_PAGE_SIZE;
            num_bitstreams = BROLTIG_DEFAULT_NUM_BITSTREAMS;
            cmd_group_size = BROTLIG_COMMAND_GROUP_SIZE;
            swizzle_size = BROTLIG_SWIZZLE_SIZE;
        }

        BrotligEncoderParams(
            int quality,
            int lgwin,
            size_t p_size
        )
        {
            this->mode = BROTLI_DEFAULT_MODE;
            this->quality = quality;
            this->lgwin = lgwin;
            this->page_size = p_size;
            this->num_bitstreams = BROLTIG_DEFAULT_NUM_BITSTREAMS;
            this->cmd_group_size = BROTLIG_COMMAND_GROUP_SIZE;
            this->swizzle_size = BROTLIG_SWIZZLE_SIZE;
        }

        BrotligEncoderParams& operator=(const BrotligEncoderParams& other)
        {
            this->mode = other.mode;
            this->lgwin = other.lgwin;
            this->quality = other.quality;
            this->page_size = other.page_size;
            this->num_bitstreams = other.num_bitstreams;
            this->cmd_group_size = other.cmd_group_size;
            this->swizzle_size = other.swizzle_size;

            return *this;
        }
    } BrotligEncoderParams;

    typedef struct BrotligEncoderStateStruct
    {
        BrotliEncoderParams params;

        MemoryManager memory_manager_;

        uint64_t input_pos_;
        Command* commands_;
        size_t num_commands_;
        size_t num_literals_;
        size_t last_insert_len_;
        int dist_cache_[BROTLI_NUM_DISTANCE_SHORT_CODES];

        Hasher hasher_;

        BROTLI_BOOL is_initialized_;

        BrotligEncoderStateStruct(brotli_alloc_func alloc_func, brotli_free_func free_func, void* opaque)
        {
            BrotliInitMemoryManager(&memory_manager_, alloc_func, free_func, opaque);

            params.mode = BROTLI_DEFAULT_MODE;
            params.large_window = BROTLI_FALSE;
            params.quality = BROTLI_DEFAULT_QUALITY;
            params.lgwin = BROTLI_DEFAULT_WINDOW;
            params.lgblock = 0;
            params.stream_offset = 0;
            params.size_hint = 0;
            params.disable_literal_context_modeling = BROTLI_FALSE;
            BrotligInitEncoderDictionary(&params.dictionary);
            params.dist.distance_postfix_bits = 0;
            params.dist.num_direct_distance_codes = 0;
            params.dist.alphabet_size_max =
                BROTLI_DISTANCE_ALPHABET_SIZE(0, 0, BROTLI_MAX_DISTANCE_BITS);
            params.dist.alphabet_size_limit = params.dist.alphabet_size_max;
            params.dist.max_distance = BROTLI_MAX_DISTANCE;

            input_pos_ = 0;
            num_commands_ = 0;
            num_literals_ = 0;
            last_insert_len_ = 0;
            HasherInit(&hasher_);
            is_initialized_ = BROTLI_FALSE;

            commands_ = 0;

            /* Initialize distance cache. */
            dist_cache_[0] = 4;
            dist_cache_[1] = 11;
            dist_cache_[2] = 15;
            dist_cache_[3] = 16;
        }

        ~BrotligEncoderStateStruct()
        {
            MemoryManager* m = &memory_manager_;
            if (BROTLI_IS_OOM(m))
            {
                BrotliWipeOutMemoryManager(m);
                return;
            }

            BROTLI_FREE(m, commands_);
            DestroyHasher(m, &hasher_);
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

        BROTLI_BOOL SetParameter(BrotliEncoderParameter p, uint32_t value)
        {
            if (is_initialized_) return BROTLI_FALSE;
            /* TODO: Validate/clamp parameters here. */
            switch (p) {
            case BROTLI_PARAM_MODE:
                params.mode = (BrotliEncoderMode)value;
                return BROTLI_TRUE;

            case BROTLI_PARAM_QUALITY:
                params.quality = (int)value;
                return BROTLI_TRUE;

            case BROTLI_PARAM_LGWIN:
                params.lgwin = (int)value;
                return BROTLI_TRUE;

            case BROTLI_PARAM_LGBLOCK:
                params.lgblock = (int)value;
                return BROTLI_TRUE;

            case BROTLI_PARAM_DISABLE_LITERAL_CONTEXT_MODELING:
                if ((value != 0) && (value != 1)) return BROTLI_FALSE;
                params.disable_literal_context_modeling = TO_BROTLI_BOOL(!!value);
                return BROTLI_TRUE;

            case BROTLI_PARAM_SIZE_HINT:
                params.size_hint = value;
                return BROTLI_TRUE;

            case BROTLI_PARAM_LARGE_WINDOW:
                params.large_window = TO_BROTLI_BOOL(!!value);
                return BROTLI_TRUE;

            case BROTLI_PARAM_NPOSTFIX:
                params.dist.distance_postfix_bits = value;
                return BROTLI_TRUE;

            case BROTLI_PARAM_NDIRECT:
                params.dist.num_direct_distance_codes = value;
                return BROTLI_TRUE;

            case BROTLI_PARAM_STREAM_OFFSET:
                if (value > (1u << 30)) return BROTLI_FALSE;
                params.stream_offset = value;
                return BROTLI_TRUE;

            default: return BROTLI_FALSE;
            }
        }

        void BrotligChooseDistanceParams() {
            uint32_t distance_postfix_bits = 0;
            uint32_t num_direct_distance_codes = 0;

            if (params.quality >= MIN_QUALITY_FOR_NONZERO_DISTANCE_PARAMS) {
                uint32_t ndirect_msb;
                if (params.mode == BROTLI_MODE_FONT) {
                    distance_postfix_bits = 1;
                    num_direct_distance_codes = 12;
                }
                else {
                    distance_postfix_bits = params.dist.distance_postfix_bits;
                    num_direct_distance_codes = params.dist.num_direct_distance_codes;
                }
                ndirect_msb = (num_direct_distance_codes >> distance_postfix_bits) & 0x0F;
                if (distance_postfix_bits > BROTLI_MAX_NPOSTFIX ||
                    num_direct_distance_codes > BROTLI_MAX_NDIRECT ||
                    (ndirect_msb << distance_postfix_bits) != num_direct_distance_codes) {
                    distance_postfix_bits = 0;
                    num_direct_distance_codes = 0;
                }
            }

            BrotliInitDistanceParams(
                &params, distance_postfix_bits, num_direct_distance_codes);
        }

        bool EnsureInitialized()
        {
            if (BROTLI_IS_OOM(&memory_manager_)) return BROTLI_FALSE;
            if (is_initialized_) return BROTLI_TRUE;
            
            uint32_t tlgwin = BROTLI_MIN_WINDOW_BITS;
            while (BROTLI_MAX_BACKWARD_LIMIT(tlgwin) < (uint64_t)params.size_hint - 16) {
                tlgwin++;
                if (tlgwin == BROTLI_MAX_WINDOW_BITS) break;
            }
            params.lgwin = tlgwin;

            SanitizeParams(&params);
            params.lgblock = ComputeLgBlock(&params);
            BrotligChooseDistanceParams();

            return BROTLI_TRUE;
        }
    } BrotligEncoderState;

    class PageEncoder
    {
    public:
        PageEncoder();
        ~PageEncoder();

        static inline size_t MaxCompressedSize(size_t inputSize)
        {
            return 2 * BrotliEncoderMaxCompressedSize(inputSize);
        }

        bool Setup(BrotligEncoderParams& params, BrotligDataconditionParams* preconditioner);
        bool Run(const uint8_t* input, size_t inputSize, size_t inputOffset, uint8_t* output, size_t* outputSize, size_t outputOffset, bool isLast);
        void Cleanup();

    private:
        bool DeltaEncode(size_t page_start, size_t page_end, uint8_t* input);
        void DeltaEncodeByte(size_t inSize, uint8_t* inData);

        inline void StoreCommand(Command& cmd);
        inline void StoreLiteral(uint8_t literal);
        inline void StoreDistance(uint16_t dist, uint32_t distextra);

        BrotligEncoderParams m_params;
        BrotligDataconditionParams* m_dcparams;
        BrotligEncoderState* m_state;

        uint32_t m_histCommands[BROLTIG_NUM_COMMAND_SYMBOLS_EFFECTIVE];
        uint32_t m_histLiterals[BROTLI_NUM_LITERAL_SYMBOLS];
        uint32_t m_histDistances[BROTLIG_NUM_DISTANCE_SYMBOLS];

        uint16_t m_cmdCodes[BROLTIG_NUM_COMMAND_SYMBOLS_EFFECTIVE];
        uint8_t m_cmdCodelens[BROLTIG_NUM_COMMAND_SYMBOLS_EFFECTIVE];

        uint16_t m_litCodes[BROTLI_NUM_LITERAL_SYMBOLS];
        uint8_t m_litCodelens[BROTLI_NUM_LITERAL_SYMBOLS];

        uint16_t m_distCodes[BROTLIG_NUM_DISTANCE_SYMBOLS];
        uint8_t m_distCodelens[BROTLIG_NUM_DISTANCE_SYMBOLS];

        BrotligSwizzler* m_pWriter;
    };
}
