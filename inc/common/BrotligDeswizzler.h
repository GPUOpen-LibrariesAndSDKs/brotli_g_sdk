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

#include "BrotligBitReader.h"

namespace BrotliG
{
    class BrotligDeswizzler
    {
    public:
        BrotligDeswizzler(BrotligBitReader* inReader, size_t inSize, size_t num_bitstreams, size_t swizzle_size);

        ~BrotligDeswizzler();

        uint32_t ReadNoConsume(uint32_t n);
        void Consume(uint32_t n, bool bsswitch = false);
        uint32_t ReadAndConsume(uint32_t n, bool bsswitch = false);

        void SetRange(size_t start_index, size_t end_index);
        void BSSwitch();
        void Reset();

        BrotligBitReader* GetReader(size_t index);

        void DeserializeCompact();

        size_t CurBinIndex();

    private:
        std::vector<std::vector<uint8_t>> m_bitstreams;
        std::vector<BrotligBitReader*> m_readers;

        BrotligBitReader* m_inReader;
        size_t m_inSize;
        size_t m_swizzle_size;

        size_t m_num_bitstreams;
        size_t m_cur_bindex;

        size_t m_start_index;
        size_t m_end_index;
    };
}