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

#include "common/BrotligConstants.h"
#include "common/BrotligHuffman.h"
#include "common/BrotligCommon.h"

#include "BrotligHistogram.h"
#include "BrotligEncoderState.h"

#define HISTOGRAMS_PER_BATCH 64
#define CLUSTERS_PER_BATCH 16

namespace BrotliG
{

template <BrotligElementTypes T, typename t>
struct BrotligBlockSplit
{
    size_t num_types;
    size_t num_blocks;
    std::vector<uint8_t> types;
    std::vector<uint32_t> lengths;

    BrotligBlockSplit()
    {
        num_types = 0;
        num_blocks = 0;
    }

    void Build(const std::vector<t>& data,
        const size_t symbols_per_histogram,
        const size_t max_histograms,
        const size_t sampling_stride_length,
        const double block_switch_cost,
        BrotligEncoderParams* params);

private:
    size_t FindBlocks(
        const std::vector<t>& data,
        const double block_switch_bitcost,
        const size_t num_histograms,
        const std::vector<BrotligHistogram<T, t>>& histograms,
        std::vector<double>& insert_cost,
        std::vector<double>& cost,
        std::vector<uint8_t>& switch_signal,
        std::vector<uint8_t>& block_id
    );

    size_t RemapBlockIds(
        std::vector<uint8_t>& block_ids,
        std::vector<uint16_t>& new_id,
        const size_t num_histograms
    );

    void BuildBlockHistograms(
        const std::vector<t>& data,
        const std::vector<uint8_t>& block_ids,
        const size_t num_histograms,
        std::vector<BrotligHistogram<T, t>>& histograms
    );

