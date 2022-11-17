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
#include "brotli/c/common/constants.h"
#include "brotli/c/enc/bit_cost.h"
#include "brotli/c/enc/cluster.h"
}

#include "BrotligConstants.h"
#include "BrotligCommon.h"

namespace BrotliG
{

    typedef enum BrotligElementTypes
    {
        Literal,
        Insert_and_copy,
        Distance
    }BrotligElementTypes;

    struct BrotligHistogramPair
    {
        uint32_t idx1;
        uint32_t idx2;
        double cost_combo;
        double cost_diff;
    };

    class BrotligHistogramPairCompare
    {
    public:
        bool operator() (const BrotligHistogramPair& c1, const BrotligHistogramPair& c2) const
        {
            if (c1.cost_diff != c2.cost_diff) {
                return (c1.cost_diff > c2.cost_diff);
            }
            return ((c1.idx2 - c1.idx1) > (c2.idx2 - c2.idx1));
        }
    };

    template <BrotligElementTypes T, typename t>
    struct BrotligHistogram
    {
        std::vector<uint32_t> data;
        size_t total_count;
        double bit_cost;
        BrotligElementTypes type;
        size_t size;
        double cost_diff;

        BrotligHistogram& operator=(BrotligHistogram other)
        {
            data = other.data;
            total_count = other.total_count;
            bit_cost = other.bit_cost;
            type = other.type;
            size = other.size;
            cost_diff = 0;

            return *this;
        }

        BrotligHistogram()
        {
            type = T;
            switch (type)
            {
            case Literal:
                size = BROTLI_NUM_LITERAL_SYMBOLS;
                break;
            
            case Insert_and_copy:
                size = BROLTIG_NUM_COMMAND_SYMBOLS_EFFECTIVE;
                break;

            case Distance:
                size = BROTLIG_NUM_DISTANCE_SYMBOLS;
                break;
            }

            data.resize(size, 0);
        }

        void Clear()
        {
            std::fill(data.begin(), data.end(), 0);
            total_count = 0;
            bit_cost = HUGE_VAL;
        }

        void Add(size_t val)
        {
            ++data[val];
            ++total_count;
        }

        void AddVector(t* vptr, size_t n)
        {
            total_count += n;
            n += 1;
            while (--n) ++data[*vptr++];
        }

        void AddHistogram(const BrotligHistogram<T, t> other)
        {
            size_t i = 0;
            total_count += other.total_count;
            for (i = 0; i < size; ++i)
            {
                data[i] += other.data[i];
            }
        }

        double PopulationCost();

        void RandomSample(
            uint32_t& seed,
            const std::vector<t>& data,
            size_t stride)
        {
            size_t length = data.size();
            size_t pos = 0;
            if (stride >= length)
                stride = length;
            else
            {
                seed *= 16807U;
                pos = seed % (length - stride + 1);
            }
        
            std::vector<t> data_t;
            for (size_t j = 0; j < stride; ++j)
            {
                data_t.push_back(data[pos + j]);
            }
            AddVector(data_t.data(), stride);
        }

        double BitCost(size_t index)
        {
            return (data[index] == 0) ? -2.0 : FastLog2(data[index]);
        }

        static size_t Combine(
            std::vector<BrotligHistogram<T, t>>& out,
            BrotligHistogram<T, t>& temp,
            std::vector<uint32_t>& cluster_size,
            std::vector<uint32_t>& symbols,
            std::vector<uint32_t>& clusters,
            std::vector<BrotligHistogramPair>& pairs,
            size_t num_clusters,
            size_t symbols_size,
            size_t max_clusters,
            size_t max_num_pairs
        );

        static void CompareAndPushToQueue(
            std::vector<BrotligHistogram<T, t>>& out, 
            BrotligHistogram<T, t>& temp,
            const std::vector<uint32_t>& cluster_size,
            uint32_t idx1, 
            uint32_t idx2, 
            size_t max_num_pairs, 
            std::vector<BrotligHistogramPair>& pairs,
            size_t& num_pairs
        );

        static size_t Combine(
            std::vector<BrotligHistogram<T, t>>& out,
            std::vector<uint32_t>& symbols,
            std::vector<uint32_t>& cluster_size,
            std::set<size_t>& toRemove,
            size_t start_ix,
            size_t end_ix,
            size_t max_clusters,
            size_t max_num_pairs
        );

