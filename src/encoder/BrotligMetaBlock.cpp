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
#include <algorithm>
#include <numeric>

extern "C" {
#include "brotli/c/enc/prefix.h"
#include "brotli/c/enc/entropy_encode.h"
}

#include "common/BrotligSwizzler.h"

#include "BrotligMetaBlock.h"

using namespace BrotliG;

BrotligMetaBlock::BrotligMetaBlock(
    BrotligEncoderState* state,
    const uint8_t* input,
    size_t input_size,
    ContextType literal_context_mode,
    uint8_t** output,
    size_t* output_size)
{
    m_state = state;
    m_input = input;
    m_inputSize = input_size;
    m_literal_context_mode = literal_context_mode;
    m_output = output;
    m_outputSize = output_size;
    m_mb = nullptr;
}

BrotligMetaBlock::~BrotligMetaBlock()
{
    delete m_mb;

    m_output = nullptr;
    m_input = nullptr;
    m_state = nullptr;
}

void BrotligMetaBlock::Build()
{
    m_mb = new BrotligMetaBlockSplit;
    std::vector<ContextType> literal_context_modes;
    size_t literal_context_multiplier = 1;
    uint32_t npostfix = 0;
    uint32_t ndirect_msb = 0;
    bool check_orig = true;
    double best_dist_cost = 1e99;

    BrotligDistanceParams orig_params = m_state->params->dist_params;
    BrotligDistanceParams new_params = m_state->params->dist_params;

    BrotligHistogram<Distance, uint16_t>* temp = new BrotligHistogram<Distance, uint16_t>;

    for (npostfix = 0; npostfix <= BROTLI_MAX_NPOSTFIX; npostfix++) {
        for (; ndirect_msb < 16; ndirect_msb++) {
            uint32_t ndirect = ndirect_msb << npostfix;
            bool skip = false;
            double dist_cost = 0;
            InitDistanceParams(
                &new_params,
                npostfix,
                ndirect);

            if (npostfix == orig_params.distance_postfix_bits &&
                ndirect == orig_params.num_direct_distance_codes) {
                check_orig = BROTLI_FALSE;
            }
            skip = !ComputeDistanceCost(
                &orig_params,
                &new_params,
                &dist_cost,
                temp);
            if (skip || (dist_cost > best_dist_cost)) {
                break;
            }
            best_dist_cost = dist_cost;
            m_state->params->dist_params = new_params;
        }
        if (ndirect_msb > 0) ndirect_msb--;
        ndirect_msb /= 2;
    }

    if (check_orig)
    {
        double dist_cost = 0;
        ComputeDistanceCost(
            &orig_params,
            &orig_params,
            &dist_cost,
            temp);

        if (dist_cost < best_dist_cost)
        {
            m_state->params->dist_params = orig_params;
        }
    }

    delete temp;

    RecomputeDistancePrefixes(
        &orig_params
    );

    SplitBlock();

    if (!m_state->params->disable_literal_context_modeling)
    {
        literal_context_multiplier = 1 << BROTLI_LITERAL_CONTEXT_BITS;
        for (size_t index = 0; index < m_mb->literal_split.num_types; ++index)
        {
            literal_context_modes.push_back(m_literal_context_mode);
        }
    }

    size_t literal_histogram_size = m_mb->literal_split.num_types * literal_context_multiplier;
    std::vector<BrotligHistogram<Literal, uint8_t>> literal_histograms(literal_histogram_size);
    BrotligHistogram<Literal, uint8_t>::ClearHistograms(literal_histograms);

    size_t distance_histogram_size = m_mb->distance_split.num_types << BROTLI_DISTANCE_CONTEXT_BITS;
    std::vector<BrotligHistogram<Distance, uint16_t>> distance_histograms(distance_histogram_size);
    BrotligHistogram<Distance, uint16_t>::ClearHistograms(distance_histograms);

    assert(m_mb->command_histograms.size() == 0);
    m_mb->command_histograms_size = m_mb->command_split.num_types;
    m_mb->command_histograms.resize(m_mb->command_histograms_size);
    BrotligHistogram<Insert_and_copy, uint16_t>::ClearHistograms(m_mb->command_histograms);

    BuildHistogramsWithContext(
        literal_context_modes,
        literal_histograms,
        m_mb->command_histograms,
        distance_histograms
    );

    assert(m_mb->literal_context_map.size() == 0);
    m_mb->literal_context_map_size = m_mb->literal_split.num_types << BROTLI_LITERAL_CONTEXT_BITS;
    m_mb->literal_context_map.resize(m_mb->literal_context_map_size);

    assert(m_mb->literal_histograms.size() == 0);
    m_mb->literal_histograms_size = m_mb->literal_context_map_size;
    m_mb->literal_histograms.resize(m_mb->literal_histograms_size);

    BrotligHistogram<Literal, uint8_t>::ClusterHistograms(
        literal_histograms,
        kMaxLiteralHistograms,
        m_mb->literal_histograms,
        m_mb->literal_context_map
    );

    literal_histograms.clear();

    m_mb->literal_histograms_size = m_mb->literal_histograms.size();
    if (m_state->params->disable_literal_context_modeling)
        m_mb->literal_context_map_size = 0;

    assert(m_mb->distance_context_map.size() == 0);
    m_mb->distance_context_map_size = m_mb->distance_split.num_types << BROTLI_DISTANCE_CONTEXT_BITS;
    m_mb->distance_context_map.resize(m_mb->distance_context_map_size);

    assert(m_mb->distance_histograms.size() == 0);
    m_mb->distance_histograms_size = m_mb->distance_context_map_size;
    m_mb->distance_histograms.resize(m_mb->distance_histograms_size);

    BrotligHistogram<Distance, uint16_t>::ClusterHistograms(
        distance_histograms,
        kMaxNumberOfHistograms,
        m_mb->distance_histograms,
        m_mb->distance_context_map
    );

    distance_histograms.clear();

    m_mb->distance_histograms_size = m_mb->distance_histograms.size();

    if (m_state->params->quality >= MIN_QUALITY_FOR_OPTIMIZE_HISTOGRAMS)
    {
        OptimizeHistograms();
    }

    assert(m_mb->literal_split.num_types == 1);
    assert(m_mb->command_split.num_types == 1);
    assert(m_mb->distance_split.num_types == 1);

    assert(m_mb->literal_histograms_size == 1);
    assert(m_mb->command_histograms_size == 1);
    assert(m_mb->distance_histograms_size == 1);
}

