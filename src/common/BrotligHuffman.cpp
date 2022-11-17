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
#include <cmath>
#include <set>
#include <iostream>

extern "C" {
#include "brotli/c/enc/entropy_encode.h"
}

#include "BrotligHuffman.h"
#include "BrotligConstants.h"
#include "BrotligUtils.h"

using namespace BrotliG;

#define BG_DEBUG 0
#define BG_DEBUG_PRINT_TRIVIAL 0
#define BG_DEBUG_PRINT_SIMPLE 0
#define BG_DEBUG_PRINT_COMPLEX 0
#define BG_DEBUG_PRINT_COMPLEX_RLE 0
#define BG_DEBUG_PRINT_COMPLEX_RLE_SYMS 0
#define BG_DEBUG_PRINT_COMPLEX_RLE_LENS 0

BrotligHuffmanTree::BrotligHuffmanTree()
{
    m_root = nullptr;
    maxcodebitsize = 0;
    mincodebitsize = 0;
    m_mostFreqSymbol = 0;
}

BrotligHuffmanTree::~BrotligHuffmanTree()
{
    Cleanup();
}

bool BrotligHuffmanTree::Build(
    const std::vector<uint32_t>& histogram
)
{
    std::vector<uint32_t> temp(histogram);
    size_t count = 0;
    size_t s4[4] = { 0 };
    size_t i = 0;
    size_t max_bits = 0;
    for (i = 0; i < temp.size(); ++i)
    {
        if (temp.at(i))
        {
            if (count < 4)
                s4[count] = i;
            else if (count < 4)
                break;
            count++;
        }
    }

    if (count <= 1)
    {
        BrotligHuffmanNodePtr node = new BrotligHuffmanNode;
        node->value = static_cast<uint16_t>(s4[0]);
        node->code = 0;
        node->depth = 1;
        node->left = nullptr;
        node->right = nullptr;
        node->total_count = temp.at(s4[0]);
        node->bitsize = 1;

        m_root = node;
        m_symbolmap.insert(std::pair<uint16_t, BrotligHuffmanNodePtr>(node->value, node));
        m_treenodes.push_back(node);

        m_mostFreqSymbol = node->value;
    }
    else
    {
        /*More than 1 iterations are required in cases where actual max tree depth exceeds the min depth of 15.
        In such cases, decrease entropy by re-binning the smallest frequencies.*/
        for (uint32_t count_limit = 1;; count_limit *= 2)
        {
            std::vector<uint32_t> reshistogram(temp.size(), 0);
            for (size_t i = 0; i < temp.size(); ++i)
            {
                if (temp.at(i) != 0)
                {
                    uint32_t count = std::max(temp.at(i), count_limit);
                    reshistogram.at(i) = count;
                }
            }

            if (!CreatePriorityQueue(reshistogram))
                return false;
            CreateHuffmanTree();
            if (ComputeDepth())
                break;
            Cleanup();
        }
        ComputeCodes();
    }

    return true;
}

void BrotligHuffmanTree::Store(BrotligSwizzler* sw, size_t alphabet_size, bool debug)
{
    size_t histogram_length = m_treenodes.size();
    
    size_t count = 0;
    size_t s4[4] = { 0 };
    size_t i = 0;
    size_t max_bits = 0;
    for (i = 0; i < histogram_length; ++i)
    {
        if (m_treenodes.at(i)->total_count)
        {
            if (count < 4)
                s4[count] = m_treenodes.at(i)->value;
            else if (count < 4)
                break;
            count++;
        }
    }

    {
        size_t max_bits_counter = alphabet_size - 1;
        while (max_bits_counter) {
            max_bits_counter >>= 1;
            ++max_bits;
        }
    }

    if (count <= 1)
    {
        /*Trivial tree header: Size 5 bits*/
        sw->Append(2, 0);                                       // tree type                    
        sw->Append(2, 1);                                       // nsym = 1
        sw->Append(1, 0);                                       // extra header bit to match size
        sw->Append(1, 0);
        /*Trivial tree header end*/

        sw->Append(static_cast<uint32_t>(max_bits), static_cast<uint32_t>(s4[0]), true);
        assert((m_symbolmap.find(static_cast<uint32_t>(s4[0])) != m_symbolmap.end())
            && (m_symbolmap.find(static_cast<uint32_t>(s4[0]))->second->code == 0));

        sw->Reset();
    }
    else if(count <= 4)
    {
        StoreSimpleTree(sw, count, max_bits);
    }
    else
    {
        StoreComplexTree(sw, alphabet_size, max_bits, debug);
    }
}