        static void CompareAndPushToQueue(
            std::priority_queue<BrotligHistogramPair, std::vector<BrotligHistogramPair>, BrotligHistogramPairCompare>& pQueue,
            const std::vector<BrotligHistogram<T, t>>& out,
            const std::vector<uint32_t>& cluster_size,
            size_t ix1,
            size_t ix2,
            size_t max_num_pairs
        );

        static double ClusterCostDiff(size_t size_a, size_t size_b) {
            size_t size_c = size_a + size_b;
            return (double)size_a * FastLog2(size_a) +
                (double)size_b * FastLog2(size_b) -
                (double)size_c * FastLog2(size_c);
        }

        static bool HistogramPairIsLess(
            const BrotligHistogramPair* p1, const BrotligHistogramPair* p2) {
            if (p1->cost_diff != p2->cost_diff) {
                return (p1->cost_diff > p2->cost_diff);
            }
            return ((p1->idx2 - p1->idx1) > (p2->idx2 - p2->idx1));
        }

        static double BitCostDistance(
            const BrotligHistogram<T, t>& histogram,
            const  BrotligHistogram<T, t>& candidate,
            BrotligHistogram<T, t>& temp
        )
        {
            if (histogram.total_count == 0)
            {
                return 0.0;
            }
            else
            {
                temp = histogram;
                temp.AddHistogram(candidate);
                return (temp.PopulationCost() - candidate.bit_cost);
            }
        }

        static void ClearHistograms(std::vector<BrotligHistogram<T, t>>& histograms)
        {
            for (size_t i = 0; i < histograms.size(); ++i)
            {
                histograms.at(i).Clear();
            }
        }

        static void ClusterHistograms(
            const std::vector<BrotligHistogram<T, t>>& in,
            size_t max_histograms,
            std::vector<BrotligHistogram<T, t>>& out,
            std::vector<uint32_t>& histogram_symbols
        );

        static void RemapHistograms(
            const std::vector<BrotligHistogram<T, t>>& in,
            const std::vector<uint32_t> clusters,
            size_t num_clusters,
            std::vector<BrotligHistogram<T, t>>& out,
            BrotligHistogram<T, t>& temp,
            std::vector<uint32_t>& symbols
        );

        static size_t ReindexHistograms(
            std::vector<BrotligHistogram<T, t>>& out,
            std::vector<uint32_t>& symbols,
            size_t length
        );
    };

