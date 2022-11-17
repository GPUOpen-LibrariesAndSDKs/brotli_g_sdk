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
#include "brotli/c/common/context.h" 
#include "brotli/c/enc/brotli_bit_stream.h"
}

#include "common/BrotligCommon.h"

#include "BrotligHistogram.h"
#include "BrotligBlockSplit.h"
#include "BrotligEncoderState.h"
#include "DataStream.h"

namespace BrotliG
{
    static const size_t kMaxLiteralHistograms = 1;
    static const size_t kMaxCommandHistograms = 1;
    static const size_t kMaxNumberOfHistograms = 1;
    static const double kLiteralBlockSwitchCost = 28.1;
    static const double kCommandBlockSwitchCost = 13.5;
    static const double kDistanceBlockSwitchCost = 14.6;
    static const size_t kLiteralStrideLength = 70;
    static const size_t kCommandStrideLength = 40;
    static const size_t kDistanceStrideLength = 40;
    static const size_t kSymbolsPerLiteralHistogram = 544;
    static const size_t kSymbolsPerCommandHistogram = 530;
    static const size_t kSymbolsPerDistanceHistogram = 544;
    static const size_t kMinLengthForBlockSplitting = 128;
    static const size_t kIterMulForRefining = 2;
    static const size_t kMinItersForRefining = 100;

#define HISTOGRAMS_PER_BATCH 64
#define CLUSTERS_PER_BATCH 16
#define SYMBOL_BITS 9

    typedef struct BrotligMetaBlockSplit
    {
        BrotligBlockSplit<Literal, uint8_t> literal_split;
        BrotligBlockSplit<Insert_and_copy, uint16_t> command_split;
        BrotligBlockSplit<Distance, uint16_t> distance_split;

        std::vector<uint32_t> literal_context_map;
        size_t literal_context_map_size;

        std::vector<uint32_t> distance_context_map;
        size_t distance_context_map_size;

        std::vector<BrotligHistogram<Literal, uint8_t>> literal_histograms;
        size_t literal_histograms_size;

        std::vector<BrotligHistogram<Insert_and_copy, uint16_t>> command_histograms;
        size_t command_histograms_size;

        std::vector<BrotligHistogram<Distance, uint16_t>> distance_histograms;
        size_t distance_histograms_size;

        BrotligMetaBlockSplit()
        {
            literal_context_map_size = 0;
            distance_context_map_size = 0;
            literal_histograms_size = 0;
            command_histograms_size = 0;
            distance_histograms_size = 0;
        }

        void EncodeAndStoreContextMap(BrotligSwizzler* sw, BrotligElementTypes type)
        {
            size_t num_histograms;
            std::vector<uint32_t> contextmap;
            size_t histogram_size;

            if (type == BrotligElementTypes::Literal)
            {
                num_histograms = literal_histograms_size;
                contextmap = literal_context_map;
                histogram_size = BROTLI_NUM_LITERAL_SYMBOLS;
            }
            else if (type == BrotligElementTypes::Distance)
            {
                num_histograms = distance_histograms_size;
                contextmap = distance_context_map;
                histogram_size = 1024;
            }
            else
            {
                return;
            }

            if (num_histograms == 1)
                return;

            /*Removing RLE for now, pending shader debugging*/
            std::vector<uint32_t> rle_symbols = MoveToFrontTransform(contextmap);
            size_t num_rle_symbols = 0;
            uint32_t max_run_length_prefix = 6;
            static const uint32_t kSymbolMask = (1u << SYMBOL_BITS) - 1u;
            RunLengthCodeZeros(rle_symbols, num_rle_symbols, max_run_length_prefix);
            rle_symbols.resize(num_rle_symbols);

            std::vector<uint32_t> rle_hist(BROTLI_MAX_CONTEXT_MAP_SYMBOLS, 0);
            for (size_t i = 0; i < num_rle_symbols; ++i)
            {
                ++rle_hist.at(rle_symbols[i] & kSymbolMask);
            }

            {
                bool use_rle = (max_run_length_prefix > 0);
                sw->Append(1, (uint64_t)use_rle);
                if (use_rle)
                {
                    sw->Append(4, max_run_length_prefix - 1);
                }
                sw->Append(1, 1); // Move to front transform
            }

            BrotligHuffmanTree* tree = new BrotligHuffmanTree;
            tree->Build(rle_hist);
            tree->Store(sw, literal_histograms_size + max_run_length_prefix);

            for (size_t i = 0; i < num_rle_symbols; ++i)
            {
                const uint32_t rle_symbol = rle_symbols[i] & kSymbolMask;
                const uint32_t extra_bits_val = rle_symbols[i] >> SYMBOL_BITS;
                sw->Append(static_cast<uint32_t>(tree->Bitsize(rle_symbol)), static_cast<uint32_t>(tree->Revcode(rle_symbol)));
                if (rle_symbol > 0 && rle_symbol <= max_run_length_prefix)
                {
                    sw->Append(extra_bits_val, rle_symbol);
                }
                sw->BSSwitch();
            }

            delete tree;
        }
    } BrotligMetaBlockSplit;