void BrotligMetaBlock::BuildBSBlockArena(
    BrotligStoreMetaBlockArena* self,
    BrotligStoreMetaBlockArena* other,
    const std::vector<BrotligCommand*>& cmd_stream)
{
    self->Initialize(other);

    for (int index = 0; index < cmd_stream.size(); ++index)
    {
        const BrotligCommand* cmd = cmd_stream.at(index);

        self->command_enc->TransferFromBE(other->command_enc);

        for (size_t j = cmd->insert_len; j != 0; --j)
        {
            self->literal_enc->TransferFromBE(other->literal_enc);
        }

        uint32_t copy_len = cmd->copy_len & 0x1FFFFFF;
        if (copy_len)
        {
            if (cmd->cmd_prefix >= 128)
            {
                self->distance_enc->TransferFromBE(other->distance_enc);
            }
        }
    }


    if ((other->literal_enc->block_lengths.size() > 0) 
        && (self->literal_enc->block_len > 0 || self->literal_enc->block_lengths.size() == 0)) 
        self->literal_enc->PushNewBlockType();

    if ((other->command_enc->block_lengths.size() != 0) 
        && (self->command_enc->block_len > 0 || self->command_enc->block_lengths.size() == 0))
        self->command_enc->PushNewBlockType();

    if ((other->distance_enc->block_lengths.size() != 0) 
        && (self->distance_enc->block_len > 0 || self->distance_enc->block_lengths.size() == 0))
        self->distance_enc->PushNewBlockType();

    for (size_t index = 0; index < self->literal_enc->block_lengths.size(); ++index)
    {
        assert(self->literal_enc->BlockLengthPrefixCode(self->literal_enc->block_lengths.at(index)) <= 65535);
        uint16_t blocklen = self->literal_enc->BlockLengthPrefixCode((uint16_t)self->literal_enc->block_lengths.at(index));
        if (other->literal_enc->length_histo.find(blocklen) == other->literal_enc->length_histo.end())
        {
            other->literal_enc->length_histo.insert(std::pair<uint16_t, uint32_t>(blocklen, 1));
        }
        else
        {
            other->literal_enc->length_histo.find(blocklen)->second += 1;
        }
    }

    for (size_t index = 0; index < self->command_enc->block_lengths.size(); ++index)
    {
        assert(self->command_enc->BlockLengthPrefixCode(self->command_enc->block_lengths.at(index)) <= 65535);
        uint16_t blocklen = self->command_enc->BlockLengthPrefixCode((uint16_t)self->command_enc->block_lengths.at(index));
        if (other->command_enc->length_histo.find(blocklen) == other->command_enc->length_histo.end())
        {
            other->command_enc->length_histo.insert(std::pair<uint16_t, uint32_t>(blocklen, 1));
        }
        else
        {
            other->command_enc->length_histo.find(blocklen)->second += 1;
        }
    }

    for (size_t index = 0; index < self->distance_enc->block_lengths.size(); ++index)
    {
        assert(self->distance_enc->BlockLengthPrefixCode(self->distance_enc->block_lengths.at(index)) <= 65535);
        uint16_t blocklen = self->distance_enc->BlockLengthPrefixCode((uint16_t)self->distance_enc->block_lengths.at(index));
        if (other->distance_enc->length_histo.find(blocklen) == other->distance_enc->length_histo.end())
        {
            other->distance_enc->length_histo.insert(std::pair<uint16_t, uint32_t>(blocklen, 1));
        }
        else
        {
            other->distance_enc->length_histo.find(blocklen)->second += 1;
        }
    }

    // reset the block encoder counters to the first block type
    self->Reset();
}

bool BrotligMetaBlock::Store()
{
#if REDISTRIBUTE_LITERALS
    return StoreWithLitDist();
#else
    return StoreNoLitDist();
#endif // ENABLE_LITERAL_DISTRIBUTION

}

