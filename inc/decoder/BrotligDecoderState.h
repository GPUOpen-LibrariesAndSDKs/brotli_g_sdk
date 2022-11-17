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

#include "common/BrotligCommand.h"
#include "common/BrotligHuffman.h"
#include "common/BrotligBitReader.h"
#include "common/BrotligBitWriter.h"

namespace BrotliG
{
    typedef struct BrotligDecoderParams
    {
        int lgwin;
        uint32_t distance_postfix_bits;
        uint32_t num_direct_distance_codes;

        size_t page_size;
        size_t num_bitstreams;
        size_t cmd_group_size;
        size_t swizzle_size;

        BrotligDecoderParams()
        {
            lgwin = BROTLI_DEFAULT_WINDOW;
            distance_postfix_bits = 0;
            num_direct_distance_codes = 0;

            page_size = 65536;
            num_bitstreams = 32;
            cmd_group_size = 1;
            swizzle_size = 4;
        }

        BrotligDecoderParams(
            size_t p_size,
            size_t n_bitstreams,
            size_t c_group_size,
            size_t s_size
        )
        {
            lgwin = BROTLI_DEFAULT_WINDOW;
            distance_postfix_bits = 0;
            num_direct_distance_codes = 0;

            page_size = p_size;
            num_bitstreams = n_bitstreams;
            cmd_group_size = c_group_size;
            swizzle_size = s_size;
        }

        BrotligDecoderParams& operator=(const BrotligDecoderParams& other)
        {
            this->lgwin = other.lgwin;
            this->distance_postfix_bits = other.distance_postfix_bits;
            this->num_direct_distance_codes = other.num_direct_distance_codes;
            this->page_size = other.page_size;
            this->num_bitstreams = other.num_bitstreams;
            this->cmd_group_size = other.cmd_group_size;
            this->swizzle_size = other.swizzle_size;

            return *this;
        }
    }BrotligDecoderParams;

    typedef struct BrotligDecoderState
    {
        BrotligDecoderParams* params;
        std::vector<BrotligHuffmanTree*> symbolTrees;
        BrotligBitReader* br;
        bool isLast;
        bool isEmpty;
        bool isUncompressed;
        uint32_t distring[4];

        size_t uncompLen;
        std::vector<std::vector<Byte>> bitstreams;

        BrotligDecoderState(BrotligDecoderParams* inparams)
        {
            BrotligDecoderParams* temp = new BrotligDecoderParams;
            (*temp) = (*inparams);
            params = temp;

            distring[0] = 4;
            distring[1] = 11;
            distring[2] = 15;
            distring[3] = 16;

            isLast = false;
            isEmpty = false;
            isUncompressed = false;
            uncompLen = 0;

            bitstreams.resize(params->num_bitstreams);
            br = new BrotligBitReader;
        }

        ~BrotligDecoderState()
        {
            for (size_t i = 0; i < params->num_bitstreams; ++i)
            {
                bitstreams.at(i).clear();
            }

            bitstreams.clear();

            for (size_t i = 0; i < symbolTrees.size(); ++i)
            {
                if (symbolTrees.at(i) != nullptr)
                {
                    delete symbolTrees.at(i);
                    symbolTrees.at(i) = nullptr;
                }
            }

            symbolTrees.clear();

            delete params;
            params = nullptr;

            delete br;
            br = nullptr;
        }

    }BrotligDecoderState;
}
