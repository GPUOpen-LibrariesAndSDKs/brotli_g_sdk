// Brotli-G SDK 1.1
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

extern "C" {
#include "brotli/c/enc/entropy_encode.h"
}

#include "common/BrotligConstants.h"
#include "common/BrotligUtils.h"

#include "BrotligHuffman.h"

using namespace BrotliG;

struct BrotligHuffmanNode
{
    uint16_t symbol;
    uint32_t code;
    uint8_t depth;
    uint32_t total_count;
    BrotligHuffmanNode* left;
    BrotligHuffmanNode* right;

    BrotligHuffmanNode()
    {
        symbol = 0;
        code = 0;
        depth = 0;
        total_count = 0;
        left = nullptr;
        right = nullptr;
    }

    ~BrotligHuffmanNode()
    {
        symbol = 0;
        code = 0;
        depth = 0;
        total_count = 0;
        left = nullptr;
        right = nullptr;
    }
};

typedef BrotligHuffmanNode* BrotligHuffmanNodePtr;

class compare
{
public:
    bool operator() (const BrotligHuffmanNodePtr& c1, const BrotligHuffmanNodePtr& c2) const
    {
        if (c1->total_count != c2->total_count)
            return c1->total_count > c2->total_count;
        else
            return c1->symbol < c2->symbol;
    }
};

static bool TraverseTree(BrotligHuffmanNodePtr pNode, uint8_t depth)
{
    if (depth > BROTLIG_HUFFMAN_MAX_DEPTH)
        return false;

    pNode->depth = depth;

    bool ret = true;
    if (pNode->left) ret &= TraverseTree(pNode->left, depth + 1);
    if (pNode->right) ret &= TraverseTree(pNode->right, depth + 1);

    return ret;
}

static void DeleteTree(BrotligHuffmanNodePtr pNode)
{
    if (pNode == nullptr) return;

    DeleteTree(pNode->left);
    DeleteTree(pNode->right);

    delete pNode;
}

void BuildHuffman(uint32_t* hist, size_t alphabet_size, uint16_t codes[], uint8_t codelens[])
{
    size_t i = 0;
    BrotligHuffmanNodePtr temp = nullptr, root = nullptr, temp2 = nullptr;
    std::priority_queue<BrotligHuffmanNodePtr, std::vector<BrotligHuffmanNodePtr>, compare> pqueue;
    std::vector<BrotligHuffmanNodePtr> nodeList;

    for (uint32_t count_limit = 1;; count_limit *= 2)
    {
        // add counts to a priority queue
        for (i = 0; i < alphabet_size; ++i)
        {
            if (hist[i])
            {
                temp = new BrotligHuffmanNode;
                temp->symbol = (uint16_t)i;
                temp->total_count = hist[i];

                nodeList.push_back(temp);
                pqueue.push(temp);
            }
        }

        // create a huffman tree
        if (pqueue.size() > 1)
        {
            while (pqueue.size() > 1)
            {
                temp = pqueue.top();
                pqueue.pop();

                temp2 = pqueue.top();
                pqueue.pop();

                root = new BrotligHuffmanNode;
                root->total_count = temp->total_count + temp2->total_count;
                root->left = temp;
                root->right = temp2;

                pqueue.push(root);
            }
        }
        else
        {
            root = pqueue.top();
            pqueue.pop();
        }

        // compute depths
        if (TraverseTree(root, 0)) break;

        DeleteTree(root);
        while (!pqueue.empty()) pqueue.pop();
        nodeList.clear();
        // reshistogram
        for (i = 0; i < alphabet_size; ++i) if (hist[i]) hist[i] = std::max(hist[i], count_limit);
    }

    // compute codes
    uint16_t blCounts[BROTLIG_HUFFMAN_NUM_CODE_LENGTH] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    uint16_t next_code[BROTLIG_HUFFMAN_NUM_CODE_LENGTH] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };

    // Count the number for codes for each length
    for each (BrotligHuffmanNodePtr node in nodeList) ++blCounts[node->depth];

    // Find the numerical values of the smallest code for each code length
    uint16_t code = 0;
    blCounts[0] = 0;
    for (uint16_t bits = 1; bits < BROTLIG_HUFFMAN_NUM_CODE_LENGTH; ++bits)
        next_code[bits] = (next_code[bits - 1] + blCounts[bits - 1]) << 1;

    // Assign numerical values to all codes using consecutive values for all codes of the same length
    // with the base values determined by the previous step.
    for each (BrotligHuffmanNodePtr node in nodeList)
    {
        codelens[node->symbol] = node->depth;
        codes[node->symbol] = BrotligReverseBits(node->depth, next_code[node->depth]++);
    }

    DeleteTree(root);
    while (!pqueue.empty()) pqueue.pop();
    nodeList.clear();
}