void BrotligHuffmanTree::StoreHufftreeOfHufftree(
    BrotligSwizzler* sw, 
    const int num_codes,
    size_t codesize,
    bool debug)
{
    static const uint8_t kStorageOrder[BROTLI_CODE_LENGTH_CODES] = {
   1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };

    /* The bit lengths of the Huffman code over the code length alphabet
     are compressed with the following static Huffman code:
       Symbol   Code
       ------   ----
       0          00
       1        1110
       2         110
       3          01
       4          10
       5        1111 */

    static const uint8_t kHuffmanBitLengthHuffmanCodeSymbols[6] = {
     0, 7, 3, 2, 1, 15
    };
    static const uint8_t kHuffmanBitLengthHuffmanCodeBitLengths[6] = {
      2, 4, 3, 2, 2, 4
    };

    std::sort(m_treenodes.begin(), m_treenodes.end(), [](BrotligHuffmanNodePtr a, BrotligHuffmanNodePtr b)
    {
            return a->value < b->value;
    });
    

    /*Throw away trailing zeros*/
    size_t codes_to_store = BROTLI_CODE_LENGTH_CODES;
    size_t skip_some = 0;

    /*Complex tree header end*/
    {
        size_t i = 0;
        for (i = skip_some; i < codes_to_store; ++i)
        {
            uint8_t order = kStorageOrder[i];
            size_t l = m_treenodes[order]->depth;
            assert(l < 10);

            sw->Append(
                5, 
                static_cast<uint32_t>(l),
                true
            );

            /*sw->Append(
                kHuffmanBitLengthHuffmanCodeBitLengths[l],
                kHuffmanBitLengthHuffmanCodeSymbols[l],
                true
            );*/
        }

        sw->Reset();
    }
}

BrotligHuffmanTree* BrotligHuffmanTree::Load(BrotligDeswizzler* deswizzler, size_t alphabet_size, bool debug)
{
    BrotligHuffmanTree* tree = new BrotligHuffmanTree;
    tree->LoadTree(deswizzler, alphabet_size, debug);
    return tree;
}

void BrotligHuffmanTree::LoadTree(BrotligDeswizzler* deswizzler, size_t alphabet_size, bool debug)
{
    uint32_t ttype = deswizzler->ReadAndConsume(2);
    uint32_t num_symbols = 0;
    size_t max_bits = 0;
    {
        size_t max_bits_counter = alphabet_size - 1;
        while (max_bits_counter) {
            max_bits_counter >>= 1;
            ++max_bits;
        }
    }

    switch (ttype)
    {   
    case 0:
    {
        num_symbols = deswizzler->ReadAndConsume(2);
        deswizzler->Consume(2);
        LoadTrivialTree(deswizzler, num_symbols, max_bits);
        break;
    }
        
    case 1:
    {
        num_symbols = deswizzler->ReadAndConsume(2) + 1;
        uint32_t tree_select = deswizzler->ReadAndConsume(1);
        deswizzler->Consume(1);
        LoadSimpleTree(deswizzler, num_symbols, (tree_select == 1), max_bits);
        break;
    }
        
    case 2:
    {
        uint32_t num_len_symbols = deswizzler->ReadAndConsume(4) + 4;
        LoadComplexTree(deswizzler, num_len_symbols, alphabet_size, max_bits, debug);
        break;
    }
     
    default:
        throw std::exception("Error loading huffman table. Incorrect tree type.");
    }
}

uint16_t BrotligHuffmanTree::Code(uint16_t symbol)
{
    return m_symbolmap.find(symbol)->second->code;
}

uint16_t BrotligHuffmanTree::Revcode(uint16_t symbol)
{
    return m_symbolmap.find(symbol)->second->revcode;
}

