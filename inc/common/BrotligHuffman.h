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

#pragma once

#include <map>
#include <string>
#include <queue>

#include "BrotligBitWriter.h"
#include "BrotligBitReader.h"
#include "BrotligSwizzler.h"
#include "BrotligDeswizzler.h"

namespace BrotliG
{
    enum BrotligHuffmanType
    {
        SMALL,
        LARGE,
        COMPLEX
    };

    struct BrotligHuffmanNode
    {
        uint16_t value;
        uint32_t total_count;
        std::string s_code;
        uint16_t code;
        uint16_t revcode;
        uint8_t depth;
        size_t bitsize;
        BrotligHuffmanNode* left;
        BrotligHuffmanNode* right;

        BrotligHuffmanNode()
        {
            value = 0;
            total_count = 0;
            s_code = "";
            code = 0;
            revcode = 0;
            depth = 0;
            bitsize = 0;
            left = nullptr;
            right = nullptr;
        }

        ~BrotligHuffmanNode()
        {
            value = 0;
            total_count = 0;
            s_code = "";
            code = 0;
            revcode = 0;
            depth = 0;
            bitsize = 0;
            left = nullptr;
            right = nullptr;
        }
    };

    typedef BrotligHuffmanNode* BrotligHuffmanNodePtr;

    class BrotligHuffmanTree
    {
    public:
        BrotligHuffmanTree();

        ~BrotligHuffmanTree();

        bool Build(const std::vector<uint32_t>& histogram);

        void Store(BrotligSwizzler* sw, size_t alphabet_size, bool debug = false);
        void StoreHufftreeOfHufftree(BrotligSwizzler* sw, const int num_codes, size_t codesize, bool debug = false);

        static BrotligHuffmanTree* Load(BrotligDeswizzler* deswizzler, size_t alphabet_size, bool debug = false);
        void LoadTree(BrotligDeswizzler* deswizzler, size_t alphabet_size, bool debug = false);

        uint16_t Code(uint16_t symbol);
        uint16_t Revcode(uint16_t symbol);
        size_t Bitsize(uint16_t symbol);
        BrotligHuffmanNodePtr Symbol(uint16_t code);
        uint16_t Symbol(uint16_t code, size_t& codelen);
        bool Has(uint16_t code);
        void SetMinCodeSize(size_t size);
        size_t MinCodeSize();
        uint16_t MostFreqSymbol();

        void Display(std::string name, std::ostream& stream);

        static void FetchFixedHuffmanCode(size_t num_symbols, bool tree_select, size_t symbolindex, uint16_t& code, size_t& length);
    private:

        /**
        ***********************************************************************************************************************
        * @brief This class is used to compare two Huffman tree nodes.
        ***********************************************************************************************************************
        */
        class compare
        {
        public:
            bool operator() (const BrotligHuffmanNodePtr& c1, const BrotligHuffmanNodePtr& c2) const
            {
                if (c1->total_count != c2->total_count)
                    return c1->total_count > c2->total_count;
                else
                    return c1->value < c2->value;
            }
        };

        bool CreatePriorityQueue(
            const std::vector<uint32_t>& histogram
        );
        void CreateHuffmanTree();
        bool ComputeDepth();
        void ComputeCodes(bool forDecoder = false);
        void SetupForDecoding();
        void Cleanup();

        bool TraverseTree(BrotligHuffmanNodePtr pNode, std::string code, uint8_t d);
        uint16_t BinaryToDecimal(std::string& bString);

        void StoreSimpleTree(BrotligSwizzler* sw, size_t num_symbols, size_t max_bits);
        void StoreComplexTree(BrotligSwizzler* sw, size_t alphabet_size, size_t max_bits, bool debug = false);

        void LoadTrivialTree(BrotligDeswizzler* dsw, size_t num_symbols, size_t max_bits);
        void LoadSimpleTree(BrotligDeswizzler* dsw, size_t num_symbols, bool tree_select, size_t max_bits);
        void LoadComplexTree(BrotligDeswizzler* dsw, size_t num_len_symbols, size_t num_symbols, size_t max_bits, bool debug = false);

        void CheckRLEUse(
            std::vector<BrotligHuffmanNodePtr>& treenodes,
            bool& use_rle_for_non_zeros,
            bool& use_rle_for_zeros);

        void ComputeRLEZerosReps(
            size_t repeitions,
            std::vector<uint8_t>& rel_codes,
            std::vector<uint8_t>& rel_extra_bits
        );

        void ComputeRLEReps(
            const uint8_t prev_value,
            const uint8_t value,
            size_t repetitions,
            std::vector<uint8_t>& rel_codes,
            std::vector<uint8_t>& rel_extra_bits
        );

        void DeleteTree(BrotligHuffmanNodePtr& pNode);

        std::map<uint16_t, BrotligHuffmanNodePtr> m_symbolmap;
        std::priority_queue<BrotligHuffmanNodePtr, std::vector<BrotligHuffmanNodePtr>, compare> m_pQueue;
        BrotligHuffmanNode* m_root;
        size_t maxcodebitsize, mincodebitsize;
        std::vector<BrotligHuffmanNodePtr> m_symbol_table;
        std::vector<BrotligHuffmanNodePtr> m_treenodes;
        uint16_t m_mostFreqSymbol;
    };
}
