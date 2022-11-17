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

#include "BrotligDeswizzler.h"

extern "C" {
#include "brotli/c/enc/fast_log.h"
}

using namespace BrotliG;

BrotligDeswizzler::BrotligDeswizzler(BrotligBitReader* inReader, size_t inSize, size_t num_bitstreams, size_t swizzle_size)
{
    m_inReader = inReader;
    m_inSize = inSize;

    m_readers.resize(num_bitstreams);

    m_num_bitstreams = num_bitstreams;
    m_swizzle_size = swizzle_size;

    Reset();
}

BrotligDeswizzler::~BrotligDeswizzler()
{
    for (size_t i = 0; i < m_num_bitstreams; ++i)
    {
        delete m_readers.at(i);
        m_readers.at(i) = nullptr;
    }

    m_readers.clear();
}

uint32_t BrotligDeswizzler::ReadNoConsume(uint32_t n)
{
    uint32_t bits = m_readers.at(m_cur_bindex)->ReadNoConsume(n);
    return bits;
}

uint32_t BrotligDeswizzler::ReadAndConsume(uint32_t n, bool bsswitch)
{
    uint32_t bits = ReadNoConsume(n);
    Consume(n, bsswitch);
    return bits;
}

void BrotligDeswizzler::Consume(uint32_t n, bool bsswitch)
{
    m_readers.at(m_cur_bindex)->Consume(n);
    if (bsswitch) BSSwitch();
}

void BrotligDeswizzler::SetRange(size_t start_index, size_t end_index)
{
    assert(start_index < m_num_bitstreams - 1);
    assert(end_index > 0);
    assert(start_index <= end_index);

    m_start_index = start_index;
    m_end_index = end_index;
    m_cur_bindex = m_start_index;
}

void BrotligDeswizzler::BSSwitch()
{
    ++m_cur_bindex;
    assert(m_cur_bindex <= m_end_index + 1);
    if (m_cur_bindex == m_end_index + 1)
        m_cur_bindex = m_start_index;
}

void BrotligDeswizzler::Reset()
{
    SetRange(0, m_num_bitstreams - 1);
}

BrotligBitReader* BrotligDeswizzler::GetReader(size_t index)
{
    return m_readers.at(index);
}

void BrotligDeswizzler::DeserializeCompact()
{
    std::vector<size_t> bslengths(m_num_bitstreams);

    // Computing base size in bits
    size_t rAvgBSSizeInBytes = (m_inSize + 31) / 32;
    size_t baseSizeBits = static_cast<size_t>(Log2FloorNonZero(rAvgBSSizeInBytes)) + 1;

    // Delta size in bits
    size_t logSize = static_cast<size_t>(Log2FloorNonZero(m_inSize - 1)) + 1;
    size_t deltaBitsSizeBits = static_cast<size_t>(Log2FloorNonZero(logSize)) + 1;

    size_t baseSize = static_cast<size_t>(m_inReader->ReadAndConsume(static_cast<uint32_t>(baseSizeBits)));
    size_t deltaSizeBits = static_cast<size_t>(m_inReader->ReadAndConsume(static_cast<uint32_t>(deltaBitsSizeBits)));
    size_t totalbslengthInBytes = 0;

    for (size_t i = 0; i < m_num_bitstreams; ++i)
    {
        size_t delta = static_cast<size_t>(m_inReader->ReadAndConsume(static_cast<uint32_t>(deltaSizeBits)));
        bslengths.at(i) = baseSize + delta;
        totalbslengthInBytes += bslengths.at(i);
    }

    m_inReader->AlignToNextDWord();

    size_t curBitPos = m_inReader->GetCurBitPos();
    size_t curBytePos = curBitPos / 8;
    const uint8_t* srcPtr = m_inReader->GetInput();
    size_t totalbytesRead = 0;

    for (size_t index = 0; index < bslengths.size() && bslengths.at(index) != 0; ++index)
    {
        size_t bslength = bslengths.at(index);

        m_readers.at(index) = new BrotligBitReader;
        m_readers.at(index)->Initialize(srcPtr + curBytePos, bslength, 0);

        curBytePos += bslength;
        totalbytesRead += bslength;
    }

    assert(totalbslengthInBytes == totalbytesRead);
}

size_t BrotligDeswizzler::CurBinIndex()
{
    return m_cur_bindex;
}