size_t BrotligHuffmanTree::Bitsize(uint16_t symbol)
{
    return m_symbolmap.find(symbol)->second->bitsize;
}

BrotligHuffmanNodePtr BrotligHuffmanTree::Symbol(uint16_t code)
{
    return m_symbol_table[code];
}

uint16_t BrotligHuffmanTree::Symbol(uint16_t code, size_t& codelen)
{
    codelen = m_symbol_table[code]->bitsize;
    return m_symbol_table[code]->value;
}

bool BrotligHuffmanTree::Has(uint16_t code)
{
    return (code < 65536 && m_symbol_table[code] != nullptr);
}

void BrotligHuffmanTree::SetMinCodeSize(size_t size)
{
    mincodebitsize = size;
}

size_t BrotligHuffmanTree::MinCodeSize()
{
    return mincodebitsize;
}

uint16_t BrotligHuffmanTree::MostFreqSymbol()
{
    return m_mostFreqSymbol;
}

void BrotligHuffmanTree::Display(std::string name, std::ostream& stream)
{
    stream << "Tree name = " << name << ": \n";
    if (m_treenodes.size() > 0)
    {
        std::vector<BrotligHuffmanNodePtr> temp(m_treenodes);

        std::sort(temp.begin(), temp.end(), [](BrotligHuffmanNodePtr a, BrotligHuffmanNodePtr b) {
            if (a->depth == b->depth)
                return a->value < b->value;
            else
                return a->depth < b->depth;
        });
        
        for each (BrotligHuffmanNodePtr n in temp)
        {
            if(n->depth != 0)
                stream << n->value << "\n";
        }
        
    }
    stream << "\n\n";
}

bool BrotligHuffmanTree::CreatePriorityQueue(
    const std::vector<uint32_t>& histogram)
{
    assert(histogram.size() <= 65536);
    size_t highestFreq = 0;
    m_mostFreqSymbol = 0;
    for(size_t i = 0;i < histogram.size(); ++i)
    {
        BrotligHuffmanNodePtr newNode = new BrotligHuffmanNode();
        newNode->value = (uint16_t)i;
        newNode->total_count = histogram.at(i);

        if (newNode->total_count > highestFreq)
        {
            m_mostFreqSymbol = newNode->value;
            highestFreq = newNode->total_count;
        }

        if (newNode->total_count == highestFreq && newNode->value < m_mostFreqSymbol)
        {
            m_mostFreqSymbol = newNode->value;
        }

        m_symbolmap.insert(std::pair<uint16_t, BrotligHuffmanNodePtr>(newNode->value, newNode));
    }

    // Add all the nodes with total_count > 0 in the node map to Priority Queue
    std::map<uint16_t, BrotligHuffmanNodePtr>::iterator itr2 = m_symbolmap.begin();
    while (itr2 != m_symbolmap.end())
    {
        if (itr2->second->total_count != 0)
        {
            m_pQueue.push(itr2->second);
        }

        itr2++;
    }

    if (m_pQueue.size() == 0)
        return false;
    else
        return true;
}

void BrotligHuffmanTree::CreateHuffmanTree()
{
    std::priority_queue<BrotligHuffmanNodePtr, std::vector<BrotligHuffmanNodePtr>, compare> temp(m_pQueue);
    if (temp.size() > 1)
    {
        BrotligHuffmanNodePtr tempNode1 = nullptr, tempNode2 = nullptr;
        while (temp.size() > 1)
        {
            tempNode1 = temp.top();
            temp.pop();

            tempNode2 = temp.top();
            temp.pop();

            m_root = new BrotligHuffmanNode();
            m_root->total_count = tempNode1->total_count + tempNode2->total_count;
            m_root->left = tempNode1;
            m_root->right = tempNode2;

            temp.push(m_root);
        }
    }
    else
    {
        m_root = temp.top();
        temp.pop();
    }
}

bool BrotligHuffmanTree::ComputeDepth()
{
     return TraverseTree(m_root, "", 0);
}