bool BrotligMetaBlock::StoreNoLitDist()
{
    size_t pos = 0;
    size_t  i = 0;
    uint32_t num_distance_symbols = m_state->params->dist_params.alphabet_size_max;
    uint32_t num_effective_distance_symbols = m_state->params->dist_params.alphabet_size_limit;
    ContextLut literal_context_lut = BROTLI_CONTEXT_LUT(m_literal_context_mode);
    BrotligStoreMetaBlockArena arena;
    
    *m_outputSize = 2 * m_inputSize + 503;
    *m_output = new Byte[*m_outputSize];
    memset(*m_output, 0, 2 * m_inputSize + 503);

    m_state->bw->SetStorage(*m_output);
    m_state->bw->SetPosition(0);

    size_t page_size = m_state->params->page_size;
    BrotligSwizzler* swizzler = new BrotligSwizzler(
        m_state->params->num_bitstreams,
        page_size,
        m_state->params->swizzle_size);
    swizzler->SetOutWriter(m_state->bw, *m_outputSize);

    const BrotligDistanceParams* dist = &m_state->params->dist_params;
    assert(num_effective_distance_symbols <= BROTLI_NUM_HISTOGRAM_DISTANCE_SYMBOLS);

    arena.Initialize(m_mb, num_effective_distance_symbols);

    size_t num_bitstreams = m_state->params->num_bitstreams;
    std::vector<std::vector<BrotligCommand*>>& cmds = m_state->commands;
    std::vector<BrotligStoreMetaBlockArena*> bitstream_arenas;
    size_t bitstreamIndex = 0;

    // Separate out block context switch for different bitstreams
    for (bitstreamIndex = 0; bitstreamIndex < num_bitstreams; ++bitstreamIndex)
    {
        BrotligStoreMetaBlockArena* bsarena = new BrotligStoreMetaBlockArena;
        BuildBSBlockArena(
            bsarena,
            &arena,
            cmds.at(bitstreamIndex)
        );

        bitstream_arenas.push_back(bsarena);
    }

    // Load entropy codes
    arena.command_enc->BuildAndStoreEntropyCodes(
        m_mb->command_histograms,
        swizzler,
        BROLTIG_NUM_COMMAND_SYMBOLS_EFFECTIVE,
        false
    );

    arena.distance_enc->BuildAndStoreEntropyCodes(
        m_mb->distance_histograms,
        swizzler,
        BROTLIG_NUM_DISTANCE_SYMBOLS
    );

    arena.literal_enc->BuildAndStoreEntropyCodes(
        m_mb->literal_histograms,
        swizzler,
        BROTLI_NUM_LITERAL_SYMBOLS,
        false
    );

    // Copy the new entropy codes to bitstream arenas
    for (bitstreamIndex = 0; bitstreamIndex < num_bitstreams; ++bitstreamIndex)
    {
        BrotligStoreMetaBlockArena* bsarena = bitstream_arenas.at(bitstreamIndex);
        bsarena->literal_enc->block_split_code = arena.literal_enc->block_split_code;
        bsarena->literal_enc->block_split_code.isCopy = true;
        bsarena->literal_enc->CopyBlockTrees(arena.literal_enc->org_block_trees);

        bsarena->command_enc->block_split_code = arena.command_enc->block_split_code;
        bsarena->command_enc->block_split_code.isCopy = true;
        bsarena->command_enc->CopyBlockTrees(arena.command_enc->org_block_trees);

        bsarena->distance_enc->block_split_code = arena.distance_enc->block_split_code;
        bsarena->distance_enc->block_split_code.isCopy = true;
        bsarena->distance_enc->CopyBlockTrees(arena.distance_enc->org_block_trees);
    }

    uint8_t prev_byte = 0, prev_byte2 = 0;
    BrotligBitWriter* bs_bw = nullptr;

    size_t dst = 0;
    for (bitstreamIndex = 0; bitstreamIndex < num_bitstreams; ++bitstreamIndex)
    {
        BrotligBSBlockArena* bsarena = bitstream_arenas.at(bitstreamIndex);
        size_t listsize = cmds.at(bitstreamIndex).size();
        bs_bw = swizzler->GetWriter(bitstreamIndex);

        for (size_t index = 0; index < listsize; ++index)
        {            
            BrotligCommand* cmd = cmds.at(bitstreamIndex).at(index);

            size_t cmd_code = cmd->cmd_prefix;
            bsarena->command_enc->StoreSymbol(cmd_code, bs_bw);
            cmd->StoreExtra(bs_bw);
            assert((bs_bw->GetPosition() / 8) < page_size);

            uint32_t pos = cmd->insert_pos;
            if (m_mb->literal_context_map_size == 0)
            {
                for (size_t j = cmd->insert_len; j != 0; --j)
                {
                    bsarena->literal_enc->StoreSymbol(m_input[pos], bs_bw);
                    assert((bs_bw->GetPosition() / 8) < page_size);
                    ++pos;
                }
            }
            else
            {
                for (size_t j = cmd->insert_len; j != 0; --j)
                {
                    size_t context = BROTLI_CONTEXT(prev_byte, prev_byte2, literal_context_lut);
                    uint8_t literal = m_input[pos];

                    bsarena->literal_enc->StoreSymbolWithContext(
                        literal,
                        context,
                        m_mb->literal_context_map,
                        bs_bw,
                        BROTLI_LITERAL_CONTEXT_BITS
                    );

                    assert((bs_bw->GetPosition() / 8) < page_size);

                    prev_byte2 = prev_byte;
                    prev_byte = literal;
                    ++pos;
                }
            }

            uint32_t copy_len = cmd->CopyLen();
            pos += copy_len;
            if (copy_len)
            {
                prev_byte2 = m_input[pos - 2];
                prev_byte = m_input[pos - 1];

                if (cmd->cmd_prefix >= 128 && cmd->cmd_prefix < BROTLI_NUM_COMMAND_SYMBOLS)
                {
                    size_t dist_code = cmd->Distance();
                    uint32_t distnumextra = cmd->dist_prefix >> 10;
                    uint64_t distextra = cmd->dist_extra;
                    if (m_mb->distance_context_map_size == 0)
                    {
                        bsarena->distance_enc->StoreSymbol(dist_code, bs_bw);
                        assert((bs_bw->GetPosition() / 8) < page_size);
                    }
                    else
                    {
                        size_t context = cmd->DistanceContext();
                        bsarena->distance_enc->StoreSymbolWithContext(
                            dist_code,
                            context,
                            m_mb->distance_context_map,
                            bs_bw,
                            BROTLI_DISTANCE_CONTEXT_BITS
                        );

                        assert((bs_bw->GetPosition() / 8) < page_size);
                    }

                    bs_bw->Write(distnumextra, distextra);

                    assert((bs_bw->GetPosition() / 8) < page_size);
                }
            }
        }
    }

    bs_bw = nullptr;

    StoreCompressedMetaBlockHeader(swizzler);

#if USE_COMPACT_SERIALIZTION
    bool isSerialized = swizzler->SerializeCompact(page_size, false, true);
#else
    bool isSerialized = swizzler->Serialize(true, true);
#endif

    for (bitstreamIndex = 0; bitstreamIndex < num_bitstreams; ++bitstreamIndex)
    {
        delete bitstream_arenas.at(bitstreamIndex);
        bitstream_arenas.at(bitstreamIndex) = nullptr;
    }
    bitstream_arenas.clear();

    delete swizzler;
    return isSerialized;
}