    void ClusterBlocks(
        const std::vector<t>& data,
        const size_t n_blocks,
        std::vector<uint8_t> block_ids
    );
};

template <BrotligElementTypes T, typename t>
size_t BrotligBlockSplit<T, t>::FindBlocks(
    const std::vector<t>& data,
    const double block_switch_bitcost,
    const size_t num_histograms,
    const std::vector<BrotligHistogram<T, t>>& histograms,
    std::vector<double>& insert_cost,
    std::vector<double>& cost,
    std::vector<uint8_t>& switch_signal,
    std::vector<uint8_t>& block_id
)
{
    const size_t length = data.size();
    const size_t alphabet_size = histograms.at(0).size;
    const size_t bitmap_len = (num_histograms + 7) >> 3;
    size_t n_blocks = 1;
    size_t byte_ix;

    assert(num_histograms <= 256);

    /* Trivial case: single historgram -> single block type. */
    if (num_histograms <= 1)
    {
        for (size_t i = 0; i < length; ++i)
        {
            block_id[i] = 0;
        }

        return 1;
    }

    /* Fill bitcost for each symbol of all histograms.
   * Non-existing symbol cost: 2 + log2(total_count).
   * Regular symbol cost: -log2(symbol_count / total_count). */
    std::fill(insert_cost.begin(), insert_cost.end(), 0);
    for (size_t i = 0; i < num_histograms; ++i)
    {
        insert_cost[i] = FastLog2((uint32_t)histograms[i].total_count);
    }
    for (size_t i = alphabet_size; i != 0;)
    {
        --i;
        for (size_t j = 0; j < num_histograms; ++j)
        {
            BrotligHistogram<T, t> hist = histograms[j];
            insert_cost[i * num_histograms + j] = insert_cost[j] - hist.BitCost(i);
        }
    }

    /* After each iteration of this loop, cost[k] will contain the difference
     between the minimum cost of arriving at the current byte position using
     entropy code k, and the minimum cost of arriving at the current byte
     position. This difference is capped at the block switch cost, and if it
     reaches block switch cost, it means that when we trace back from the last
     position, we need to switch here. */
    std::fill(cost.begin(), cost.end(), 0);
    std::fill(switch_signal.begin(), switch_signal.end(), 0);
    for (byte_ix = 0; byte_ix < length; ++byte_ix)
    {
        size_t ix = byte_ix * bitmap_len;
        size_t symbol = data[byte_ix];
        size_t insert_cost_ix = symbol * num_histograms;
        double min_cost = 1e99;
        double block_switch_cost = block_switch_bitcost;
        size_t k = 0;
        for (k = 0; k < num_histograms; ++k)
        {
            /* We are coding the symbol with entropy code k. */
            cost[k] += insert_cost[insert_cost_ix + k];
            if (cost[k] < min_cost)
            {
                min_cost = cost[k];
                block_id[byte_ix] = (uint8_t)k;
            }
        }

        /* More blocks for the beginning. */
        if (byte_ix < 2000)
        {
            block_switch_cost *= 0.77 + 0.07 * (double)byte_ix / 2000;
        }

        for (k = 0; k < num_histograms; ++k)
        {
            cost[k] -= min_cost;
            if (cost[k] >= block_switch_cost)
            {
                const uint8_t mask = (uint8_t)(1u << (k & 7));
                cost[k] = block_switch_cost;
                assert((k >> 3) < bitmap_len);
                switch_signal[ix + (k >> 3)] = mask;
            }
        }
    }

    byte_ix = length - 1;
    {
        /* Trace back from the last position and switch at the marked places. */
        size_t ix = byte_ix * bitmap_len;
        uint8_t cur_id = block_id[byte_ix];
        while (byte_ix > 0)
        {
            const uint8_t mask = (uint8_t)(1u << (cur_id & 7));
            assert(((size_t)cur_id >> 3) < bitmap_len);
            --byte_ix;
            ix -= bitmap_len;
            if (switch_signal[ix + (cur_id >> 3)] & mask)
            {
                if (cur_id != block_id[byte_ix])
                {
                    cur_id = block_id[byte_ix];
                    ++n_blocks;
                }
            }
            block_id[byte_ix] = cur_id;
        }
    }

    return n_blocks;
}

template <BrotligElementTypes T, typename t>
size_t BrotligBlockSplit<T, t>::RemapBlockIds(
    std::vector<uint8_t>& block_ids,
    std::vector<uint16_t>& new_id,
    const size_t num_histograms
)
{
    const size_t length = block_ids.size();
    static const uint16_t kInvalidId = 256;
    uint16_t next_id = 0;
    size_t i;
    for (i = 0; i < num_histograms; ++i) {
        new_id[i] = kInvalidId;
    }

    for (i = 0; i < length; ++i) {
        assert(block_ids[i] < num_histograms);
        if (new_id[block_ids[i]] == kInvalidId) {
            new_id[block_ids[i]] = next_id++;
        }
    }

    for (i = 0; i < length; ++i) {
        block_ids[i] = (uint8_t)new_id[block_ids[i]];
        assert(block_ids[i] < num_histograms);
    }

    assert(next_id <= num_histograms);
    return next_id;
}

template <BrotligElementTypes T, typename t>
void BrotligBlockSplit<T, t>::BuildBlockHistograms(
    const std::vector<t>& data,
    const std::vector<uint8_t>& block_ids,
    const size_t num_histograms,
    std::vector<BrotligHistogram<T, t>>& histograms
)
{
    const size_t length = data.size();
    size_t i = 0;
    BrotligHistogram<T, t>::ClearHistograms(histograms);

    for (size_t i = 0; i < length; ++i)
    {
        histograms.at(block_ids[i]).Add(data[i]);
    }
}

/* Given the initial partitioning build partitioning with limited number
 * of histograms (and block types). */
template <BrotligElementTypes T, typename t>
void BrotligBlockSplit<T, t>::ClusterBlocks(
    const std::vector<t>& data,
    const size_t n_blocks,
    std::vector<uint8_t> block_ids
)
{
    const size_t length = data.size();
    std::vector<uint32_t> histogram_symbols(n_blocks);
    const size_t expected_num_clusters = CLUSTERS_PER_BATCH *
        (n_blocks + HISTOGRAMS_PER_BATCH - 1) / HISTOGRAMS_PER_BATCH;
    size_t all_histograms_size = 0;
    size_t all_histograms_capacity = expected_num_clusters;
    std::vector<BrotligHistogram<T, t>> all_histograms(all_histograms_capacity);
    size_t cluster_size_size = 0;
    size_t cluster_size_capacity = expected_num_clusters;
    std::vector<uint32_t> cluster_size(cluster_size_capacity);
    size_t num_clusters = 0;
    std::vector<BrotligHistogram<T, t>> histograms(BROTLI_MIN(size_t, n_blocks, HISTOGRAMS_PER_BATCH));

    size_t max_num_pairs =
        HISTOGRAMS_PER_BATCH * HISTOGRAMS_PER_BATCH / 2;
    size_t pairs_capacity = max_num_pairs + 1;
    std::vector<BrotligHistogramPair> pairs(pairs_capacity);
    size_t pos = 0;
    size_t num_final_clusters;
    static const uint32_t kInvalidIndex = BROTLI_UINT32_MAX;
    std::vector<uint32_t> sizes(HISTOGRAMS_PER_BATCH, 0);
    std::vector<uint32_t> new_clusters(HISTOGRAMS_PER_BATCH, 0);
    std::vector<uint32_t> symbols(HISTOGRAMS_PER_BATCH, 0);
    std::vector<uint32_t> remap(HISTOGRAMS_PER_BATCH, 0);
    std::vector<uint32_t> block_lengths(HISTOGRAMS_PER_BATCH + n_blocks, 0);

    BrotligHistogram<T, t> temp, temp2;

    /* Calculate block lengths (convert repeating values -> series length). */
    {
        size_t block_idx = 0;
        for (size_t i = 0; i < length; ++i) {
            assert(block_idx < n_blocks);
            ++block_lengths[block_idx];
            if (i + 1 == length || block_ids[i] != block_ids[i + 1]) {
                ++block_idx;
            }
        }
        assert(block_idx == n_blocks);
    }

    /* Pre-cluster blocks (cluster batches). */
    for (size_t i = 0; i < n_blocks; i += HISTOGRAMS_PER_BATCH) {
        const size_t num_to_combine =
            BROTLI_MIN(size_t, n_blocks - i, HISTOGRAMS_PER_BATCH);
        size_t num_new_clusters;
        size_t j;
        for (j = 0; j < num_to_combine; ++j) {
            size_t k;
            size_t block_length = block_lengths[i + j];
            histograms.at(j).Clear();
            for (k = 0; k < block_length; ++k) {
                histograms.at(j).Add(data[pos++]);
            }
            histograms[j].bit_cost = histograms[j].PopulationCost();
            new_clusters[j] = (uint32_t)j;
            symbols[j] = (uint32_t)j;
            sizes[j] = 1;
        }

        num_new_clusters = BrotligHistogram<T, t>::Combine(
            histograms,
            temp,
            sizes,
            symbols,
            new_clusters,
            pairs,
            num_to_combine,
            num_to_combine,
            HISTOGRAMS_PER_BATCH,
            max_num_pairs);

        all_histograms.resize(all_histograms_size + num_new_clusters);
        //all_histograms_size = all_histograms.size();
        cluster_size.resize(cluster_size_size + num_new_clusters);
        //cluster_size_size = cluster_size.size();

        for (j = 0; j < num_new_clusters; ++j) {
            all_histograms[all_histograms_size++] = histograms[new_clusters[j]];
            cluster_size[cluster_size_size++] = sizes[new_clusters[j]];
            remap[new_clusters[j]] = (uint32_t)j;
        }
        for (j = 0; j < num_to_combine; ++j) {
            histogram_symbols[i + j] = (uint32_t)num_clusters + remap[symbols[j]];
        }
        num_clusters += num_new_clusters;
        assert(num_clusters == cluster_size_size);
        assert(num_clusters == all_histograms_size);
    }

    histograms.clear();

    /* Final clustering. */
    max_num_pairs = BROTLI_MIN(size_t, 64 * num_clusters, (num_clusters / 2) * num_clusters);
    if (pairs_capacity < max_num_pairs + 1) {
        pairs.clear();
        pairs.reserve(max_num_pairs + 1);
    }

    std::vector<uint32_t> clusters(num_clusters);

    for (size_t i = 0; i < num_clusters; ++i) {
        clusters[i] = (uint32_t)i;
    }
    num_final_clusters = BrotligHistogram<T, t>::Combine(
        all_histograms,
        temp,
        cluster_size,
        histogram_symbols,
        clusters,
        pairs,
        num_clusters,
        n_blocks,
        BROTLI_MAX_NUMBER_OF_BLOCK_TYPES,
        max_num_pairs
    );

    pairs.clear();
    cluster_size.clear();

    /* Assign blocks to final histograms. */
    std::vector<uint32_t> new_index(num_clusters, kInvalidIndex);
    pos = 0;
    {
        uint32_t next_index = 0;
        for (size_t i = 0; i < n_blocks; ++i)
        {
            size_t j = 0;
            uint32_t best_out = 0;
            double best_bits = 0.0;
            temp.Clear();
            for (j = 0; j < block_lengths[i]; ++j)
            {
                temp.Add(data[pos++]);
            }

            /* Among equally good histograms prefer last used. */
            best_out = (i == 0) ? histogram_symbols[0] : histogram_symbols[i - 1];
            best_bits = BrotligHistogram<T, t>::BitCostDistance(
                temp,
                all_histograms[best_out],
                temp2
            );

            for (j = 0; j < num_final_clusters; ++j)
            {
                const double cur_bits = BrotligHistogram<T, t>::BitCostDistance(
                    temp,
                    all_histograms[clusters[j]],
                    temp2
                );

                if (cur_bits < best_bits)
                {
                    best_bits = cur_bits;
                    best_out = clusters[j];
                }
            }

            histogram_symbols[i] = best_out;
            if (new_index[best_out] == kInvalidIndex)
            {
                new_index[best_out] = next_index++;
            }
        }
    }

    clusters.clear();
    all_histograms.clear();
    types.clear();
    lengths.clear();

    /* Rewrite final assignment to block-split. There might be less blocks
  * than |n_blocks| due to clustering. */

    {
        uint32_t cur_length = 0;
        uint8_t max_type = 0;
        for (size_t i = 0; i < n_blocks; ++i)
        {
            cur_length += block_lengths[i];
            if (i + 1 == n_blocks
                || histogram_symbols[i] != histogram_symbols[i + 1])
            {
                const uint8_t id = (uint8_t)new_index[histogram_symbols[i]];
                types.push_back(id);
                lengths.push_back(cur_length);
                max_type = std::max(max_type, id);
                cur_length = 0;
            }
        }

        num_blocks = types.size();
        num_types = (size_t)max_type + 1;
    }

    new_index.clear();
    histogram_symbols.clear();
}

template <BrotligElementTypes T, typename t>
void BrotligBlockSplit<T, t>::Build(
    const std::vector<t>& data,
    const size_t symbols_per_histogram,
    const size_t max_histograms,
    const size_t sampling_stride_length,
    const double block_switch_cost,
    BrotligEncoderParams* params
)
{
    std::vector<BrotligHistogram<T, t>> histograms;
    BrotligHistogram<T, t> temp;
    size_t data_size = temp.size;
    size_t length = data.size();

    size_t num_histograms = length / symbols_per_histogram + 1;
    if (num_histograms > max_histograms)
        num_histograms = max_histograms;

    if (length == 0)
    {
        num_types = 1;
        return;
    }

    if (length < kMinLengthForBlockSplitting)
    {
        types.resize(num_blocks + 1);
        lengths.resize(num_blocks + 1);

        num_types = 1;
        types[num_blocks] = 0;
        lengths[num_blocks] = (uint32_t)length;
        num_blocks++;
        return;
    }


    histograms.resize(num_histograms);

    // Initialize histograms
    uint32_t seed = 7;
    size_t blocklength = length / num_histograms;

    BrotligHistogram<T, t>::ClearHistograms(histograms);

    for (size_t i = 0; i < num_histograms; ++i)
    {
        size_t pos = length * i / num_histograms;
        if (i != 0)
        {
            seed *= 16807U;
            pos += seed % blocklength;
        }

        if (pos + sampling_stride_length >= length)
            pos = length - sampling_stride_length - 1;

        std::vector<t> data_t;
        for (size_t j = 0; j < sampling_stride_length; ++j)
        {
            data_t.push_back(data[pos + j]);
        }
        histograms.at(i).AddVector(data_t.data(), sampling_stride_length);
    }

    // Refine the entropy codes
    size_t iters = kIterMulForRefining * length / sampling_stride_length + kMinItersForRefining;
    seed = 7;
    size_t iter;
    iters = ((iters + num_histograms - 1) / num_histograms) * num_histograms;
    for (iter = 0; iter < iters; ++iter)
    {
        temp.Clear();
        temp.RandomSample(seed, data, sampling_stride_length);
        histograms.at(iter % num_histograms).AddHistogram(temp);
    }

    {
        /* Find a good path through literals with the good entropy codes. */
        std::vector<uint8_t> blockids(length);
        size_t n_blocks = 0;
        const size_t bitmaplen = (num_histograms + 7) >> 3;
        std::vector<double> insert_cost(data_size * num_histograms);
        std::vector<double> cost(num_histograms);
        std::vector<uint8_t> switch_signal(length * bitmaplen);
        std::vector<uint16_t> new_id(num_histograms);
        const size_t iters = params->quality < HQ_ZOPFLIFICATION_QUALITY ? 3 : 10;

        for (size_t i = 0; i < iters; ++i)
        {
            n_blocks = FindBlocks(
                data,
                block_switch_cost,
                num_histograms,
                histograms,
                insert_cost,
                cost,
                switch_signal,
                blockids
            );

            num_histograms = RemapBlockIds(
                blockids,
                new_id,
                num_histograms
            );

            BuildBlockHistograms(
                data,
                blockids,
                num_histograms,
                histograms
            );
        }

        insert_cost.clear();
        cost.clear();
        switch_signal.clear();
        new_id.clear();
        histograms.clear();

        ClusterBlocks(
            data,
            n_blocks,
            blockids
        );

        blockids.clear();
    }
}

template <BrotligElementTypes T, typename t>
struct BrotligBlockSplitIterator
{
    const BrotligBlockSplit<T, t>* split;
    size_t idx;
    size_t type;
    size_t length;