    template <BrotligElementTypes T, typename t>
    double BrotligHistogram<T, t>::PopulationCost()
    {
        static const double kOneSymbolHistogramCost = 12;
        static const double kTwoSymbolHistogramCost = 20;
        static const double kThreeSymbolHistogramCost = 28;
        static const double kFourSymbolHistogramCost = 37;
        const size_t data_size = size;
        int count = 0;
        size_t s[5];
        double bits = 0.0;
        size_t i;

        if (total_count == 0) {
            return kOneSymbolHistogramCost;
        }

        for (i = 0; i < data_size; ++i) {
            if (data[i] > 0) {
                s[count] = i;
                ++count;
                if (count > 4) break;
            }
        }

        if (count == 1) {
            return kOneSymbolHistogramCost;
        }

        if (count == 2) {
            return (kTwoSymbolHistogramCost + (double)total_count);
        }

        if (count == 3) {
            const uint32_t histo0 = data[s[0]];
            const uint32_t histo1 = data[s[1]];
            const uint32_t histo2 = data[s[2]];
            const uint32_t histomax =
                std::max(histo0, std::max(histo1, histo2));
            return (kThreeSymbolHistogramCost +
                2 * (histo0 + histo1 + histo2) - histomax);
        }

        if (count == 4) {
            uint32_t histo[4];
            uint32_t h23;
            uint32_t histomax;
            for (i = 0; i < 4; ++i) {
                histo[i] = data[s[i]];
            }
            /* Sort */
            for (i = 0; i < 4; ++i) {
                size_t j;
                for (j = i + 1; j < 4; ++j) {
                    if (histo[j] > histo[i]) {
                        BROTLI_SWAP(uint32_t, histo, j, i);
                    }
                }
            }
            h23 = histo[2] + histo[3];
            histomax = std::max(h23, histo[0]);
            return (kFourSymbolHistogramCost +
                3 * h23 + 2 * (histo[0] + histo[1]) - histomax);
        }

        {
            /* In this loop we compute the entropy of the histogram and simultaneously
               build a simplified histogram of the code length codes where we use the
               zero repeat code 17, but we don't use the non-zero repeat code 16. */
            size_t max_depth = 1;
            uint32_t depth_histo[BROTLI_CODE_LENGTH_CODES] = { 0 };
            const double log2total = FastLog2(total_count);
            for (i = 0; i < data_size;) {
                if (data[i] > 0) {
                    /* Compute -log2(P(symbol)) = -log2(count(symbol)/total_count) =
                                                = log2(total_count) - log2(count(symbol)) */
                    double log2p = log2total - FastLog2(data[i]);
                    /* Approximate the bit depth by round(-log2(P(symbol))) */
                    size_t depth = (size_t)(log2p + 0.5);
                    bits += data[i] * log2p;
                    if (depth > 15) {
                        depth = 15;
                    }
                    if (depth > max_depth) {
                        max_depth = depth;
                    }
                    ++depth_histo[depth];
                    ++i;
                }
                else {
                    /* Compute the run length of zeros and add the appropriate number of 0
                       and 17 code length codes to the code length code histogram. */
                    uint32_t reps = 1;
                    size_t k;
                    for (k = i + 1; k < data_size && data[k] == 0; ++k) {
                        ++reps;
                    }
                    i += reps;
                    if (i == data_size) {
                        /* Don't add any cost for the last zero run, since these are encoded
                           only implicitly. */
                        break;
                    }
                    if (reps < 3) {
                        depth_histo[0] += reps;
                    }
                    else {
                        reps -= 2;
                        while (reps > 0) {
                            ++depth_histo[BROTLI_REPEAT_ZERO_CODE_LENGTH];
                            /* Add the 3 extra bits for the 17 code length code. */
                            bits += 3;
                            reps >>= 3;
                        }
                    }
                }
            }
            /* Add the estimated encoding cost of the code length code histogram. */
            bits += (double)(18 + 2 * max_depth);
            /* Add the entropy of the code length code histogram. */
            bits += BitsEntropy(depth_histo, BROTLI_CODE_LENGTH_CODES);
        }

        return bits;
    }

    /* Computes the bit cost reduction by combining out[idx1] and out[idx2] and if
       it is below a threshold, stores the pair (idx1, idx2) in the *pairs queue. */
    template <BrotligElementTypes T, typename t>
    void BrotligHistogram<T, t>::CompareAndPushToQueue(
        std::vector<BrotligHistogram<T, t>>& out,
        BrotligHistogram<T, t>& temp,
        const std::vector<uint32_t>& cluster_size,
        uint32_t idx1,
        uint32_t idx2,
        size_t max_num_pairs,
        std::vector<BrotligHistogramPair>& pairs,
        size_t& num_pairs) 
    {
        bool is_good_pair = false;
        BrotligHistogramPair p;
        p.idx1 = p.idx2 = 0;
        p.cost_diff = p.cost_combo = 0;
        if (idx1 == idx2) {
            return;
        }
        if (idx2 < idx1) {
            uint32_t t = idx2;
            idx2 = idx1;
            idx1 = t;
        }
        p.idx1 = idx1;
        p.idx2 = idx2;
        p.cost_diff = 0.5 * ClusterCostDiff(cluster_size[idx1], cluster_size[idx2]);
        p.cost_diff -= out[idx1].bit_cost;
        p.cost_diff -= out[idx2].bit_cost;

        if (out[idx1].total_count == 0) {
            p.cost_combo = out[idx2].bit_cost;
            is_good_pair = true;
        }
        else if (out[idx2].total_count == 0) {
            p.cost_combo = out[idx1].bit_cost;
            is_good_pair = true;
        }
        else {
            double threshold = num_pairs == 0 ? 1e99 : std::max(0.0, pairs[0].cost_diff);
            double cost_combo;
            temp = out[idx1];
            temp.AddHistogram(out[idx2]);
            cost_combo = temp.PopulationCost();
            if (cost_combo < threshold - p.cost_diff) {
                p.cost_combo = cost_combo;
                is_good_pair = true;
            }
        }

        if (is_good_pair) {
            p.cost_diff += p.cost_combo;
            if (num_pairs > 0 && HistogramPairIsLess(&pairs[0], &p)) {
                /* Replace the top of the queue if needed. */
                if (num_pairs < max_num_pairs) {
                    pairs[num_pairs] = pairs[0];
                    ++(num_pairs);
                }
                pairs[0] = p;
            }
            else if (num_pairs < max_num_pairs) {
                pairs[num_pairs] = p;
                ++(num_pairs);
            }
        }   
    }