bool BrotligMetaBlock::StoreWithLitDist()
{
    size_t pos = 0;
    size_t  i = 0;
    uint32_t num_distance_symbols = m_state->params->dist_params.alphabet_size_max;
    uint32_t num_effective_distance_symbols = m_state->params->dist_params.alphabet_size_limit;
    ContextLut literal_context_lut = BROTLI_CONTEXT_LUT(m_literal_context_mode);
    BrotligStoreMetaBlockArena arena;

    *m_outputSize = 2 * m_inputSize + 503;
    *m_output = new Byte[*m_outputSize];
    memset(*m_output, 0, 2 * m_inputSize + 503);

    m_state->bw->SetStorage(*m_output);
    m_state->bw->SetPosition(0);

    size_t page_size = m_state->params->page_size;
    BrotligSwizzler* swizzler = new BrotligSwizzler(
        m_state->params->num_bitstreams,
        page_size,
        m_state->params->swizzle_size);
    swizzler->SetOutWriter(m_state->bw, *m_outputSize);

    const BrotligDistanceParams* dist = &m_state->params->dist_params;
    assert(num_effective_distance_symbols <= BROTLI_NUM_HISTOGRAM_DISTANCE_SYMBOLS);

    arena.Initialize(m_mb, num_effective_distance_symbols);

    size_t num_bitstreams = m_state->params->num_bitstreams;
    std::vector<std::vector<BrotligCommand*>>& cmds = m_state->commands;
    std::vector<BrotligStoreMetaBlockArena*> bitstream_arenas;
    size_t bitstreamIndex = 0;

    // Separate out block context switch for different bitstreams
    for (bitstreamIndex = 0; bitstreamIndex < num_bitstreams; ++bitstreamIndex)
    {
        BrotligStoreMetaBlockArena* bsarena = new BrotligStoreMetaBlockArena;
        BuildBSBlockArena(
            bsarena,
            &arena,
            cmds.at(bitstreamIndex)
        );

        bitstream_arenas.push_back(bsarena);
    }

    // Load entropy codes
    arena.command_enc->BuildAndStoreEntropyCodes(
        m_mb->command_histograms,
        swizzler,
        BROLTIG_NUM_COMMAND_SYMBOLS_EFFECTIVE,
        false
    );

    arena.distance_enc->BuildAndStoreEntropyCodes(
        m_mb->distance_histograms,
        swizzler,
        BROTLIG_NUM_DISTANCE_SYMBOLS
    );

    arena.literal_enc->BuildAndStoreEntropyCodes(
        m_mb->literal_histograms,
        swizzler,
        BROTLI_NUM_LITERAL_SYMBOLS
    );

    // Copy the new entropy codes to bitstream arenas
    for (bitstreamIndex = 0; bitstreamIndex < num_bitstreams; ++bitstreamIndex)
    {
        BrotligStoreMetaBlockArena* bsarena = bitstream_arenas.at(bitstreamIndex);
        bsarena->literal_enc->block_split_code = arena.literal_enc->block_split_code;
        bsarena->literal_enc->block_split_code.isCopy = true;
        bsarena->literal_enc->CopyBlockTrees(arena.literal_enc->org_block_trees);

        bsarena->command_enc->block_split_code = arena.command_enc->block_split_code;
        bsarena->command_enc->block_split_code.isCopy = true;
        bsarena->command_enc->CopyBlockTrees(arena.command_enc->org_block_trees);

        bsarena->distance_enc->block_split_code = arena.distance_enc->block_split_code;
        bsarena->distance_enc->block_split_code.isCopy = true;
        bsarena->distance_enc->CopyBlockTrees(arena.distance_enc->org_block_trees);
    }

    uint8_t prev_byte = 0, prev_byte2 = 0;
    BrotligBitWriter* bs_bw = nullptr;

    // Compute the number of rounds
    size_t NRounds = 0;
    size_t totalCmds = 0;
    for (bitstreamIndex = 0; bitstreamIndex < num_bitstreams; ++bitstreamIndex)
    {
        totalCmds += cmds.at(bitstreamIndex).size();
        if (cmds.at(bitstreamIndex).size() > NRounds)
        {
            NRounds = cmds.at(bitstreamIndex).size();
        }
    }

    // Collect all the literals
    std::vector<uint8_t> lits;
    std::vector<BrotligBlockEncoder<Literal, uint8_t>*> literal_encoders;
    std::vector<uint8_t> prev_bytes, prev_bytes_2;
    uint8_t mostFreqLiteral = 0;
    size_t highestFreq = 0;
    uint8_t mostFreqLitPrevByte1 = 0, mostFreqLitPrevByte2 = 0;
    BrotligBlockEncoder<Literal, uint8_t>* mostFreqLiteralEncoder = nullptr;
    for (size_t round = 0; round < NRounds; ++round)
    {
        for (bitstreamIndex = 0; bitstreamIndex < num_bitstreams; ++bitstreamIndex)
        {
            size_t listsize = cmds.at(bitstreamIndex).size();
            if (listsize <= round)
                continue;

            BrotligCommand* cmd = cmds.at(bitstreamIndex).at(round);
            BrotligBSBlockArena* bsarena = bitstream_arenas.at(bitstreamIndex);

            uint32_t pos = cmd->insert_pos;
            if (m_mb->literal_context_map_size == 0)
            {
                for (size_t j = cmd->insert_len; j != 0; --j)
                {
                    lits.push_back(m_input[pos]);
                    literal_encoders.push_back(bsarena->literal_enc);
                    ++pos;
                }
            }
            else
            {
                for (size_t j = cmd->insert_len; j != 0; --j)
                {
                    uint8_t literal = m_input[pos];
                    lits.push_back(literal);
                    prev_bytes.push_back(prev_byte);
                    prev_bytes_2.push_back(prev_byte2);

                    if ((m_mb->literal_histograms.at(0).data[literal] > highestFreq) ||
                        (m_mb->literal_histograms.at(0).data[literal] == highestFreq
                            && literal < mostFreqLiteral))
                    {
                        mostFreqLiteral = literal;
                        highestFreq = m_mb->literal_histograms.at(0).data[literal];
                        mostFreqLiteralEncoder = bsarena->literal_enc;
                        mostFreqLitPrevByte1 = prev_byte;
                        mostFreqLitPrevByte2 = prev_byte2;
                    }

                    prev_byte2 = prev_byte;
                    prev_byte = literal;

                    literal_encoders.push_back(bsarena->literal_enc);
                    ++pos;
                }
            }

            // Encode and store copy distance in its own lane
            uint32_t copy_len = cmd->CopyLen();
            pos += copy_len;
            if (copy_len)
            {
                prev_byte2 = m_input[pos - 2];
                prev_byte = m_input[pos - 1];
            }
        }
    }

    assert(lits.size() == literal_encoders.size());

    // Swizzling
    // Encode the commands round by round
    // A round consists of <= num_bitream commands, one from each bitstream/lane's command buffer
    // At each round
    //      For each lane
    //          insert-and-copy lengths and copy distances are encoded and stored in their own lanes
    //          literals are collected and stored in a temp buffer
    //      Literals from all lanes are encoded and redstributed across all the lanes
    size_t prev_tail = 0;
    size_t cur_lit_index = 0;
    for (size_t round = 0; round < NRounds; ++round)
    {
        size_t litcount = 0;
        for (bitstreamIndex = 0; bitstreamIndex < num_bitstreams; ++bitstreamIndex)
        {
            size_t listsize = cmds.at(bitstreamIndex).size();
            if (listsize <= round)
                continue;

            BrotligBSBlockArena* bsarena = bitstream_arenas.at(bitstreamIndex);
            bs_bw = swizzler->GetWriter(bitstreamIndex);

            BrotligCommand* cmd = cmds.at(bitstreamIndex).at(round);

            // Encode and store ICP in its own lane
            size_t cmd_code = cmd->cmd_prefix;
            bsarena->command_enc->StoreSymbol(cmd_code, bs_bw);
            cmd->StoreExtra(bs_bw);
            assert((bs_bw->GetPosition() / 8) < page_size);

            // Compute litcount for each round
            uint32_t pos = cmd->insert_pos;
            litcount += cmd->insert_len;
            pos += cmd->insert_len;

            // Encode and store copy distance in its own lane
            uint32_t copy_len = cmd->CopyLen();
            pos += copy_len;
            if (copy_len)
            {
                prev_byte2 = m_input[pos - 2];
                prev_byte = m_input[pos - 1];

                if (cmd->cmd_prefix >= 128 && cmd->cmd_prefix < BROTLI_NUM_COMMAND_SYMBOLS)
                {
                    size_t dist_code = cmd->Distance();
                    uint32_t distnumextra = cmd->dist_prefix >> 10;
                    uint64_t distextra = cmd->dist_extra;
                    if (m_mb->distance_context_map_size == 0)
                    {
                        bsarena->distance_enc->StoreSymbol(dist_code, bs_bw);
                        assert((bs_bw->GetPosition() / 8) < page_size);
                    }
                    else
                    {
                        size_t context = cmd->DistanceContext();
                        bsarena->distance_enc->StoreSymbolWithContext(
                            dist_code,
                            context,
                            m_mb->distance_context_map,
                            bs_bw,
                            BROTLI_DISTANCE_CONTEXT_BITS
                        );

                        assert((bs_bw->GetPosition() / 8) < page_size);
                    }

                    bs_bw->Write(distnumextra, distextra);

                    assert((bs_bw->GetPosition() / 8) < page_size);
                }
            }
        }

        size_t effNum_bitstreams = (totalCmds >= num_bitstreams) ? num_bitstreams : totalCmds;
        size_t aclitcount = (litcount > prev_tail) ? litcount - prev_tail : 0;
        size_t mult = (aclitcount + effNum_bitstreams - 1) / effNum_bitstreams;
        size_t rlitcount = effNum_bitstreams * mult;
        size_t totalcnt = rlitcount + prev_tail;
        prev_tail = totalcnt - litcount;

        // Encode collected literals and redistribute them to all the lanes
        size_t curwriteindex = 0;
        size_t litoff = 0;
        for (litoff = 0; litoff < rlitcount/* && cur_lit_index + litoff < lits.size()*/; ++litoff)
        {
            uint8_t literal = 0;
            BrotligBlockEncoder<Literal, uint8_t>* lit_enc = nullptr;
            if (cur_lit_index + litoff >= lits.size())
            {
                if (round < NRounds - 1 || m_state->isLast)
                {
                    literal = mostFreqLiteral;
                    lit_enc = mostFreqLiteralEncoder;
                    ++lit_enc->block_lengths[lit_enc->block_ix];
                    ++lit_enc->block_len;

                    prev_byte = mostFreqLitPrevByte1;
                    prev_byte2 = mostFreqLitPrevByte2;
                }
                else
                {
                    break;
                }
            }
            else
            {
                literal = lits.at(cur_lit_index + litoff);
                lit_enc = literal_encoders.at(cur_lit_index + litoff);

                prev_byte = prev_bytes.at(cur_lit_index + litoff);
                prev_byte2 = prev_bytes_2.at(cur_lit_index + litoff);
            }

            bs_bw = swizzler->GetWriter(curwriteindex);
            if (m_mb->literal_context_map_size == 0)
            {
                lit_enc->StoreSymbol(literal, bs_bw);
                assert((bs_bw->GetPosition() / 8) < page_size);

            }
            else
            {
                size_t context = BROTLI_CONTEXT(prev_byte, prev_byte2, literal_context_lut);

                lit_enc->StoreSymbolWithContext(
                    literal,
                    context,
                    m_mb->literal_context_map,
                    bs_bw,
                    BROTLI_LITERAL_CONTEXT_BITS
                );

                assert((bs_bw->GetPosition() / 8) < page_size);
            }

            curwriteindex++;
            if (curwriteindex == 32)
                curwriteindex = 0;
        }

        cur_lit_index += litoff;
    }

    lits.clear();
    literal_encoders.clear();
    prev_bytes.clear();
    prev_bytes_2.clear();

    bs_bw = nullptr;

    StoreCompressedMetaBlockHeader(swizzler);

#if USE_COMPACT_SERIALIZTION
    bool isSerialized = swizzler->SerializeCompact(page_size, false, true);
#else
    bool isSerialized = swizzler->Serialize(true, true);
#endif

    for (bitstreamIndex = 0; bitstreamIndex < num_bitstreams; ++bitstreamIndex)
    {
        delete bitstream_arenas.at(bitstreamIndex);
        bitstream_arenas.at(bitstreamIndex) = nullptr;
    }
    bitstream_arenas.clear();

    delete swizzler;
    return isSerialized;
}