    BrotligBlockSplitIterator(BrotligBlockSplit<T, t>* s)
    {
        split = s;
        idx = 0;
        type = 0;
        length = (split->lengths.size() > 0) ? split->lengths.at(0) : 0;
    }

    ~BrotligBlockSplitIterator()
    {
        split = nullptr;
    }

    BrotligBlockSplitIterator& operator++()
    {
        --length;

        if (length == 0)
        {
            ++idx;
            if (idx < split->types.size())
            {
                type = split->types.at(idx);
                length = split->lengths.at(idx);
            }
        }

        return *this;
    }

    BrotligBlockSplitIterator& operator++(int)
    {
        BrotligBlockSplitIterator temp = *this;

        --length;

        if (length == 0)
        {
            ++idx;
            if (idx < split->types.size())
            {
                type = split->types.at(idx);
                length = split->lengths.at(idx);
            }
        }
        return temp;
    }
};

typedef struct BrotligBlockTypeCodeCalculator {
    size_t last_type;
    size_t second_last_type;

    void Init()
    {
        last_type = 1;
        second_last_type = 0;
    }

    size_t NextBlockTypeCode(uint8_t type)
    {
        size_t type_code = (type == last_type + 1) ? 1u : (type == second_last_type) ? 0u : type + 2u;
        second_last_type = type;
        last_type = type;

        return type_code;
    }
} BrotligBlockTypeCodeCalculator;

/* Data structure that stores almost everything that is needed to encode each
block switch command. */
typedef struct BrotligBlockSplitCode {
    BrotligBlockTypeCodeCalculator type_code_calculator;
    //uint8_t type_depths[BROTLI_MAX_BLOCK_TYPE_SYMBOLS];
    //uint16_t type_bits[BROTLI_MAX_BLOCK_TYPE_SYMBOLS];
    //uint8_t length_depths[BROTLI_NUM_BLOCK_LEN_SYMBOLS];
    //uint16_t length_bits[BROTLI_NUM_BLOCK_LEN_SYMBOLS];
    BrotligHuffmanTree* type_tree;
    BrotligHuffmanTree* length_tree;
    bool isCopy;

    BrotligBlockSplitCode()
    {
        type_tree = nullptr;
        length_tree = nullptr;
        isCopy = false;
    }

    ~BrotligBlockSplitCode()
    {
        if (!isCopy)
        {
            if(type_tree)
                delete type_tree;
            if(length_tree)
                delete length_tree;
        }
        else
        {
            type_tree = nullptr;
            length_tree = nullptr;
        }
    }
} BrotligBlockSplitCode;

template <BrotligElementTypes T, typename t>
struct BrotligBlockEncoder
{
    size_t histogram_length;
    size_t num_types;
    std::vector<uint8_t> block_types;
    std::vector<uint32_t> block_lengths;
    BrotligBlockSplitCode block_split_code;
    size_t block_ix;
    size_t block_len;
    uint8_t block_type;
    size_t entropy_ix;