    template <BrotligElementTypes T, typename t>
    size_t BrotligHistogram<T, t>::Combine(
        std::vector<BrotligHistogram<T, t>>& out,
        BrotligHistogram<T, t>& temp,
        std::vector<uint32_t>& cluster_size,
        std::vector<uint32_t>& symbols,
        std::vector<uint32_t>& clusters,
        std::vector<BrotligHistogramPair>& pairs,
        size_t num_clusters,
        size_t symbols_size,
        size_t max_clusters,
        size_t max_num_pairs)
    {
        double cost_diff_threshold = 0.0;
        size_t min_cluster_size = 1;
        size_t num_pairs = 0;                                                           
        {
            /* We maintain a vector of histogram pairs, with the property that the pair
               with the maximum bit cost reduction is the first. */
            size_t idx1;
            for (idx1 = 0; idx1 < num_clusters; ++idx1) 
            {
              size_t idx2;
              for (idx2 = idx1 + 1; idx2 < num_clusters; ++idx2) 
              {
                CompareAndPushToQueue(
                    out, 
                    temp, 
                    cluster_size, 
                    clusters[idx1],
                    clusters[idx2], 
                    max_num_pairs, 
                    pairs, 
                    num_pairs);
              }
            }
        }

        while (num_clusters > min_cluster_size) {
            uint32_t best_idx1;
            uint32_t best_idx2;
            size_t i;
            if (pairs[0].cost_diff >= cost_diff_threshold) {
                cost_diff_threshold = 1e99;
                min_cluster_size = max_clusters;
                continue;
            }
            /* Take the best pair from the top of heap. */
            best_idx1 = pairs[0].idx1;
            best_idx2 = pairs[0].idx2;
            out[best_idx1].AddHistogram(out[best_idx2]);    
            out[best_idx1].bit_cost = pairs[0].cost_combo;
            cluster_size[best_idx1] += cluster_size[best_idx2];
            for (i = 0; i < symbols_size; ++i) {
                if (symbols[i] == best_idx2) {
                    symbols[i] = best_idx1;
                }
            }
            for (i = 0; i < num_clusters; ++i) {
                if ((clusters[i] == best_idx2) && ((num_clusters - i - 1) > 0)) {
                    memmove(&clusters[i], &clusters[i + 1],
                    (num_clusters - i - 1) * sizeof(clusters[0]));
                    break;
                }
            }
            --num_clusters;
            {
                /* Remove pairs intersecting the just combined best pair. */
                size_t copy_to_idx = 0;
                for (i = 0; i < num_pairs; ++i) {
                    BrotligHistogramPair* p = &pairs[i];
                    if (p->idx1 == best_idx1 || p->idx2 == best_idx1 ||
                        p->idx1 == best_idx2 || p->idx2 == best_idx2) {
                        /* Remove invalid pair from the queue. */
                        continue;
                    }
                    if (HistogramPairIsLess(&pairs[0], p)) {
                        /* Replace the top of the queue if needed. */
                        BrotligHistogramPair front = pairs[0];
                        pairs[0] = *p;
                        pairs[copy_to_idx] = front;
                    }
                    else {
                        pairs[copy_to_idx] = *p;
                    }
                    ++copy_to_idx;
                }
                num_pairs = copy_to_idx;
            }

            /* Push new pairs formed with the combined histogram to the heap. */
            for (i = 0; i < num_clusters; ++i) {
                CompareAndPushToQueue(
                    out, 
                    temp, 
                    cluster_size, 
                    best_idx1,
                    clusters[i], 
                    max_num_pairs, 
                    pairs, 
                    num_pairs
                );
            }
        }
        return num_clusters;
    }