void BrotligHuffmanTree::ComputeCodes(bool forDecoder)
{
    // Compute Canonical Huffman codes
     
    // Create an array of symbols sorted based on first depth and then the symbol value in lexiographical order
    std::transform(
        m_symbolmap.begin(),
        m_symbolmap.end(),
        std::back_inserter(m_treenodes),
        [](std::pair<const uint16_t, BrotliG::BrotligHuffmanNodePtr>& pair) { return pair.second; });

    // Count the number of codes for each code length
    std::vector<uint16_t> bl_count(BROTLIG_MAX_HUFFMAN_BITS, 0);
    for each (BrotligHuffmanNodePtr treenode in m_treenodes)
    {
        bl_count[treenode->depth]++;
    }

    // Find the numerical values of the smallest code for each code length
    uint16_t code = 0;
    bl_count[0] = 0;
    std::vector<uint16_t> next_code(BROTLIG_MAX_HUFFMAN_BITS, 0);
    for (size_t bits = 1; bits < BROTLIG_MAX_HUFFMAN_BITS; bits++)
    {
       code = (code + bl_count[bits - 1]) << 1;
       next_code[bits] = code;
    }

    // Assign numerical values to all codes using consecutive values for all codes of the same length
    // with base values determined by the previous step.
    // Codes that are never used (bit length of zero) are not assigned a value
    std::vector<BrotligHuffmanNodePtr>::iterator litr = m_treenodes.begin();
    uint8_t depth = 0;
    while (litr != m_treenodes.end())
    {
        depth = (*litr)->depth;
        if (depth != 0)
        {
            uint16_t code = next_code[depth];
            (*litr)->code = code;
            (*litr)->revcode = BrotligReverseBits(depth, code);
            (*litr)->bitsize = depth;
            next_code[depth]++;
        }

        ++litr;
    }

    if (forDecoder)
    {
        SetupForDecoding();
    }
    else
    {
        std::sort(m_treenodes.begin(), m_treenodes.end(), [](BrotligHuffmanNodePtr a, BrotligHuffmanNodePtr b) {
            return a->value < b->value;
            });
    }    
}

void BrotligHuffmanTree::SetupForDecoding()
{
    std::sort(m_treenodes.begin(), m_treenodes.end(), [](BrotligHuffmanNodePtr& a, BrotligHuffmanNodePtr& b) {
        if (a->bitsize == b->bitsize)
            return a->code < b->code;
        else
            return a->bitsize < b->bitsize;
    });

    m_symbol_table.resize(65536, nullptr);
    for (size_t i = 0; i < m_treenodes.size(); ++i)
    {
        BrotligHuffmanNodePtr node = m_treenodes.at(i);
        size_t bitsize = node->bitsize;
        if (bitsize == 0)
            continue;
        uint16_t code = node->code;
        uint16_t value = node->value;
        size_t leftbits = 16 - bitsize;
        code <<= leftbits;
        uint16_t max_add = (uint16_t(1) << leftbits) - uint16_t(1);
        uint16_t toAdd = 0;

        uint16_t startIndex = code | toAdd;
        uint16_t endIndex = code | max_add;
        size_t count = (endIndex - startIndex + 1);
        std::fill_n(m_symbol_table.begin() + startIndex, count, node);
    }
}

void BrotligHuffmanTree::Cleanup()
{
    while (!m_pQueue.empty())
    {
        m_pQueue.pop();
    }

    for (std::map<uint16_t, BrotligHuffmanNodePtr>::iterator itr = m_symbolmap.begin(); itr != m_symbolmap.end(); ++itr)
    {
        delete itr->second;
        itr->second = nullptr;
    }
    m_symbolmap.clear();
    m_root = nullptr;
    m_treenodes.clear();
    m_symbol_table.clear();

    maxcodebitsize = 0;
    mincodebitsize = 0;
}

bool BrotligHuffmanTree::TraverseTree(
    BrotligHuffmanNodePtr pNode,
    std::string code,
    uint8_t d)
{
    if (d > BROTLIG_MAX_HUFFMAN_DEPTH)
        return false;

    pNode->s_code = code;
    pNode->depth = d;

    bool ret = true;
    if (pNode->left != NULL)
    {
        ret &= TraverseTree(pNode->left, code + '0', d + 1);
    }

    if (pNode->right != NULL)
    {
        ret &= TraverseTree(pNode->right, code + '1', d + 1);
    }

    return ret;
}

