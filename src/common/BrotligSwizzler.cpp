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

#include "common/BrotligUtils.h"

#include "BrotligSwizzler.h"

using namespace BrotliG;

BrotligSwizzler::BrotligSwizzler(size_t num_bitstreams, size_t bssizes)
{
    m_headerStream.resize(bssizes, 0);
    m_headerWriter = new BrotligBitWriterLSB;
    m_headerWriter->SetStorage(m_headerStream.data());
    m_headerWriter->SetPosition(0);

    m_bitstreams.resize(num_bitstreams);
    m_writers.resize(num_bitstreams);
    for (size_t i = 0; i < num_bitstreams; ++i)
    {
        m_bitstreams.at(i) = std::vector<uint8_t>(bssizes, 0);
        BrotligBitWriterLSB* bw = new BrotligBitWriterLSB;
        bw->SetStorage(m_bitstreams.at(i).data());
        bw->SetPosition(0);
        m_writers.at(i) = bw;
    }

    m_numbitstreams = num_bitstreams;
    m_curindex = 0;

    m_outWriter = nullptr;
    m_outSize = 0;
}

BrotligSwizzler::~BrotligSwizzler()
{
    m_headerStream.clear();
    delete m_headerWriter;

    for (size_t i = 0; i < m_numbitstreams; ++i)
    {
        delete m_writers.at(i);
        m_bitstreams.at(i).clear();
    }

    m_writers.clear();
    m_bitstreams.clear();
    m_outWriter = nullptr;
    m_outSize = 0;
}

void BrotligSwizzler::AppendBitstreamSizes()
{
    size_t curheadersizeInBytes = (m_headerWriter->GetPosition() + 8 - 1) / 8;
    size_t curheadersizeDWAligned = ((curheadersizeInBytes + 4 - 1) / 4) * 4;
    size_t totbslengthInBytes = 0, maxSize = 0, minSize = BROTLIG_MAX_PAGE_SIZE, lenInBytes = 0, i = 0;
    std::vector<size_t> bsLengthsInBytes(m_numbitstreams, 0);
    for (i = 0; i < m_numbitstreams; ++i)
    {
        lenInBytes = (m_writers.at(i)->GetPosition() + 8 - 1) / 8;
        totbslengthInBytes += lenInBytes;
        bsLengthsInBytes.at(i) = lenInBytes;
        if (lenInBytes < minSize)
            minSize = lenInBytes;
        if (lenInBytes > maxSize)
            maxSize = lenInBytes;
    }

    size_t estimateSizeInBytes = curheadersizeDWAligned + totbslengthInBytes;
    size_t baseSizeBits = 0, deltaBitsSizeBits = 0, logSize = 0, deltaSizeBits = 0, offset = 0, rAvgBSSizeInBytes = 0;
    size_t totalDeltaSizeInBits = 0, newHeaderSizeInBits = 0, newHeaderSizeInBytes = 0, newHeaderSizeDWAligned = 0, newEstimateSizeInBytes = 0, newRAvgBSSizeInBytes = 0, newBaseSizeBits = 0;
    std::vector<size_t> offsets(m_numbitstreams, 0);

    // Compute offsets
    for (i = 0; i < m_numbitstreams; ++i)
    {
        offset = bsLengthsInBytes.at(i) - minSize;
        offsets.at(i) = offset;
        size_t offsetSizeBits = offset ? static_cast<size_t>(Log2FloorNonZero(offset)) + 1 : 1;
        if (deltaSizeBits < offsetSizeBits)
            deltaSizeBits = offsetSizeBits;
    }

    bool redo = true;
    while (redo)
    {
        // Computing base size in bits
        rAvgBSSizeInBytes = (estimateSizeInBytes + (m_numbitstreams - 1)) / m_numbitstreams;
        baseSizeBits = static_cast<size_t>(Log2FloorNonZero(rAvgBSSizeInBytes)) + 1;

        // Delta size in bits
        logSize = static_cast<size_t>(Log2FloorNonZero(estimateSizeInBytes - 1)) + 1;
        deltaBitsSizeBits = static_cast<size_t>(Log2FloorNonZero(logSize)) + 1;

        totalDeltaSizeInBits = deltaSizeBits * m_numbitstreams;
        newHeaderSizeInBits = (m_headerWriter->GetPosition() + baseSizeBits + deltaBitsSizeBits + totalDeltaSizeInBits);
        newHeaderSizeInBytes = (newHeaderSizeInBits + 8 - 1) / 8;
        newHeaderSizeDWAligned = ((newHeaderSizeInBytes + 4 - 1) / 4) * 4;
        newEstimateSizeInBytes = newHeaderSizeDWAligned + totbslengthInBytes;
        newRAvgBSSizeInBytes = (newEstimateSizeInBytes + (m_numbitstreams - 1)) / m_numbitstreams;
        newBaseSizeBits = static_cast<size_t>(Log2FloorNonZero(newRAvgBSSizeInBytes)) + 1;

        redo = !((Log2FloorNonZero(newEstimateSizeInBytes - 1) == Log2FloorNonZero(estimateSizeInBytes - 1))
            && (newBaseSizeBits == baseSizeBits));

        if (redo)
        {
            estimateSizeInBytes = newEstimateSizeInBytes;
        }
    }

    assert(minSize == 0 || CountBits((uint32_t)minSize) <= baseSizeBits);
    AppendToHeader(static_cast<uint32_t>(baseSizeBits), static_cast<uint32_t>(minSize));

    assert(deltaSizeBits == 0 || CountBits((uint32_t)deltaSizeBits) <= deltaBitsSizeBits);
    AppendToHeader(static_cast<uint32_t>(deltaBitsSizeBits), static_cast<uint32_t>(deltaSizeBits));

    for (i = 0; i < m_numbitstreams; ++i)
    {
        //assert(offsets.at(i) <= maxDeltaSize);
        assert((uint32_t)offsets.at(i) == 0 || CountBits((uint32_t)offsets.at(i)) <= deltaSizeBits);
        AppendToHeader(static_cast<uint32_t>(deltaSizeBits), static_cast<uint32_t>(offsets.at(i)));
    }

    m_headerWriter->AlignToNextDWord();
}

