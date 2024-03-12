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
#pragma once

extern "C" {
#include "brotli/c/dec/huffman.h"
}

#include "common/BrotligCommand.h"
#include "common/BrotligDeswizzler.h"
#include "common/BrotligCommandLut.h"
#include "common/BrotligDataConditioner.h"

#include "DataStream.h"

namespace BrotliG
{
    typedef struct BrotligDecoderParams
    {
        int lgwin;
        uint32_t distance_postfix_bits;
        uint32_t num_direct_distance_codes;

        size_t page_size;
        size_t num_bitstreams;

        BrotligDecoderParams()
        {
            lgwin = BROTLI_DEFAULT_WINDOW;
            distance_postfix_bits = 0;
            num_direct_distance_codes = 0;

            page_size = BROTLIG_DEFAULT_PAGE_SIZE;
            num_bitstreams = BROLTIG_DEFAULT_NUM_BITSTREAMS;
        }

        BrotligDecoderParams(
            size_t p_size,
            size_t n_bitstreams
        )
        {
            lgwin = BROTLI_DEFAULT_WINDOW;
            distance_postfix_bits = 0;
            num_direct_distance_codes = 0;

            page_size = p_size;
            num_bitstreams = n_bitstreams;
        }

        BrotligDecoderParams& operator=(const BrotligDecoderParams& other)
        {
            this->lgwin = other.lgwin;
            this->distance_postfix_bits = other.distance_postfix_bits;
            this->num_direct_distance_codes = other.num_direct_distance_codes;
            this->page_size = other.page_size;
            this->num_bitstreams = other.num_bitstreams;

            return *this;
        }
    }BrotligDecoderParams;

    class PageDecoder
    {
    public:
        PageDecoder();
        ~PageDecoder();

        bool Setup(const BrotligDecoderParams& params, const BrotligDataconditionParams& dcParams);
        bool Run(const uint8_t* input, size_t inputSize, size_t inputOffset, uint8_t* output, size_t outputSize, size_t outputOffset);
        void Cleanup();

    private:
        inline bool DecodeCommand(BrotligCommand& cmd);
        inline uint8_t DecodeLiteral();
        uint8_t DecodeNFetchLiteral(uint16_t& code, size_t& codelen);
        inline uint32_t DecodeDistance();
        inline void TranslateDistance(BrotligCommand& cmd);

        inline uint32_t DeconditionBC1_5(uint32_t offsetAddr, uint32_t sub);
        void DeltaDecode(size_t page_start, size_t page_end, uint8_t* input);
        void DeltaDecodeByte(size_t inSize, uint8_t* inData);

        BrotligDecoderParams m_params;
        BrotligDataconditionParams m_dcparams;

        uint16_t* m_symbols[BROTLIG_NUM_HUFFMAN_TREES];
        uint16_t* m_codelens[BROTLIG_NUM_HUFFMAN_TREES];

        uint32_t m_distring[4];

        BrotligDeswizzler m_pReader;
    };
}
