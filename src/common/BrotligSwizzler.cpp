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

#include "BrotligSwizzler.h"

using namespace BrotliG;

BrotligSwizzler::BrotligSwizzler(size_t num_bitstreams, size_t bssizes, size_t swizzle_size)
{
    m_headerStream.resize(bssizes, 0);
    m_headerWriter = new BrotligBitWriter;
    m_headerWriter->SetStorage(m_headerStream.data());
    m_headerWriter->SetPosition(0);

    m_bitstreams.resize(num_bitstreams);
    m_writers.resize(num_bitstreams);
    for (size_t i = 0; i < num_bitstreams; ++i)
    {
        m_bitstreams.at(i) = std::vector<uint8_t>(bssizes, 0);
        BrotligBitWriter* bw = new BrotligBitWriter;
        bw->SetStorage(m_bitstreams.at(i).data());
        bw->SetPosition(0);
        m_writers.at(i) = bw;
    }

    m_num_bitstreams = num_bitstreams;
    m_swizzle_size = swizzle_size;

    Reset();
}

BrotligSwizzler::~BrotligSwizzler()
{
    m_headerStream.clear();
    delete m_headerWriter;

    for (size_t i = 0; i < m_num_bitstreams; ++i)
    {
        delete m_writers.at(i);
        m_bitstreams.at(i).clear();
    }

    m_writers.clear();
    m_bitstreams.clear();
    m_outWriter = nullptr;
    m_outSize = 0;
}

void BrotligSwizzler::AppendToHeader(uint32_t n, uint32_t bits)
{
    m_headerWriter->Write(n, (uint64_t)bits);
}

void BrotligSwizzler::AppendBitstreamSizes()
{
    size_t curheadersizeInBytes = (m_headerWriter->GetPosition() + 8 - 1) / 8;
    size_t curheadersizeDWAligned = ((curheadersizeInBytes + 4 - 1) / 4) * 4;
    size_t totbslengthInBytes = 0;
    std::vector<size_t> bsLengthsInBytes(m_num_bitstreams, 0);
    for (size_t i = 0; i < m_num_bitstreams; ++i)
    {
        size_t bitposition = m_writers.at(i)->GetPosition();
        size_t lenInBytes = (bitposition + 8 - 1) / 8;
        totbslengthInBytes += lenInBytes;
        bsLengthsInBytes.at(i) = lenInBytes;
    }

    size_t estimateSizeInBytes = curheadersizeDWAligned + totbslengthInBytes;
    bool redo = true;
    size_t baseSizeBits = 0, deltaBitsSizeBits = 0, deltaSizeBits = 0, maxDeltaSize = 0;
    std::vector<size_t> offsets(m_num_bitstreams, 0);
    size_t iteration = 0;
    size_t maxSize = 0;
    size_t minSize = bsLengthsInBytes.at(0);

    for (size_t i = 0; i < m_num_bitstreams; ++i)
    {
        if (bsLengthsInBytes.at(i) < minSize)
            minSize = bsLengthsInBytes.at(i);
        if (bsLengthsInBytes.at(i) > maxSize)
            maxSize = bsLengthsInBytes.at(i);
    }

    // Compute offsets
    deltaSizeBits = 0;
    for (size_t i = 0; i < m_num_bitstreams; ++i)
    {
        size_t offset = bsLengthsInBytes.at(i) - minSize;
        offsets.at(i) = offset;
        size_t offsetSizeBits = offset ? static_cast<size_t>(Log2FloorNonZero(offset)) + 1 : 1;
        if (deltaSizeBits < offsetSizeBits)
            deltaSizeBits = offsetSizeBits;
    }

    while (redo)
    {
        // Computing base size in bits
        size_t rAvgBSSizeInBytes = (estimateSizeInBytes + 31) / 32;
        baseSizeBits = static_cast<size_t>(Log2FloorNonZero(rAvgBSSizeInBytes)) + 1; // remove -1 from inside Log
        size_t maxBaseSize = (static_cast<size_t>(1u) << static_cast<uint32_t>(baseSizeBits));

        // Delta size in bits
        size_t logSize = static_cast<size_t>(Log2FloorNonZero(estimateSizeInBytes - 1)) + 1;
        deltaBitsSizeBits = static_cast<size_t>(Log2FloorNonZero(logSize)) + 1;
        size_t maxDeltaSizeBits = (static_cast<size_t>(1u) << static_cast<uint32_t>(deltaBitsSizeBits)); // remove -1

        assert(minSize <= maxBaseSize);

        assert(deltaSizeBits <= maxDeltaSizeBits);
        maxDeltaSize = (static_cast<size_t>(1u) << static_cast<uint32_t>(deltaSizeBits)) - 1;

        size_t totalDeltaSizeInBits = deltaSizeBits * m_num_bitstreams;
        size_t newHeaderSizeInBits = (m_headerWriter->GetPosition() + baseSizeBits + deltaBitsSizeBits + totalDeltaSizeInBits);
        size_t newHeaderSizeInBytes = (newHeaderSizeInBits + 8 - 1) / 8;
        size_t newHeaderSizeDWAligned = ((newHeaderSizeInBytes + 4 - 1) / 4) * 4;
        size_t newEstimateSizeInBytes = newHeaderSizeDWAligned + totbslengthInBytes;
        size_t newRAvgBSSizeInBytes = (newEstimateSizeInBytes + 31) / 32;
        size_t newBaseSizeBits = static_cast<size_t>(Log2FloorNonZero(newRAvgBSSizeInBytes)) + 1;

        redo = !((Log2FloorNonZero(newEstimateSizeInBytes - 1) == Log2FloorNonZero(estimateSizeInBytes - 1))
            && (newBaseSizeBits == baseSizeBits));

        if (redo)
        {
            estimateSizeInBytes = newEstimateSizeInBytes;
            iteration++;
        }
    }

    AppendToHeader(static_cast<uint32_t>(baseSizeBits), static_cast<uint32_t>(minSize));

    AppendToHeader(static_cast<uint32_t>(deltaBitsSizeBits), static_cast<uint32_t>(deltaSizeBits));

    for (size_t i = 0; i < m_num_bitstreams; ++i)
    {
        //assert(offsets.at(i) <= maxDeltaSize);
        AppendToHeader(static_cast<uint32_t>(deltaSizeBits), static_cast<uint32_t>(offsets.at(i)));
    }

    m_headerWriter->AlignToNextDWord();
}