void BrotligSwizzler::SerializeHeader()
{
    size_t readbits = 0;
    size_t indexoff = 0;
    while (readbits < m_headerWriter->GetPosition())
    {
        uint32_t bitsRead = m_headerStream[indexoff];
        bitsRead |= m_headerStream[indexoff + 1] << 8;
        bitsRead |= m_headerStream[indexoff + 2] << 16;
        bitsRead |= m_headerStream[indexoff + 3] << 24;

        m_outWriter->Write(32, (uint64_t)bitsRead);

        indexoff += 4;
        readbits += 32;
    }
}

void BrotligSwizzler::SerializeBitstreams()
{
    size_t streamsize = 0;
    uint32_t bitsToStore = 0;
    std::vector<uint8_t> bytebuffer;
    for (size_t i = 0; i < m_numbitstreams; ++i)
    {
        streamsize = (m_writers.at(i)->GetPosition() + 8 - 1) / 8;
        for (size_t byteindex = 0; byteindex < streamsize; ++byteindex)
            bytebuffer.push_back(m_bitstreams.at(i).at(byteindex));
    }

    for(size_t byteindex = 0;byteindex < bytebuffer.size(); byteindex += 4)
    {
        bitsToStore = 0;
        for (size_t k = 0; k < 4; ++k)
        {
            if (byteindex + k >= bytebuffer.size()) break;
            bitsToStore |= (uint32_t)bytebuffer[byteindex + k] << (k * 8);
        }

        assert(m_outWriter->GetPosition() + 32 <= m_outSize * 8);
        m_outWriter->Write(32, (uint64_t)bitsToStore);
    }

    Clear();
    BSReset();
}

void BrotligSwizzler::SetOutWriter(BrotligBitWriterLSB* outWriter, size_t outSize)
{
    m_outWriter = outWriter;
    m_outSize = outSize;
}

void BrotligSwizzler::Clear()
{
    for (size_t i = 0; i < m_numbitstreams; ++i)
    {
        std::fill(m_bitstreams.at(i).begin(), m_bitstreams.at(i).end(), 0);
        m_writers.at(i)->SetPosition(0);
    }
}