    template <BrotligElementTypes T, typename t>
    void BrotligHistogram<T, t>::CompareAndPushToQueue(
        std::priority_queue<BrotligHistogramPair, std::vector<BrotligHistogramPair>, BrotligHistogramPairCompare>& pQueue,
        const std::vector<BrotligHistogram<T, t>>& out,
        const std::vector<uint32_t>& cluster_size,
        size_t idx1,
        size_t idx2,
        size_t max_num_pairs
        )
    {
        if (idx1 == idx2) {
            return;
        }

        if (idx2 < idx1) {
            uint32_t t = static_cast<uint32_t>(idx2);
            idx2 = idx1;
            idx1 = t;
        }

        // Prevent addition of duplicates;
        /*std::priority_queue<BrotligHistogramPair, std::vector<BrotligHistogramPair>, BrotligHistogramPairCompare> temp(pQueue);
        while (!temp.empty())
        {
            BrotligHistogramPair tempP = temp.top();
            temp.pop();

            if (tempP.idx1 == idx1 && tempP.idx2 == idx2)
                return;
        }*/

        bool is_good_pair = false;
        BrotligHistogramPair p;
        p.idx1 = p.idx2 = 0;
        p.cost_diff = p.cost_combo = 0;
    
    
        p.idx1 = static_cast<uint32_t>(idx1);
        p.idx2 = static_cast<uint32_t>(idx2);
        p.cost_diff = 0.5 * ClusterCostDiff(cluster_size[idx1], cluster_size[idx2]);
        p.cost_diff -= out[idx1].bit_cost;
        p.cost_diff -= out[idx2].bit_cost;

        if (out[idx1].total_count == 0) {
            p.cost_combo = out[idx2].bit_cost;
            is_good_pair = true;
        }
        else if (out[idx2].total_count == 0) {
            p.cost_combo = out[idx1].bit_cost;
            is_good_pair = true;
        }
        else {
            double threshold = pQueue.size() == 0 ? 1e99 : std::max(0.0, pQueue.top().cost_diff);
            double cost_combo;
            BrotligHistogram<T, t> temp = out[idx1];
            temp.AddHistogram(out[idx2]);
            cost_combo = temp.PopulationCost();
            if (cost_combo < threshold - p.cost_diff) {
                p.cost_combo = cost_combo;
                is_good_pair = true;
            }
        }

        if (is_good_pair) {
            p.cost_diff += p.cost_combo;
            if (pQueue.size() > 0 && HistogramPairIsLess(&pQueue.top(), &p)) {
                /* Replace the top of the queue if needed. */
                if (pQueue.size() < max_num_pairs) {
                    pQueue.push(p);
                }
                else
                {
                    pQueue.pop();
                    pQueue.push(p);
                }
            }
            else if (pQueue.size() < max_num_pairs) {
                pQueue.push(p);
            }
        }
    }