void BrotligSwizzler::Append(uint32_t n, uint32_t bits, bool bsswitch)
{
    m_writers.at(m_cur_bindex)->Write(n, (uint64_t)bits);
    if (bsswitch) BSSwitch();
}

void BrotligSwizzler::StoreVarLenUint8(size_t n)
{
    BrotligBitWriter::StoreVanLenUint8(n, m_writers.at(m_cur_bindex));
}

void BrotligSwizzler::AlignToByteBoundary()
{
    m_writers.at(m_cur_bindex)->AlignToNextByte();
}

void BrotligSwizzler::AlignToDWBoundary()
{
    m_writers.at(m_cur_bindex)->AlignToNextDWord();
}

void BrotligSwizzler::AlignAllToByteBoundary()
{
    for (size_t i = 0; i < m_num_bitstreams; ++i)
    {
        m_writers.at(i)->AlignToNextByte();
    }
}
void BrotligSwizzler::AlignAllToDWBoundary()
{
    for (size_t i = 0; i < m_num_bitstreams; ++i)
    {
        m_writers.at(i)->AlignToNextDWord();
    }
}

bool BrotligSwizzler::SerializeHeader()
{
    int swizzleSizeInBits = static_cast<int>(m_swizzle_size * 8);
    size_t readbits = 0;
    size_t indexoff = 0;
    while (readbits < m_headerWriter->GetPosition())
    {
        uint32_t bitsRead = m_headerStream[indexoff];
        bitsRead |= m_headerStream[indexoff + 1] << 8;
        bitsRead |= m_headerStream[indexoff + 2] << 16;
        bitsRead |= m_headerStream[indexoff + 3] << 24;

        m_outWriter->Write(swizzleSizeInBits, (uint64_t)bitsRead);

        indexoff += 4;
        readbits += 32;
    }

    return true;
}

bool BrotligSwizzler::SerializeCompact(size_t page_size, bool writeout, bool reset)
{
    int swizzleSizeInBits = static_cast<int>(m_swizzle_size * 8);
    std::vector<size_t> bslengths(m_num_bitstreams);
    std::vector<size_t> bslengthsInBytes(m_num_bitstreams);
    size_t totallengthInBytes = 0;
    for (size_t i = 0; i < m_num_bitstreams; ++i)
    {
        bslengths.at(i) = m_writers.at(i)->GetPosition();
        bslengthsInBytes.at(i) = (m_writers.at(i)->GetPosition() + 8 - 1) / 8;
        totallengthInBytes += bslengthsInBytes.at(i);
    }

    std::vector<uint8_t> bytebuffer;
    for (size_t bindex = 0; bindex < m_num_bitstreams; ++bindex)
    {
        size_t streamsize = bslengthsInBytes.at(bindex);
        for (size_t byteindex = 0; byteindex < streamsize; ++byteindex)
        {
            bytebuffer.push_back(m_bitstreams.at(bindex).at(byteindex));
        }
    }

    for (size_t byteindex = 0; byteindex < bytebuffer.size(); byteindex += 4)
    {
        uint32_t bitsToStore = 0;
        for (size_t k = 0; k < 4; ++k)
        {
            if (byteindex + k >= bytebuffer.size())
                break;
            size_t shiftLeft = (k * 8);
            bitsToStore |= (uint32_t)bytebuffer[byteindex + k] << shiftLeft;
        }

        assert(m_outWriter->GetPosition() + swizzleSizeInBits <= m_outSize * 8);
        m_outWriter->Write(swizzleSizeInBits, (uint64_t)bitsToStore);
    }

    if (reset)
    {
        Clear();
        Reset();
    }

    return true;
}