uint16_t BrotligHuffmanTree::BinaryToDecimal(std::string& bString)
{
    uint16_t result = 0;
    for (int i = 0; i < bString.size(); ++i)
    {
        result = result * 2 + bString[i] - '0';
    }

    return result;
}

void BrotligHuffmanTree::StoreSimpleTree(BrotligSwizzler* sw, size_t num_symbols, size_t max_bits)
{
    std::vector<BrotligHuffmanNodePtr> temp(m_treenodes);
    temp.erase(std::remove_if(temp.begin(), temp.end(), [](BrotligHuffmanNodePtr a) {
        return (a->total_count == 0);
    }), temp.end());
    
    std::sort(temp.begin(), temp.end(), [](BrotligHuffmanNodePtr a, BrotligHuffmanNodePtr b) {
        if (a->depth == b->depth)
            return a->value < b->value;
        else
            return a->depth < b->depth;
    });
    
    /*Simple tree header start: Size 5 bits*/
    sw->Append(2, 1);                                       // tree type                    
    sw->Append(2, static_cast<uint32_t>(num_symbols) - 1);                         // nsym - 1
    sw->Append(1, (temp[0]->depth == 1) ? 1 : 0);           // tree select (used only when nsym == 4)
    sw->Append(1, 0);
    /*Simple tree header end*/

    if (num_symbols == 2)
    {
        assert(temp[0]->code == 0);
        sw->Append(static_cast<uint32_t>(max_bits), static_cast<uint32_t>(temp[0]->value), true);
        sw->Append(static_cast<uint32_t>(max_bits), static_cast<uint32_t>(temp[1]->value), true);
    }
    else if (num_symbols == 3)
    {
        assert(temp[0]->code == 0);
        sw->Append(static_cast<uint32_t>(max_bits), static_cast<uint32_t>(temp[0]->value), true);
        sw->Append(static_cast<uint32_t>(max_bits), static_cast<uint32_t>(temp[1]->value), true);
        sw->Append(static_cast<uint32_t>(max_bits), static_cast<uint32_t>(temp[2]->value), true);
    }
    else
    {
        assert(temp[0]->code == 0);
        sw->Append(static_cast<uint32_t>(max_bits), static_cast<uint32_t>(temp[0]->value), true);
        sw->Append(static_cast<uint32_t>(max_bits), static_cast<uint32_t>(temp[1]->value), true);
        sw->Append(static_cast<uint32_t>(max_bits), static_cast<uint32_t>(temp[2]->value), true);
        sw->Append(static_cast<uint32_t>(max_bits), static_cast<uint32_t>(temp[3]->value), true);
    }

    sw->Reset();
}