    size_t cache_block_ix;
    size_t cache_block_len;
    
    std::vector<BrotligHuffmanTree*> org_block_trees;
    std::vector<BrotligHuffmanTree*> copy_block_trees;

    std::map<uint16_t, uint32_t> length_histo;

    BrotligBlockEncoder()
    {
        histogram_length = 0;
        num_types = 0;
        block_types.clear();
        block_lengths.clear();
        block_ix = 0;
        block_len = 0;
        block_type = 0;
        entropy_ix = 0;

        cache_block_ix = 0;
        cache_block_len = 0;
    }

    ~BrotligBlockEncoder()
    {
        histogram_length = 0;
        num_types = 0;
        block_types.clear();
        block_lengths.clear();
        block_ix = 0;
        block_len = 0;
        block_type = 0;
        entropy_ix = 0;

        for (size_t index = 0; index < org_block_trees.size(); ++index)
        {
            delete org_block_trees.at(index);
        }

        copy_block_trees.clear();
    }

    void Initialize(
        BrotligBlockSplit<T, t> split, 
        size_t histogram_length_p)
    {
        histogram_length = histogram_length_p;
        num_types = split.num_types;
        block_types = split.types;
        block_lengths = split.lengths;
        block_split_code.type_code_calculator.Init();
        block_split_code.isCopy = false;
        block_ix = 0;
        block_len = (block_lengths.size() == 0) ? 0 : block_lengths[block_ix];
        block_type = (block_types.size() == 0) ? 0: block_types[block_ix];
        entropy_ix = 0;
    }