void BrotligMetaBlock::InitDistanceParams(
    BrotligDistanceParams* dist_params,
    uint32_t npostfix,
    uint32_t ndirect)
{
    uint32_t alphabet_size_max;
    uint32_t alphabet_size_limit;
    uint32_t max_distance;

    dist_params->distance_postfix_bits = npostfix;
    dist_params->num_direct_distance_codes = ndirect;

    alphabet_size_max = BROTLI_DISTANCE_ALPHABET_SIZE(
        npostfix, ndirect, BROTLI_MAX_DISTANCE_BITS);
    alphabet_size_limit = alphabet_size_max;
    max_distance = ndirect + (1U << (BROTLI_MAX_DISTANCE_BITS + npostfix + 2)) -
        (1U << (npostfix + 2));

    if (m_state->params->large_window) {
        BrotliDistanceCodeLimit limit = BrotliCalculateDistanceCodeLimit(
            BROTLI_MAX_ALLOWED_DISTANCE, npostfix, ndirect);
        alphabet_size_max = BROTLI_DISTANCE_ALPHABET_SIZE(
            npostfix, ndirect, BROTLI_LARGE_MAX_DISTANCE_BITS);
        alphabet_size_limit = limit.max_alphabet_size;
        max_distance = limit.max_distance;
    }

    dist_params->alphabet_size_max = alphabet_size_max;
    dist_params->alphabet_size_limit = alphabet_size_limit;
    dist_params->max_distance = max_distance;
}