void BrotligHuffmanTree::StoreComplexTree(
    BrotligSwizzler* sw, 
    size_t alphabet_size, 
    size_t max_bits,
    bool debug)
{
    size_t num_symbols = m_treenodes.size();

    std::sort(m_treenodes.begin(), m_treenodes.end(), [](BrotligHuffmanNodePtr& a, BrotligHuffmanNodePtr& b) {
            return a->value < b->value;
    });

    bool use_rle_for_non_zeros = true;
    bool use_rle_for_zeros = true;

    // Actual RLE encoding
    std::vector<uint8_t> rle_codes, rle_extra_bits;
    uint8_t prev_value = BROTLI_INITIAL_REPEATED_CODE_LENGTH;
    for (size_t i = 0; i < m_treenodes.size();)
    {
        const uint8_t value = m_treenodes.at(i)->depth;
        size_t reps = 1;
        if (i == 0)                         
        {
            rle_codes.push_back(value);
            rle_extra_bits.push_back(0);
            prev_value = value;
        }
        else
        {
            if ((value != 0 && use_rle_for_non_zeros) ||
                (value == 0 && use_rle_for_zeros))
            {
                size_t k = 0;
                for (k = i + 1; k < m_treenodes.size() && m_treenodes.at(k)->depth == value; ++k)
                {
                    ++reps;
                }
            }

            if (value == 0)
            {
                ComputeRLEZerosReps(
                    reps,
                    rle_codes,
                    rle_extra_bits
                );
            }
            else
            {
                ComputeRLEReps(
                    prev_value,
                    value,
                    reps,
                    rle_codes,
                    rle_extra_bits
                );
            }
            prev_value = value;
        }
        i += reps;
    }

    // Calculate statistics of the rle codes
    std::vector<uint32_t> rle_histo(BROTLI_CODE_LENGTH_CODES, 0);
    for (size_t i = 0; i < rle_codes.size(); ++i)
    {
        ++rle_histo.at(rle_codes.at(i));
    }

    int num_codes = 0;
    size_t code = 0;
    for (size_t i = 0; i < BROTLIG_MAX_HUFFMAN_BITS; ++i)
    {
        if (rle_histo[i])
        {
            if (num_codes == 0)
            {
                code = i;
                num_codes = 1;
            }
            else if (num_codes == 1)
            {
                num_codes = 2;
                break;
            }
        }
    }

    BrotligHuffmanTree* tree = new BrotligHuffmanTree;
    tree->Build(rle_histo);
    auto it = std::max_element(
        m_treenodes.begin(),
        m_treenodes.end(),
        [](const BrotligHuffmanNodePtr& a, const BrotligHuffmanNodePtr& b) { return a->bitsize < b->bitsize; });

    /*Complex tree header start: Size 5 bits*/
    sw->Append(2, 2);               // tree type
    // RLE max value
    sw->Append(4, BROTLI_CODE_LENGTH_CODES - 4);

    tree->StoreHufftreeOfHufftree(sw, num_codes, (*it)->bitsize, debug);

    for (size_t i = 0; i < rle_codes.size(); ++i)
    {        
        uint16_t ix = static_cast<uint16_t>(rle_codes[i]);

        sw->Append(static_cast<uint32_t>(tree->Bitsize(ix)), static_cast<uint32_t>(tree->Revcode(ix)));

        switch (ix)
        {
        case BROTLI_REPEAT_PREVIOUS_CODE_LENGTH:
            sw->Append(2, static_cast<uint32_t>(rle_extra_bits[i]), true);
            break;
        case BROTLI_REPEAT_ZERO_CODE_LENGTH:
            sw->Append(3, static_cast<uint32_t>(rle_extra_bits[i]), true);
            break;
        default:
            sw->BSSwitch();
            break;
        }
    }

    sw->Reset();

    delete tree;
}

void BrotligHuffmanTree::CheckRLEUse(
    std::vector<BrotligHuffmanNodePtr>& treenodes,
    bool& use_rle_for_non_zeros,
    bool& use_rle_for_zeros)
{
    size_t total_reps_zero = 0;
    size_t total_reps_non_zero = 0;
    size_t count_reps_zero = 1;
    size_t count_reps_non_zero = 1;
    size_t i;
    for (i = 0; i < treenodes.size();)
    {
        const uint8_t value = treenodes.at(i)->depth;
        size_t reps = 1;
        size_t k = 0;
        for (k = i + 1; k < treenodes.size() && treenodes.at(k)->depth == value; ++k)
        {
            ++reps;
        }

        if (reps >= 3 && value == 0)
        {
            total_reps_zero += reps;
            ++count_reps_zero;
        }
        if (reps >= 4 && value != 0)
        {
            total_reps_non_zero += reps;
            ++count_reps_non_zero;
        }
        i += reps;
    }
    use_rle_for_non_zeros = (total_reps_non_zero > count_reps_non_zero * 2);
    use_rle_for_zeros = (total_reps_zero > count_reps_zero * 2);
}

