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

#include "BrotligBitWriter.h"

namespace BrotliG
{
    class BrotligSwizzler
    {
    public:
        BrotligSwizzler(size_t num_bitstreams, size_t bssizes, size_t swizzle_size);

        ~BrotligSwizzler();

        void AppendToHeader(uint32_t n, uint32_t bits);
        void AppendBitstreamSizes();
        void Append(uint32_t n, uint32_t bits, bool bsswitch = false);
        void StoreVarLenUint8(size_t n);

        void AlignToByteBoundary();
        void AlignToDWBoundary();

        void AlignAllToByteBoundary();
        void AlignAllToDWBoundary();

        void SetRange(size_t start_index, size_t end_index);
        void BSSwitch();
        void Reset();
        void Clear();

        BrotligBitWriter* GetWriter(size_t index);

        bool Serialize(bool reset = true, bool addpadding = false);
        bool SerializeHeader();
        bool SerializeCompact(size_t page_size, bool writeout = false, bool reset = true);

        size_t CurBinIndex();

        void SetOutWriter(BrotligBitWriter* outWriter, size_t outSize);

    private:
        std::vector<uint8_t> m_headerStream;
        std::vector<std::vector<uint8_t>> m_bitstreams;

        BrotligBitWriter* m_headerWriter;
        std::vector<BrotligBitWriter*> m_writers;

        BrotligBitWriter* m_outWriter;
        size_t m_outSize;
        size_t m_swizzle_size;

        size_t m_num_bitstreams;
        size_t m_cur_bindex;

        size_t m_start_index;
        size_t m_end_index;

    };
}