    void Initialize(BrotligBlockEncoder* other)
    {
        histogram_length = other->histogram_length;
        num_types = other->num_types;
        block_split_code = other->block_split_code;
        block_split_code.isCopy = true;
        block_type = (other->block_types.size() == 0) ? 0 : other->block_types[other->block_ix];
        block_ix = 0;
        block_len = 0;

        CopyBlockTrees(other->org_block_trees);
    }

    void CopyBlockTrees(const std::vector<BrotligHuffmanTree*>& other_trees)
    {
        for (size_t i = 0; i < other_trees.size(); ++i)
        {
            copy_block_trees.push_back(other_trees.at(i));
        }
    }

    void Reset()
    {
        block_ix = 0;
        block_len = (block_lengths.size() == 0) ? 0 : block_lengths[0];
        block_type = (block_types.size() == 0) ? 0 : block_types[0];
    }

    void PushNewBlockType()
    {
        block_types.push_back(block_type);
        block_lengths.push_back(static_cast<uint32_t>(block_len));
    }

    uint32_t BlockLengthPrefixCode(uint32_t len)
    {
        uint32_t code = (len >= 177) ? (len >= 753 ? 20 : 14) : (len >= 41 ? 7 : 0);
        while (code < (BROTLI_NUM_BLOCK_LEN_SYMBOLS - 1) &&
            len >= _kBrotliPrefixCodeRanges[code + 1].offset) ++code;
        return code;
    }