static void StoreComplexHuffman(uint16_t codes[], uint8_t codelens[], size_t alphabet_size, BrotligSwizzler& writer)
{
    bool use_rle_for_non_zeros = true;
    bool use_rle_for_zeros = true;

    uint8_t rle_codes[BROLTIG_NUM_COMMAND_SYMBOLS_EFFECTIVE], rle_extra_bits[BROLTIG_NUM_COMMAND_SYMBOLS_EFFECTIVE];
    size_t num_rle_codes = 0, num_rle_extra_bits = 0;

    ComputeRLECodes(
        alphabet_size, 
        codelens, 
        rle_codes, 
        num_rle_codes, 
        rle_extra_bits, 
        num_rle_extra_bits
    );

    // Compute rle code statistics
    size_t i = 0;
    uint32_t rle_hist[BROTLI_CODE_LENGTH_CODES];
    memset(rle_hist, 0, sizeof(rle_hist));
    for (i = 0; i < num_rle_codes; ++i) ++rle_hist[rle_codes[i]];

    // Build rle code tree
    uint16_t rle_huffman_codes[BROTLI_CODE_LENGTH_CODES];
    uint8_t rle_huffman_codelens[BROTLI_CODE_LENGTH_CODES];

    memset(rle_huffman_codes, 0, sizeof(rle_huffman_codes));
    memset(rle_huffman_codelens, 0, sizeof(rle_huffman_codelens));

    BuildHuffman(rle_hist, BROTLI_CODE_LENGTH_CODES, rle_huffman_codes, rle_huffman_codelens);

    // Store rle huffman tree
    static const uint8_t kStorageOrder[BROTLI_CODE_LENGTH_CODES] = {
        1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };

    for (i = 0; i < BROTLI_CODE_LENGTH_CODES; ++i)
    {
        uint8_t order = kStorageOrder[i];
        assert(rle_huffman_codes[order] <= 511);
        assert(rle_huffman_codelens[order] < 10);

        writer.Append(5, rle_huffman_codelens[order], true);
    }

    writer.BSReset();

    // Store encoded complex huffman tree
    for (i = 0; i < num_rle_codes; ++i)
    {
        uint8_t ix = rle_codes[i];
        assert(rle_huffman_codelens[ix] <= 9);
        writer.Append(rle_huffman_codelens[ix], rle_huffman_codes[ix]);

        switch (ix)
        {
        case BROTLI_REPEAT_PREVIOUS_CODE_LENGTH:
            writer.Append(2, rle_extra_bits[i], true);
            break;
        case BROTLI_REPEAT_ZERO_CODE_LENGTH:
            writer.Append(3, rle_extra_bits[i], true);
            break;
        default:
            writer.BSSwitch();
        }
    }
}

void BrotliG::BuildStoreHuffmanTable(
    uint32_t* hist, 
    size_t alphabet_size, 
    BrotligSwizzler& writer, 
    uint16_t codes[], 
    uint8_t codelens[])
{
    memset(codes, 0, sizeof(uint16_t) * alphabet_size);
    memset(codelens, 0, sizeof(uint8_t) * alphabet_size);
    
    size_t count = 0;
    size_t s4[4] = { 0 };
    for (size_t i = 0; i < alphabet_size; ++i)
    {
        if (hist[i])
        {
            if (count < 4) s4[count] = i;
            else if (count > 4) break;
            ++count;
        }
    }

    size_t max_bits = 0;
    {
        size_t max_bits_counter = alphabet_size - 1;
        while (max_bits_counter) {
            max_bits_counter >>= 1;
            ++max_bits;
        }
    }

    if (count <= 1)
    {
        /*Trivial tree header*/
        writer.Append(2, 0);                                            // tree type
        writer.Append(2, 1);                                            // msym = 1
        writer.Append(2, 0);                                            // extra header bit to match size

        writer.Append((uint32_t)max_bits, (uint32_t)s4[0], true);
        writer.BSReset();

        codes[s4[0]] = 0;
        codelens[s4[0]] = 0;

        writer.BSReset();
        return;
    }
    
    memset(codelens, 0, sizeof(codelens));
    memset(codes, 0, sizeof(codes));

    BuildHuffman(hist, alphabet_size, codes, codelens);

    if (count <= 4) {
        /*Simple tree header*/
        writer.Append(2, 1);                                            // tree type
        writer.Append(2, (uint32_t)count - 1);                          // nsym - 1

        {
            /* sort */
            for (size_t i = 0; i < count; ++i)
                for (size_t j = i + 1; j < count; ++j)
                    if ((codelens[s4[j]] < codelens[s4[i]]) || 
                        (codelens[s4[j]] == codelens[s4[i]] && s4[j] < s4[i]))
                        BROTLI_SWAP(size_t, s4, j, i);
        }

        switch (count)
        {
        case 2:
            writer.Append(2, 0);                                        // extra header bits
            writer.Append((uint32_t)max_bits, (uint32_t)s4[0], true);
            writer.Append((uint32_t)max_bits, (uint32_t)s4[1], true);
            break;
        case 3:
            writer.Append(2, 0);                                        // extra header bits
            writer.Append((uint32_t)max_bits, (uint32_t)s4[0], true);
            writer.Append((uint32_t)max_bits, (uint32_t)s4[1], true);
            writer.Append((uint32_t)max_bits, (uint32_t)s4[2], true);
            break;
        case 4:                                                     
            writer.Append(1, (uint32_t)codelens[s4[0]] == 1 ? 1 : 0);   // tree select
            writer.Append(1, 0);                                        // extra header bits
            writer.Append((uint32_t)max_bits, (uint32_t)s4[0], true);
            writer.Append((uint32_t)max_bits, (uint32_t)s4[1], true);
            writer.Append((uint32_t)max_bits, (uint32_t)s4[2], true);
            writer.Append((uint32_t)max_bits, (uint32_t)s4[3], true);
            break;
        default:
            throw std::exception("Incorrect num symbols for simple huffman tree.");
        }

        writer.BSReset();
    } else {
        /*Complex tree header*/
        writer.Append(2, 2);                                            // tree type
        writer.Append(4, BROTLI_CODE_LENGTH_CODES - 4);                 // nsym - 4

        StoreComplexHuffman(codes, codelens, alphabet_size, writer);

        writer.BSReset();
    }
}