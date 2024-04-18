// Brotli-G SDK 1.2
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

#include "common/BrotligBitWriter.h"
#include "common/BrotligConstants.h"

namespace BrotliG
{
    class BrotligSwizzler
    {
    public:
        BrotligSwizzler(size_t num_bitstreams, size_t bssizes);

        ~BrotligSwizzler();

        inline void AppendToHeader(uint32_t n, uint32_t bits) 
        { 
            m_headerWriter->Write(n, (uint64_t)bits); 
        }

        void AppendBitstreamSizes();

        inline void Append(uint32_t n, uint64_t bits, bool bsswitch = false)
        {
            m_writers.at(m_curindex)->Write(n, bits);
            if (bsswitch) BSSwitch();
        }

        inline void BSSwitch()
        {
            ++m_curindex;
            if (m_curindex == m_numbitstreams)
                m_curindex = 0;
        }

        inline void BSReset()
        {
            m_curindex = 0;
        }

        void Clear();

        void SetOutWriter(BrotligBitWriterLSB* outWriter, size_t outSize);

        void SerializeHeader();
        void SerializeBitstreams();

    private:
        std::vector<uint8_t> m_headerStream;
        std::vector<std::vector<uint8_t>> m_bitstreams;

        BrotligBitWriterLSB* m_headerWriter;
        std::vector<BrotligBitWriterLSB*> m_writers;

        BrotligBitWriterLSB* m_outWriter;
        size_t m_outSize;

        size_t m_numbitstreams;
        size_t m_curindex;
    };
}