bool BrotligSwizzler::Serialize(bool reset, bool addpadding)
{
    int swizzleSizeInBits = static_cast<int>(m_swizzle_size * 8);
    std::vector<size_t> bslengths(m_num_bitstreams);
    std::vector<uint32_t> padding(m_num_bitstreams);
    for (size_t i = 0; i < m_num_bitstreams; ++i)
    {
        bslengths.at(i) = m_writers.at(i)->GetPosition();
        padding.at(i) = 2;
    }
    size_t maxLength = *std::max_element(bslengths.begin(), bslengths.end());
    maxLength = (maxLength % swizzleSizeInBits == 0) ? maxLength : (maxLength + swizzleSizeInBits - (maxLength % swizzleSizeInBits));

    size_t minLength = *std::min_element(bslengths.begin(), bslengths.end());
    minLength = (minLength % swizzleSizeInBits == 0) ? minLength : (minLength + swizzleSizeInBits - (minLength % swizzleSizeInBits));

    std::vector<size_t> bspositions(m_num_bitstreams, 0);
    size_t curLength = 0;

    if (addpadding)
    {
        while (curLength != maxLength)
        {
            for (size_t bindex = 0; bindex < m_num_bitstreams; ++bindex)
            {
                size_t numBitsTotal = bslengths[bindex];
                size_t numBitsRead = bspositions[bindex];
                size_t numBitsLeft = numBitsTotal - numBitsRead;
                size_t numBitsToRead = (numBitsLeft >= swizzleSizeInBits) ? swizzleSizeInBits : numBitsLeft;

                uint32_t bitsRead = 0;
                if (numBitsToRead > 0)
                {
                    size_t iteration = 0;
                    size_t numBits = numBitsToRead;
                    while (numBits > 0)
                    {
                        size_t curNumBitsToRead = (numBits > 8) ? 8 : numBits;
                        size_t shiftLeft = (iteration * 8);
                        bitsRead |= (uint32_t)(m_bitstreams[bindex][bspositions[bindex] / 8]) << shiftLeft;
                        bspositions[bindex] += curNumBitsToRead;

                        numBits -= curNumBitsToRead;
                        ++iteration;
                    }
                }

                if (m_outWriter->GetPosition() + swizzleSizeInBits > m_outSize * 8)
                    return false;
                m_outWriter->Write(swizzleSizeInBits, (uint64_t)bitsRead);
            }

            curLength += swizzleSizeInBits;
        }
    }
    else
    {
        while (curLength != maxLength)
        {
            for (size_t bindex = 0; bindex < m_num_bitstreams; ++bindex)
            {
                size_t numBitsTotal = bslengths[bindex];
                size_t numBitsRead = bspositions[bindex];
                size_t numBitsLeft = numBitsTotal - numBitsRead;
                size_t numBitsToRead = (numBitsLeft >= swizzleSizeInBits) ? swizzleSizeInBits : numBitsLeft;

                uint32_t bitsRead = 0;
                if (numBitsToRead > 0)
                {
                    size_t iteration = 0;
                    size_t numBits = numBitsToRead;
                    while (numBits > 0)
                    {
                        size_t curNumBitsToRead = (numBits > 8) ? 8 : numBits;
                        size_t shiftLeft = (iteration * 8);
                        bitsRead |= (uint32_t)(m_bitstreams[bindex][bspositions[bindex] / 8]) << shiftLeft;
                        bspositions[bindex] += curNumBitsToRead;

                        numBits -= curNumBitsToRead;
                        ++iteration;
                    }
                    if (m_outWriter->GetPosition() > m_outSize * 8)
                        return false;
                    m_outWriter->Write(swizzleSizeInBits, (uint64_t)bitsRead);
                }
            }

            curLength += swizzleSizeInBits;
        }
    }

    if (reset)
    {
        Clear();
        Reset();
    }

    return true;
}

size_t BrotligSwizzler::CurBinIndex()
{
    return m_cur_bindex;
}

void BrotligSwizzler::SetOutWriter(BrotligBitWriter* outWriter, size_t outSize)
{
    m_outWriter = outWriter;
    m_outSize = outSize;
}

void BrotligSwizzler::SetRange(size_t start_index, size_t end_index)
{
    assert(start_index < m_num_bitstreams - 1);
    assert(end_index > 0);
    assert(start_index <= end_index);
    
    m_start_index = start_index;
    m_end_index = end_index;
    m_cur_bindex = m_start_index;
}

void BrotligSwizzler::BSSwitch()
{
    ++m_cur_bindex;
    assert(m_cur_bindex <= m_end_index + 1);
    if (m_cur_bindex == m_end_index + 1)
        m_cur_bindex = m_start_index;
}

void BrotligSwizzler::Reset()
{
    SetRange(0, m_num_bitstreams - 1);
}

BrotligBitWriter* BrotligSwizzler::GetWriter(size_t index)
{
    return m_writers.at(index);
}

void BrotligSwizzler::Clear()
{
    for (size_t i = 0; i < m_num_bitstreams; ++i)
    {
        std::fill(m_bitstreams.at(i).begin(), m_bitstreams.at(i).end(), 0);
        m_writers.at(i)->SetPosition(0);
    }
}