    void GetBlockLengthPrefixCode(
        uint32_t len,
        size_t* code,
        uint32_t* n_extra,
        uint32_t* extra
    )
    {
        *code = BlockLengthPrefixCode(len);
        *n_extra = _kBrotliPrefixCodeRanges[*code].nbits;
        *extra = len - _kBrotliPrefixCodeRanges[*code].offset;
    }

    void StoreBlockSwitch(
        size_t index,
        bool is_first_block,
        BrotligBitWriter* bw
    )
    {
        //BrotligBlockTypeCodeCalculator& type_code_calculator = block_split_code.type_code_calculator;
        if (block_lengths.size() > 0)
        {
            size_t typecode = block_types[index];
            size_t lencode = 0;
            uint32_t len_nextra = 0;
            uint32_t len_extra = 0;
            if (!is_first_block)
            {
                /*bw->Write(
                    block_split_code.type_depths[typecode],
                    block_split_code.type_bits[typecode]
                );*/
                bw->Write(
                    block_split_code.type_tree->Bitsize(typecode),
                    block_split_code.type_tree->Revcode(typecode)
                );
            }
            GetBlockLengthPrefixCode(
                block_len,
                &lencode,
                &len_nextra,
                &len_extra
            );

            bw->Write(
                block_split_code.length_tree->Bitsize(lencode),
                block_split_code.length_tree->Revcode(lencode)
            );

            bw->Write(
                len_nextra,
                len_extra
            );
        }
    }