    template <BrotligElementTypes T, typename t>
    size_t BrotligHistogram<T, t>::Combine(
        std::vector<BrotligHistogram<T, t>>& out,
        std::vector<uint32_t>& symbols,
        std::vector<uint32_t>& cluster_size,
        std::set<size_t>& toRemove,
        size_t start_ix,
        size_t end_ix,
        size_t max_clusters,
        size_t max_num_pairs
    )
    {
        size_t num_clusters = end_ix - start_ix + 1;
        // Add all the histogram pairs to the priority queue
        std::priority_queue<BrotligHistogramPair, std::vector<BrotligHistogramPair>, BrotligHistogramPairCompare> pQueue;
        for (size_t ix1 = start_ix; ix1 <= end_ix; ++ix1)
        {
            for (size_t ix2 = start_ix; ix2 <= end_ix; ++ix2)
            {
                if (ix1 != ix2)
                {
                    CompareAndPushToQueue(
                        pQueue,
                        out,
                        cluster_size,
                        ix1,
                        ix2,
                        max_num_pairs
                    );
                }
            }
        }

        size_t min_cluster_size = 1;
        double cost_diff_threshold = 0;
        while ((num_clusters > min_cluster_size)
            && (pQueue.size() > 0))
        {
            BrotligHistogramPair pTop = pQueue.top();
            if (pTop.cost_diff >= cost_diff_threshold) {
                cost_diff_threshold = 1e99;
                min_cluster_size = max_clusters;
                continue;
            }

            pQueue.pop();

            /* Take the best pair from the top of heap and combine the histograms*/
            uint32_t best_idx1 = pTop.idx1;
            uint32_t best_idx2 = pTop.idx2;
            out[best_idx1].AddHistogram(out[best_idx2]);
            out[best_idx1].bit_cost = pTop.cost_combo;
            cluster_size[best_idx1] += cluster_size[best_idx2];
            //cluster_size.erase(cluster_size.begin() + best_idx2);
            //out.erase(out.begin() + best_idx2);
            toRemove.insert(best_idx2);
            for (size_t i = 0; i < symbols.size(); ++i) {
                if (symbols[i] == best_idx2) {
                    symbols[i] = best_idx1;
                }
            }
            --num_clusters;
            // Remove any pairs in the heap that instersects with the best pair
            std::priority_queue<BrotligHistogramPair, std::vector<BrotligHistogramPair>, BrotligHistogramPairCompare> tempQueue;
            while (!pQueue.empty())
            {
                BrotligHistogramPair temp = pQueue.top();
                pQueue.pop();

                if (temp.idx1 == best_idx1 || temp.idx2 == best_idx1 ||
                    temp.idx1 == best_idx2 || temp.idx2 == best_idx2) {
                    /* Remove invalid pair from the queue. */
                    continue;
                }

                tempQueue.push(temp);
            }
        
            pQueue = tempQueue;

            /* Push new pairs formed with the combined histogram to the heap. */
            for (size_t i = start_ix; i < end_ix; ++i) {
                if (i != best_idx1 && toRemove.find(i) == toRemove.end())
                {
                    CompareAndPushToQueue(
                        pQueue,
                        out,
                        cluster_size,
                        best_idx1,
                        i,
                        max_num_pairs
                    );
                }
            }
        }

        return num_clusters;
    }

    //template <BrotligElementTypes T, typename t>
    //void BrotligHistogram<T, t>::ClusterHistograms(
    //    const std::vector<BrotligHistogram<T, t>>& in,
    //    size_t max_histograms,
    //    std::vector<BrotligHistogram<T, t>>& out,
    //    std::vector<uint32_t>& histogram_symbols
    //)
    //{
    //    size_t in_size = in.size();
    //    std::vector<uint32_t> cluster_size(in_size, 1);
    //    std::vector<uint32_t> clusters(in_size);
    //    size_t num_clusters = 0;
    //    const size_t max_input_histograms = 64;
    //    size_t pairs_capacity = max_input_histograms * max_input_histograms / 2;
    //    /* For the first pass of clustering, we allow all pairs. */
    //    std::vector<BrotligHistogramPair> pairs(pairs_capacity + 1);
    //    BrotligHistogram<T, t> temp;
    //
    //    size_t i = 0;
    //
    //    for (i = 0; i < in_size; ++i)
    //    {
    //        BrotligHistogram<T, t> in_i = in.at(i);
    //        BrotligHistogram<T, t> out_i = in_i;
    //        out_i.bit_cost = in_i.PopulationCost();
    //        out[i] = out_i;
    //        histogram_symbols[i] = (uint32_t)i;
    //    }
    //
    //    for (i = 0; i < in_size; i += max_input_histograms)
    //    {
    //        size_t num_to_combine = std::min(in_size - i, max_input_histograms);
    //        size_t num_new_clusters = 0;
    //        size_t j = 0;
    //
    //        std::vector<uint32_t> histogram_symbols_t(num_to_combine, 0);
    //        std::vector<uint32_t> clusters_t(num_to_combine, 0);
    //        for (j = 0; j < num_to_combine; ++j)
    //        {
    //            histogram_symbols_t[j] = histogram_symbols[i + j];
    //            clusters_t[j] = (uint32_t)(i + j);
    //        }
    //
    //        num_new_clusters = BrotligHistogram<T, t>::Combine(
    //            out,
    //            temp,
    //            cluster_size,
    //            histogram_symbols_t,
    //            clusters_t,
    //            pairs,
    //            num_to_combine,
    //            num_to_combine,
    //            max_histograms,
    //            pairs_capacity
    //        );
    //
    //        for (j = 0; j < num_to_combine; ++j)
    //        {
    //            histogram_symbols[i + j] = histogram_symbols_t[j];
    //            clusters[num_clusters + j] = clusters_t[j];
    //        }
    //
    //        num_clusters += num_new_clusters;
    //    }
    //
    //    {
    //        /* For the second pass, we limit the total number of histogram pairs.
    //        After this limit is reached, we only keep searching for the best pair. */
    //        size_t max_num_pairs = std::min(64 * num_clusters, (num_clusters / 2) * num_clusters);
    //        pairs.resize(max_num_pairs + 1);
    //
    //        num_clusters = Combine(
    //            out,
    //            temp,
    //            cluster_size,
    //            histogram_symbols,
    //            clusters,
    //            pairs,
    //            num_clusters,
    //            in_size,
    //            max_histograms,
    //            max_num_pairs
    //        );
    //    }
    //
    //    pairs.clear();
    //    cluster_size.clear();
    //
    //    RemapHistograms(
    //        in,
    //        clusters,
    //        num_clusters,
    //        out,
    //        temp,
    //        histogram_symbols
    //    );
    //
    //    size_t out_size = ReindexHistograms(
    //        out,
    //        histogram_symbols,
    //        in_size
    //    );
    //
    //    out.resize(out_size); // To do: Check here and make sure only the empty elements of out are deleted
    //}