void BrotligHuffmanTree::ComputeRLEZerosReps(
    size_t repeitions,
    std::vector<uint8_t>& rle_codes,
    std::vector<uint8_t>& rle_extra_bits
)
{
    if (repeitions == 11)
    {
        rle_codes.push_back(0);
        rle_extra_bits.push_back(0);
        --repeitions;
    }

    if (repeitions < 3)
    {
        for (size_t i = 0; i < repeitions; ++i)
        {
            rle_codes.push_back(0);
            rle_extra_bits.push_back(0);
        }
    }
    else
    {
       /* repeitions -= 3;
        while (true)
        {
            rle_codes.push_back(BROTLI_REPEAT_ZERO_CODE_LENGTH);
            rle_extra_bits.push_back(repeitions & 0x7);
            repeitions >>= 3;
            if (repeitions == 0)
                break;
            --repeitions;
        }*/

        while (true)     // To do: Revert back to the commented code, this only to get the shader running to completion
        {
            size_t minus = (repeitions > 10) ? 10 : repeitions;
            repeitions -= minus;
            rle_codes.push_back(BROTLI_REPEAT_ZERO_CODE_LENGTH);
            rle_extra_bits.push_back(static_cast<uint8_t>(minus - 3));
            if (repeitions < 3)
                break;
        }

        if (repeitions != 0)
        {
            for (size_t i = 0; i < repeitions; ++i)
            {
                rle_codes.push_back(0);
                rle_extra_bits.push_back(0);
            }
        }
    }
}

void BrotligHuffmanTree::ComputeRLEReps(
    const uint8_t prev_value,
    const uint8_t value,
    size_t repetitions,
    std::vector<uint8_t>& rle_codes,
    std::vector<uint8_t>& rle_extra_bits
)
{
    assert(repetitions > 0);
    if (prev_value != value)
    {
        rle_codes.push_back(value);
        rle_extra_bits.push_back(0);
        --repetitions;
    }

    if (repetitions == 7)
    {
        rle_codes.push_back(value);
        rle_extra_bits.push_back(0);
        --repetitions;
    }

    if (repetitions < 3)
    {
        for (size_t i = 0; i < repetitions; ++i)
        {
            rle_codes.push_back(value);
            rle_extra_bits.push_back(0);
        }
    }
    else
    {
        /*repetitions -= 3;
        while (true)
        {
            rle_codes.push_back(BROTLI_REPEAT_PREVIOUS_CODE_LENGTH);
            rle_extra_bits.push_back(repetitions & 0x3);
            repetitions >>= 2;
            if (repetitions == 0)
                break;
            --repetitions;
        }*/

        while (true) // To do: Revert back to the commented code, this only to get the shader running to completion
        {
            size_t minus = (repetitions > 6) ? 6 : repetitions;
            repetitions -= minus;
            rle_codes.push_back(BROTLI_REPEAT_PREVIOUS_CODE_LENGTH);
            rle_extra_bits.push_back(static_cast<uint8_t>(minus - 3));
            if (repetitions < 3)
                break;
        }

        if (repetitions != 0)
        {
            for (size_t i = 0; i < repetitions; ++i)
            {
                rle_codes.push_back(value);
                rle_extra_bits.push_back(0);
            }
        }
    }
}

void BrotligHuffmanTree::LoadTrivialTree(BrotligDeswizzler* dsw, size_t num_symbols, size_t max_bits)
{
    uint32_t symbol = dsw->ReadAndConsume(static_cast<uint32_t>(max_bits));
    
    BrotligHuffmanNodePtr newNode = new BrotligHuffmanNode;
    newNode->value = symbol;
    newNode->code = 0;
    newNode->revcode = BrotligReverseBits(1, 0);
    newNode->bitsize = 1;

    m_treenodes.push_back(newNode);

    SetupForDecoding();

    dsw->Reset();
}

void BrotligHuffmanTree::LoadSimpleTree(BrotligDeswizzler* dsw, size_t num_symbols, bool tree_select, size_t max_bits)
{
    BrotligHuffmanNodePtr newNode = nullptr;
    for (size_t i = 0; i < num_symbols; ++i)
    {
        uint16_t symbol = static_cast<uint16_t>(dsw->ReadAndConsume(static_cast<uint32_t>(max_bits), true));
        uint16_t code = 0;
        size_t codelen = 0;
        FetchFixedHuffmanCode(num_symbols, tree_select, i, code, codelen);
        
        newNode = new BrotligHuffmanNode;
        newNode->value = symbol;
        newNode->code = code;
        newNode->revcode = BrotligReverseBits(codelen, code);
        newNode->bitsize = codelen;

        m_treenodes.push_back(newNode);
    }

    SetupForDecoding();

    dsw->Reset();
}