    void BuildAndStoreBlockSplitCode(BrotligSwizzler* sw)
    {   
        size_t i = 0;
        size_t num_blocks = block_lengths.size();
        if (num_blocks > 0)
        {
            std::vector<uint32_t> type_histo(BROTLI_MAX_BLOCK_TYPE_SYMBOLS, 0);
            std::vector<uint32_t> len_histo(BROTLI_NUM_BLOCK_LEN_SYMBOLS, 0);

            for (i = 0; i < num_blocks; ++i)
            {
                size_t type_code = block_types[i];
                    ++type_histo[type_code];
            }

            // Add sentinel block switch type to the histogram
            assert(type_histo[257] == 0);
            ++type_histo[257];

            {
                block_split_code.type_tree = new BrotligHuffmanTree;
                block_split_code.type_tree->Build(type_histo);
                block_split_code.type_tree->Store(sw, BROTLI_MAX_BLOCK_TYPE_SYMBOLS);
            }

            sw->Reset();

            std::map<uint16_t, uint32_t>::iterator iter = length_histo.begin();
            while (iter != length_histo.end())
            {
                len_histo[iter->first] = iter->second;
                ++iter;
            }

            // Add sentinel block switch len to the histogram
            // Sentitnel block switch len = 0;
            // If not present, add it
            ++len_histo[0];

            {
                block_split_code.length_tree = new BrotligHuffmanTree;
                block_split_code.length_tree->Build(len_histo);
                block_split_code.length_tree->Store(sw, BROTLI_NUM_BLOCK_LEN_SYMBOLS);
            }

            sw->Reset();
        }
    }

    void BuildAndStoreEntropyCodes(
        const std::vector<BrotligHistogram<T, t>> histograms,
        BrotligSwizzler* sw,
        size_t alphabet_size,
        bool debug = false
    )
    {
        size_t num_histograms = histograms.size();

        for (size_t i = 0; i < num_histograms; ++i)
        {
            BrotligHistogram<T, t> histogram = histograms.at(i);
            BrotligHuffmanTree* tree = new BrotligHuffmanTree();

            if (tree->Build(histogram.data))
            {
                tree->Store(sw, alphabet_size, debug);
                org_block_trees.push_back(tree);
            }
        }
    }