    template <BrotligElementTypes T, typename t>
    void BrotligHistogram<T, t>::ClusterHistograms(
        const std::vector<BrotligHistogram<T, t>>& in,
        size_t max_histograms,
        std::vector<BrotligHistogram<T, t>>& out,
        std::vector<uint32_t>& histogram_symbols
    )
    {
        /*if (in.size() > 1)
        {*/
            size_t in_size = in.size();
            std::vector<uint32_t> cluster_sizes(in_size, 1);
            const size_t max_input_histograms = 64;
            size_t max_num_pairs = max_input_histograms * max_input_histograms / 2;
            for (size_t i = 0; i < in_size; ++i)
            {
                BrotligHistogram<T, t> in_i = in.at(i);
                BrotligHistogram<T, t> out_i = in_i;
                out_i.bit_cost = in_i.PopulationCost();
                out[i] = out_i;
                histogram_symbols[i] = (uint32_t)i;
            }

            size_t start_index = 0;
            size_t toprocess = std::min(out.size(), max_input_histograms);
            size_t end_index = start_index + toprocess - 1;
            std::set<size_t> toRemove_allclusters;
            size_t num_clusters = 0;
            while (start_index < out.size())
            {
                std::set<size_t> toRemove;
                num_clusters += BrotligHistogram<T, t>::Combine(
                    out,
                    histogram_symbols,
                    cluster_sizes,
                    toRemove,
                    start_index,
                    end_index,
                    max_histograms,
                    max_num_pairs
                );

                toRemove_allclusters.insert(toRemove.begin(), toRemove.end());

                start_index = end_index + 1;
                toprocess = std::min(out.size() - start_index, max_input_histograms);
                end_index = start_index + toprocess - 1;
            }

            // Remap histograms
            std::map<size_t, size_t> histogram_map;
            std::vector<BrotligHistogram<T, t>> temp;
            size_t next_hist_id = 0;
            for (size_t index = 0; index < out.size(); ++index)
            {
                if (toRemove_allclusters.find(index) == toRemove_allclusters.end())
                {
                    histogram_map.insert(std::pair<size_t, size_t>(index, next_hist_id++));
                    temp.push_back(out.at(index));
                }
            }

            for (size_t index = 0; index < histogram_symbols.size(); ++index)
            {
                uint32_t oldhistid = histogram_symbols.at(index);
                uint32_t newhistid = (uint32_t)histogram_map.find(static_cast<size_t>(oldhistid))->second;
                histogram_symbols.at(index) = newhistid;
            }

            out.clear();
            //std::fill(out.end(), temp.begin(), temp.end());
            for (size_t index = 0; index < temp.size(); ++index)
            {
                out.push_back(temp.at(index));
            }
            temp.clear();


            cluster_sizes.clear();
            cluster_sizes.resize(out.size(), 1);
            max_num_pairs = std::min(64 * num_clusters, (num_clusters / 2) * num_clusters);
            toRemove_allclusters.clear();
            {
                /* For the second pass, we limit the total number of histogram pairs.
                After this limit is reached, we only keep searching for the best pair. */

                num_clusters = Combine(
                    out,
                    histogram_symbols,
                    cluster_sizes,
                    toRemove_allclusters,
                    0,
                    out.size() - 1,
                    max_histograms,
                    max_num_pairs
                );
            }

            // Remap histograms
            histogram_map.clear();
            next_hist_id = 0;
            for (size_t index = 0; index < out.size(); ++index)
            {
                if (toRemove_allclusters.find(index) == toRemove_allclusters.end())
                {
                    histogram_map.insert(std::pair<size_t, size_t>(index, next_hist_id++));
                    temp.push_back(out.at(index));
                }
            }

            for (size_t index = 0; index < histogram_symbols.size(); ++index)
            {
                uint32_t oldhistid = histogram_symbols.at(index);
                uint32_t newhistid = (uint32_t)histogram_map.find(static_cast<size_t>(oldhistid))->second;
                histogram_symbols.at(index) = newhistid;
            }

            out.clear();
            for (size_t index = 0; index < temp.size(); ++index)
            {
                out.push_back(temp.at(index));
            }

            temp.clear();
            toRemove_allclusters.clear();
            cluster_sizes.clear();
        /*}
        else
        {
            out.clear();
            out.push_back(in.at(0));
            histogram_symbols.clear();
            histogram_symbols.push_back(1);
        }*/
    }