bool BrotligMetaBlock::ComputeDistanceCost(
    const BrotligDistanceParams* orig_params,
    const BrotligDistanceParams* new_params,
    double* cost,
    BrotligHistogram<Distance, uint16_t>* tmp)
{
    size_t i, j;
    bool equal_params = false;
    uint16_t dist_prefix;
    uint32_t dist_extra;
    double extra_bits = 0.0;
    tmp->Clear();

    if (orig_params->distance_postfix_bits == new_params->distance_postfix_bits &&
        orig_params->num_direct_distance_codes ==
        new_params->num_direct_distance_codes) {
        equal_params = true;
    }

    std::vector<std::vector<BrotligCommand*>>& cmds = m_state->commands;
    for (i = 0; i < cmds.size(); i++) {
        for (j = 0; j < cmds.at(i).size(); ++j)
        {
            BrotligCommand* cmd = cmds.at(i).at(j);
            uint32_t copy_len = cmd->copy_len & 0x1FFFFFF;
            if (copy_len && cmd->cmd_prefix >= 128) {
                if (equal_params) {
                    dist_prefix = cmd->dist_prefix;
                }
                else {
                    uint32_t distance = RestoreDistanceCode(cmd, orig_params);
                    if (distance > new_params->max_distance) {
                        return false;
                    }

                    PrefixEncodeCopyDistance(
                        distance,
                        new_params->num_direct_distance_codes,
                        new_params->distance_postfix_bits,
                        &dist_prefix,
                        &dist_extra
                    );
                }
                tmp->Add(dist_prefix & 0x3FF);
                extra_bits += dist_prefix >> 10;
            }
        }
    }

    *cost = tmp->PopulationCost();

    return true;
}