    typedef struct BrotligEncodeContextMapArena
    {
        uint32_t histogram[BROTLI_MAX_CONTEXT_MAP_SYMBOLS];
        uint8_t depths[BROTLI_MAX_CONTEXT_MAP_SYMBOLS];
        uint16_t bits[BROTLI_MAX_CONTEXT_MAP_SYMBOLS];
    }BrotligEncodeContextMapArena;

    typedef struct BrotligStoreMetaBlockArena
    {
        BrotligBlockEncoder<Literal, uint8_t>* literal_enc;
        BrotligBlockEncoder<Insert_and_copy, uint16_t>* command_enc;
        BrotligBlockEncoder<Distance, uint16_t>* distance_enc;
        BrotligEncodeContextMapArena context_map_arena;

        BrotligStoreMetaBlockArena()
        {
            literal_enc = new BrotligBlockEncoder<Literal, uint8_t>;
            command_enc = new BrotligBlockEncoder<Insert_and_copy, uint16_t>;
            distance_enc = new BrotligBlockEncoder<Distance, uint16_t>;
        }

        ~BrotligStoreMetaBlockArena()
        {
            delete literal_enc;
            delete command_enc;
            delete distance_enc;
        }

        void Initialize(BrotligMetaBlockSplit* other, uint32_t num_effective_distance_symbols)
        {
            literal_enc->Initialize(other->literal_split, BROTLI_NUM_LITERAL_SYMBOLS);
            command_enc->Initialize(other->command_split, BROTLI_NUM_COMMAND_SYMBOLS);
            distance_enc->Initialize(other->distance_split, num_effective_distance_symbols);
        }

        void Initialize(BrotligStoreMetaBlockArena* other)
        {
            literal_enc->Initialize(other->literal_enc);
            command_enc->Initialize(other->command_enc);
            distance_enc->Initialize(other->distance_enc);
        }

        void Reset()
        {
            literal_enc->Reset();
            command_enc->Reset();
            distance_enc->Reset();
        }
    }BrotligBSBlockArena;

    class BrotligMetaBlock
    {
    public:
        BrotligMetaBlock(
            BrotligEncoderState* state,
            const uint8_t* input,
            size_t input_size,
            ContextType literal_context_mode,
            uint8_t** output,
            size_t* output_size
        );

        ~BrotligMetaBlock();

        void Build();

        bool Store();
    private:
        BrotligEncoderState* m_state;
        const uint8_t* m_input;
        size_t m_inputSize;
        ContextType m_literal_context_mode;

        BrotligMetaBlockSplit* m_mb;

        uint8_t** m_output;
        size_t* m_outputSize;

        void InitDistanceParams(
            BrotligDistanceParams* dist_params,
            uint32_t npostfix,
            uint32_t ndirect);

        bool ComputeDistanceCost(
            const BrotligDistanceParams* orig_params,
            const BrotligDistanceParams* new_params,
            double* cost,
            BrotligHistogram<Distance, uint16_t>* tmp);

        uint32_t RestoreDistanceCode(
            const BrotligCommand* self,
            const BrotligDistanceParams* dist);

        void RecomputeDistancePrefixes(
            const BrotligDistanceParams* orig_params
        );

        void SplitBlock();

        void BuildHistogramsWithContext(
            const std::vector<ContextType> context_modes,
            std::vector<BrotligHistogram<Literal, uint8_t>>& literal_histograms,
            std::vector<BrotligHistogram<Insert_and_copy, uint16_t>>& command_histograms,
            std::vector<BrotligHistogram<Distance, uint16_t>>& distance_histograms
        );

        void OptimizeHistograms();

        void StoreCompressedMetaBlockHeader(BrotligSwizzler* sw);
        void EncodeContextMap(BrotligStoreMetaBlockArena& arena);

        void BuildBSBlockArena(
            BrotligStoreMetaBlockArena* self,
            BrotligStoreMetaBlockArena* other,
            const std::vector<BrotligCommand*>& cmd_stream
        );

        bool StoreNoLitDist();
        bool StoreWithLitDist();
    };
}