    template <BrotligElementTypes T, typename t>
    void BrotligHistogram<T, t>::RemapHistograms(
        const std::vector<BrotligHistogram<T, t>>& in,
        const std::vector<uint32_t> clusters,
        size_t num_clusters,
        std::vector<BrotligHistogram<T, t>>& out,
        BrotligHistogram<T, t>& temp,
        std::vector<uint32_t>& symbols
    )
    {
        size_t in_size = in.size();
        size_t i = 0;
        for (i = 0; i < in_size; ++i)
        {
            uint32_t best_out = (i == 0) ? symbols[0] : symbols[i - 1];
            double best_bits = BitCostDistance(in[i], out[best_out], temp);
            size_t j = 0;
            for (j = 0; j < num_clusters; ++j)
            {
                const double cur_bits = BitCostDistance(in[i], out[clusters[j]], temp);
                if (cur_bits < best_bits)
                {
                    best_bits = cur_bits;
                    best_out = clusters[j];
                }
            }

            symbols[i] = best_out;
        }

        /* Recompute each out based on raw and symbols. */
        for (i = 0; i < num_clusters; ++i)
        {
            out[clusters[i]].Clear();
        }
        for (i = 0; i < in_size; ++i)
        {
            out[symbols[i]].AddHistogram(in[i]);
        }
    }

    template <BrotligElementTypes T, typename t>
    size_t BrotligHistogram<T, t>::ReindexHistograms(
        std::vector<BrotligHistogram<T, t>>& out,
        std::vector<uint32_t>& symbols,
        size_t length
    )
    {
        static const uint32_t kInvalidIndex = BROTLI_UINT32_MAX;
        std::vector<uint32_t> new_index(length);
        uint32_t next_index = 0;
        std::vector<BrotligHistogram<T, t>> temp;
        size_t i = 0;
        for (i = 0; i < length; ++i) {
            new_index[i] = kInvalidIndex;
        }

        for (i = 0; i < length; ++i) {
            if (new_index[symbols[i]] == kInvalidIndex) {
                new_index[symbols[i]] = next_index;
                ++next_index;
            }
        }

        /* TODO: by using idea of "cycle-sort" we can avoid allocation of
         tmp and reduce the number of copying by the factor of 2. */
        temp.resize(next_index);
        next_index = 0;
        for (i = 0; i < length; ++i)
        {
            if (new_index[symbols[i]] == next_index)
            {
                temp[next_index] = out[symbols[i]];
                ++next_index;
            }

            symbols[i] = new_index[symbols[i]];
        }

        new_index.clear();
        for (i = 0; i < next_index; ++i)
        {
            out[i] = temp[i];
        }

        temp.clear();

        return next_index;
    }
}