uint32_t BrotligMetaBlock::RestoreDistanceCode(
    const BrotligCommand* self, 
    const BrotligDistanceParams* dist) 
{
    if ((self->dist_prefix & 0x3FFu) <
        BROTLI_NUM_DISTANCE_SHORT_CODES + dist->num_direct_distance_codes) {
        return self->dist_prefix & 0x3FFu;
    }
    else {
        uint32_t dcode = self->dist_prefix & 0x3FFu;
        uint32_t nbits = self->dist_prefix >> 10;
        uint32_t extra = self->dist_extra;
        uint32_t postfix_mask = (1U << dist->distance_postfix_bits) - 1U;
        uint32_t hcode = (dcode - dist->num_direct_distance_codes -
            BROTLI_NUM_DISTANCE_SHORT_CODES) >>
            dist->distance_postfix_bits;
        uint32_t lcode = (dcode - dist->num_direct_distance_codes -
            BROTLI_NUM_DISTANCE_SHORT_CODES) & postfix_mask;
        uint32_t offset = ((2U + (hcode & 1U)) << nbits) - 4U;
        return ((offset + extra) << dist->distance_postfix_bits) + lcode +
            dist->num_direct_distance_codes + BROTLI_NUM_DISTANCE_SHORT_CODES;
    }
}

void BrotligMetaBlock::RecomputeDistancePrefixes(
    const BrotligDistanceParams* orig_params
)
{
    BrotligDistanceParams& new_params = m_state->params->dist_params;
    size_t i, j;

    if (orig_params->distance_postfix_bits == new_params.distance_postfix_bits &&
        orig_params->num_direct_distance_codes ==
        new_params.num_direct_distance_codes) {
        return;
    }

    std::vector<std::vector<BrotligCommand*>>& cmds = m_state->commands;
    for (i = 0; i < cmds.size(); i++)
    {
        for (j = 0; j < cmds.at(i).size(); ++j)
        {
            BrotligCommand* cmd = cmds.at(i).at(j);
            uint32_t copy_len = cmd->copy_len & 0x1FFFFFF;
            if (copy_len && cmd->cmd_prefix >= 128) {
                PrefixEncodeCopyDistance(RestoreDistanceCode(cmd, orig_params),
                    new_params.num_direct_distance_codes,
                    new_params.distance_postfix_bits,
                    &cmd->dist_prefix,
                    &cmd->dist_extra);
            }
        }
    }
}

void BrotligMetaBlock::SplitBlock()
{
    std::vector<std::vector<BrotligCommand*>>& cmds = m_state->commands;

    // Compute literal blocks
    {
        std::vector<uint8_t> literals;
        size_t pos = 0;
        size_t from_pos = 0;

        for (size_t i = 0; i < cmds.size(); ++i)
        {
            for (size_t j = 0; j < cmds.at(i).size(); ++j)
            {
                size_t insert_len = cmds.at(i).at(j)->insert_len;
                if (insert_len > 0)
                {
                    from_pos = cmds.at(i).at(j)->insert_pos;
                    for (int k = 0; k < insert_len; ++k)
                    {
                        literals.push_back(m_input[from_pos + k]);
                    }
                }
            }
        }

        m_mb->literal_split.Build(
            literals,
            kSymbolsPerLiteralHistogram,
            kMaxLiteralHistograms,
            kLiteralStrideLength,
            kLiteralBlockSwitchCost,
            m_state->params
            );

        literals.clear();
    }
    
    // Compute insert-and-copy length blocks
    {
        std::vector<uint16_t> insert_and_copy_codes;
        for (size_t i = 0; i < cmds.size(); ++i)
        {
            for (size_t j = 0; j < cmds.at(i).size(); ++j)
            {
                insert_and_copy_codes.push_back(m_state->commands.at(i).at(j)->cmd_prefix);
            }
        }

        m_mb->command_split.Build(
            insert_and_copy_codes,
            kSymbolsPerCommandHistogram,
            kMaxCommandHistograms,
            kCommandStrideLength,
            kCommandBlockSwitchCost,
            m_state->params
            );

        insert_and_copy_codes.clear();
    }

    // Compute distance blocks
    {
        std::vector<uint16_t> distance_prefixes;
        for (size_t i = 0; i < cmds.size(); ++i)
        {
            for (size_t j = 0; j < cmds.at(i).size(); ++j)
            {
                BrotligCommand* cmd = m_state->commands.at(i).at(j);
                uint32_t copy_len = cmd->CopyLen();
                if (copy_len && cmd->cmd_prefix >= 128 && cmd->cmd_prefix < BROTLI_NUM_COMMAND_SYMBOLS)
                {
                    distance_prefixes.push_back(cmd->Distance());
                }
            }
        }

        m_mb->distance_split.Build(
            distance_prefixes,
            kSymbolsPerDistanceHistogram,
            kMaxCommandHistograms,          
            kDistanceStrideLength,
            kDistanceBlockSwitchCost,
            m_state->params
            );

        distance_prefixes.clear();
    }
}