void BrotligHuffmanTree::FetchFixedHuffmanCode(size_t num_symbols, bool tree_select, size_t symbolindex, uint16_t& code, size_t& length)
{
    static const uint16_t codes[4][4] = {
        { 0, 1, 0, 0 },
        { 0, 2, 3, 0 },
        { 0, 1, 2, 3 },
        { 0, 2, 6, 7 }
    };

    static const size_t codelengths[4][4] = {
        { 1, 1, 0, 0 },
        { 1, 2, 2, 0 },
        { 2, 2, 2, 2 },
        { 1, 2, 3, 3 }
    };

    size_t table_idx = num_symbols < 4 ? num_symbols - 2 : tree_select ? 3 : 2;
    code = codes[table_idx][symbolindex];
    length = codelengths[table_idx][symbolindex];
}

void BrotligHuffmanTree::LoadComplexTree(BrotligDeswizzler* dsw, size_t num_len_symbols, size_t num_symbols, size_t max_bits, bool debug)
{
    static const uint8_t kReadOrder[BROTLI_CODE_LENGTH_CODES] = { 
        1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15
    };

    std::vector<uint32_t> len_symbol_lens(num_len_symbols);
    BrotligHuffmanTree* len_sym_tree = new BrotligHuffmanTree;
    size_t minsize = BROTLIG_MAX_HUFFMAN_DEPTH + 2;
    for (size_t i = 0; i < num_len_symbols; ++i)
    {
        len_symbol_lens.at(kReadOrder[i]) = dsw->ReadAndConsume(5, true);

        BrotligHuffmanNodePtr newNode = new BrotligHuffmanNode();
        newNode->value = (uint16_t)kReadOrder[i];
        newNode->depth = len_symbol_lens.at(kReadOrder[i]);
        if (newNode->depth != 0 && newNode->depth < minsize)
            minsize = newNode->depth;

        len_sym_tree->m_symbolmap.insert(std::pair<uint16_t, BrotligHuffmanNodePtr>(newNode->value, newNode));
    }

    len_sym_tree->SetMinCodeSize(minsize);
    len_sym_tree->ComputeCodes(true);
    dsw->Reset();

    uint16_t symbol_len = 0, prev_symbol_len = BROTLI_INITIAL_REPEATED_CODE_LENGTH;
    for (size_t i = 0, k = 0; i < num_symbols; k++)
    {
        uint16_t code = static_cast<uint16_t>(dsw->ReadNoConsume(static_cast<uint32_t>(16)));
        size_t codelen = 0;
        BrotligHuffmanNodePtr node = len_sym_tree->Symbol(BrotligReverseBits(16, code));
        symbol_len = node->value;
        codelen = node->bitsize;
        dsw->Consume(static_cast<uint32_t>(codelen), false);
        uint32_t num_reps = 1;
        if (symbol_len <= 15)
        {
            num_reps = 1;
            dsw->BSSwitch();
            prev_symbol_len = symbol_len;
        }

        if (symbol_len == BROTLI_REPEAT_PREVIOUS_CODE_LENGTH)
        {
            num_reps = dsw->ReadAndConsume(2, true) + 3;
            symbol_len = prev_symbol_len;
        }
         
        if (symbol_len == BROTLI_REPEAT_ZERO_CODE_LENGTH)
        {
            num_reps = dsw->ReadAndConsume(3, true) + 3;
            symbol_len = 0;
        }

        for (size_t k = 0; k < num_reps; ++k)
        {
            BrotligHuffmanNodePtr newNode = new BrotligHuffmanNode();
            newNode->value = (uint16_t)(i + k);
            newNode->depth = (uint8_t)symbol_len;

            m_symbolmap.insert(std::pair<uint16_t, BrotligHuffmanNodePtr>(newNode->value, newNode));
        }

        i += num_reps;
    }

    ComputeCodes(true);
    delete len_sym_tree;
    dsw->Reset();
}

void BrotligHuffmanTree::DeleteTree(BrotligHuffmanNodePtr& pNode)
{
    // Base case: empty tree
    if (pNode == nullptr) {
        return;
    }

    // delete left and right subtree first (Postorder)
    DeleteTree(pNode->left);
    DeleteTree(pNode->right);

    // delete the current node after deleting its left and right subtree
    delete pNode;

    // set root as null before returning
    pNode = NULL;
}