    void TransferFromBE(BrotligBlockEncoder<T, t>* out)
    {
        if (out->block_len == 0)
        {
            // push the new type information to new block encoder
            PushNewBlockType();

            // switch the block for the old encoder
            ++out->block_ix;
            out->block_len = out->block_lengths[out->block_ix];
            out->block_type = out->block_types[out->block_ix];

            // initialize a new block for the new block encoder
            ++block_ix;
            block_type = out->block_type;
            block_len = 0; 
        }

        ++block_len;
        --out->block_len;
    }

    void StoreSymbol(size_t symbol, BrotligBitWriter* bw)
    {
        if (block_len == 0)
        {
            ++block_ix;
            block_len = block_lengths[block_ix];
            block_type = block_types[block_ix];
            entropy_ix = block_type * histogram_length;
            /*StoreBlockSwitch(
                block_ix,
                0,
                bw
            );*/
        }
        --block_len;
        {
            size_t hist_ix = block_type;
            size_t codebitsize = copy_block_trees.at(hist_ix)->Bitsize(static_cast<uint16_t>(symbol));
            uint16_t code = copy_block_trees.at(hist_ix)->Revcode(static_cast<uint16_t>(symbol));
            bw->Write(codebitsize, code);
        }
    }

    void StoreNFetchSymbol(size_t symbol, BrotligBitWriter* bw, size_t& codelen, uint16_t& code)
    {
        if (block_len == 0)
        {
            ++block_ix;
            block_len = block_lengths[block_ix];
            block_type = block_types[block_ix];
            entropy_ix = block_type * histogram_length;
            /*StoreBlockSwitch(
                block_ix,
                0,
                bw
            );*/
        }
        --block_len;
        {
            size_t hist_ix = block_type;
            codelen = copy_block_trees.at(hist_ix)->Bitsize(static_cast<uint16_t>(symbol));
            code = copy_block_trees.at(hist_ix)->Revcode(static_cast<uint16_t>(symbol));
            bw->Write(codelen, code);
        }
    }

    void StoreSymbolWithContext(
        size_t symbol,
        size_t context,
        const std::vector<uint32_t>& context_map,
        BrotligBitWriter* bw,
        const size_t context_bits)
    {
        if (block_len == 0)
        {
            ++block_ix;
            block_len = block_lengths[block_ix];
            block_type = block_types[block_ix];
            /*StoreBlockSwitch(
                block_ix,
                0,
                bw
            );*/
        }
        --block_len;
        {
            entropy_ix = (static_cast<size_t>(block_type)) << context_bits;
            size_t hist_ix = context_map[entropy_ix + context];
            assert(hist_ix == 0);
            //size_t ix = hist_ix * histogram_length + symbol;
            //uint8_t depth = depths[ix];
            //uint16_t bits = depthbits[ix];
            size_t codebitsize = copy_block_trees.at(hist_ix)->Bitsize(static_cast<uint16_t>(symbol));
            uint16_t code = copy_block_trees.at(hist_ix)->Revcode(static_cast<uint16_t>(symbol));
            bw->Write(codebitsize, code);
        }
    }

    void StoreNFetchSymbolWithContext(
        size_t symbol,
        size_t context,
        const std::vector<uint32_t>& context_map,
        BrotligBitWriter* bw,
        const size_t context_bits,
        size_t& codelen, 
        uint16_t& code)
    {
        if (block_len == 0)
        {
            ++block_ix;
            block_len = block_lengths[block_ix];
            block_type = block_types[block_ix];
            /*StoreBlockSwitch(
                block_ix,
                0,
                bw
            );*/
        }
        --block_len;
        {
            entropy_ix = (static_cast<size_t>(block_type)) << context_bits;
            size_t hist_ix = context_map[entropy_ix + context];
            assert(hist_ix == 0);
            //size_t ix = hist_ix * histogram_length + symbol;
            //uint8_t depth = depths[ix];
            //uint16_t bits = depthbits[ix];
            codelen = copy_block_trees.at(hist_ix)->Bitsize(static_cast<uint16_t>(symbol));
            code = copy_block_trees.at(hist_ix)->Revcode(static_cast<uint16_t>(symbol));
            bw->Write(codelen, code);
        }
    }
};

}