void BrotligMetaBlock::BuildHistogramsWithContext(
    const std::vector<ContextType> context_modes,
    std::vector<BrotligHistogram<Literal, uint8_t>>& literal_histograms,
    std::vector<BrotligHistogram<Insert_and_copy, uint16_t>>& command_histograms,
    std::vector<BrotligHistogram<Distance, uint16_t>>& distance_histograms
)
{
    size_t pos = 0;

    BrotligBlockSplitIterator<Literal, uint8_t> literal_it(&m_mb->literal_split);
    BrotligBlockSplitIterator<Insert_and_copy, uint16_t> command_it(&m_mb->command_split);
    BrotligBlockSplitIterator<Distance, uint16_t> distance_it(&m_mb->distance_split);

    uint8_t prev_byte = 0, prev_byte2 = 0;
    size_t num_bitstreams = m_state->params->num_bitstreams;
    std::vector<std::vector<BrotligCommand*>>& cmds = m_state->commands;
    for (size_t bsIndex = 0; bsIndex < num_bitstreams; ++bsIndex)
    {
        size_t listsize = cmds.at(bsIndex).size();
        for (int lIndex = 0; lIndex < listsize; ++lIndex)
        {
            BrotligCommand* cmd = cmds.at(bsIndex).at(lIndex);
            size_t j = 0;
            command_histograms.at(command_it.type).Add(cmd->cmd_prefix);
            ++command_it;

            pos = cmd->insert_pos;
            for (j = cmd->insert_len; j != 0; --j)
            {
                size_t hist_ix = literal_it.type;
                if (context_modes.size() > 0)
                {
                    ContextLut lut = BROTLI_CONTEXT_LUT(context_modes[literal_it.type]);
                    size_t entropyix = (literal_it.type << BROTLI_LITERAL_CONTEXT_BITS);
                    size_t context = BROTLI_CONTEXT(prev_byte, prev_byte2, lut);
                    hist_ix = entropyix + context;
                }

                literal_histograms.at(hist_ix).Add(m_input[pos]);
                prev_byte2 = prev_byte;
                prev_byte = m_input[pos];
                ++pos;
                ++literal_it;
            }

            uint32_t copy_len = cmd->CopyLen();
            pos += copy_len;
            if (copy_len)
            {
                prev_byte2 = m_input[pos - 2];
                prev_byte = m_input[pos - 1];
                if (cmd->cmd_prefix >= 128 && cmd->cmd_prefix < BROTLI_NUM_COMMAND_SYMBOLS)
                {
                    
                    size_t context = (distance_it.type << BROTLI_DISTANCE_CONTEXT_BITS)
                        + cmd->DistanceContext();

                    distance_histograms.at(context).Add(
                        cmd->Distance()
                    );

                    ++distance_it;
                }
            }
        }
    }
}

void BrotligMetaBlock::OptimizeHistograms()
{
    uint32_t num_distance_codes = m_state->params->dist_params.alphabet_size_limit;
    uint8_t good_for_rle[BROTLI_NUM_COMMAND_SYMBOLS];
    size_t i = 0;

    for (i = 0; i < m_mb->literal_histograms_size; ++i)
    {
        BrotliOptimizeHuffmanCountsForRle(
            256,
            m_mb->literal_histograms[i].data.data(),
            good_for_rle
        );
    }

    for (i = 0; i < m_mb->command_histograms_size; ++i)
    {
        BrotliOptimizeHuffmanCountsForRle(
            BROTLI_NUM_COMMAND_SYMBOLS,
            m_mb->command_histograms[i].data.data(),
            good_for_rle
        );
    }

    for (i = 0; i < m_mb->distance_histograms_size; ++i)
    {
        BrotliOptimizeHuffmanCountsForRle(
            num_distance_codes,
            m_mb->distance_histograms[i].data.data(),
            good_for_rle
        );
    }
}

void BrotligMetaBlock::StoreCompressedMetaBlockHeader(BrotligSwizzler* sw)
{
    uint64_t lenbits = 0;
    size_t nlenbits = 0;
    uint64_t nibblesbits = 0;
    size_t bitswritten = 0;

#if LGWIN_FIELD
    assert(m_state->params->lgwin < 31);
    sw->AppendToHeader(5, m_state->params->lgwin); bitswritten += 5;
#endif // LGWIN_FIELD                                            

#if ISLAST_FLAG
    /* Write ISLAST bit. */
    sw->AppendToHeader(1, (uint64_t)m_state->isLast); bitswritten += 1;
#endif

#if ISEMPTY_FLAG
    /* Write ISEMPTY bit. */
    if (m_state->isLast) {
        sw->AppendToHeader(1, 0);
        bitswritten += 1;
    }
#endif

#if ISUNCOMPRESSED_FLAG
    if (!m_state->isLast) {
        /* Write ISUNCOMPRESSED bit. */
        sw->AppendToHeader(1, 0);
        bitswritten += 1;
    }
#endif

#if DIST_POSTFIX_BITS_FIELD
    sw->AppendToHeader(2, m_state->params->dist_params.distance_postfix_bits);  bitswritten += 2;
#endif

#if NUM_DIRECT_DIST_CODES_FIELD
    sw->AppendToHeader(
        4,
        m_state->params->dist_params.num_direct_distance_codes >> m_state->params->dist_params.distance_postfix_bits
    );
    bitswritten += 4;
#endif

#if RESERVE_BITS
    sw->AppendToHeader(
        2,
        0
    );
    bitswritten += 2;
#endif

#if PAD_HEADER
    sw->AppendToHeader(static_cast<uint32_t>(32 - bitswritten), 0);
    bitswritten = 0;
#endif

#if UNCOMPLEN_FIELD
    EncodeMLen(m_input->size, lenbits, nlenbits, nibblesbits);
    assert(nibblesbits <= 3);
    sw->AppendToHeader(2, static_cast<uint32_t>(nibblesbits)); bitswritten += 2;
    sw->AppendToHeader(static_cast<uint32_t>(nlenbits), static_cast<uint32_t>(lenbits)); bitswritten += nlenbits;
#endif

#if PAD_HEADER
    sw->AppendToHeader(static_cast<uint32_t>(32 - bitswritten), 0);
#endif

    sw->AppendBitstreamSizes();

#if USE_COMPACT_SERIALIZTION
    sw->SerializeHeader();
#else
    sw->Serialize(true, false);
#endif
}