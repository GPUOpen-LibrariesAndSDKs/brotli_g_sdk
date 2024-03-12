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

#include "common/BrotligBitReader.h"
#include "common/BrotligDataConditioner.h"

#include "decoder/BrotligHuffmanTable.h"

#include "PageDecoder.h"

using namespace BrotliG;

#define OVERLAP(x1, x2, y1, y2) (x1 < y2 && y1 < x2)

PageDecoder::PageDecoder()
{
    for (size_t i = 0; i < BROTLIG_NUM_HUFFMAN_TREES; ++i)
    {
        m_symbols[i] = nullptr;
        m_codelens[i] = nullptr;
    }
    
    m_distring[0] = m_distring[1] = m_distring[2] = m_distring[3] = 0;
}

PageDecoder::~PageDecoder()
{
    Cleanup();
}

bool PageDecoder::Setup(const BrotligDecoderParams& params, const BrotligDataconditionParams& dcParams)
{
    m_params = params;
    m_dcparams = dcParams;

    m_pReader.Initialize(
        m_params.num_bitstreams
    );

    for (size_t i = 0; i < BROTLIG_NUM_HUFFMAN_TREES; ++i)
    {
        m_symbols[i] = new uint16_t[BROTLIG_HUFFMAN_TABLE_SIZE];
        m_codelens[i] = new uint16_t[BROTLIG_HUFFMAN_TABLE_SIZE];
    }

    return true;
}

bool PageDecoder::Run(const uint8_t* input, size_t inputSize, size_t inputOffset, uint8_t* output, size_t outputSize, size_t outputOffset)
{
    const uint8_t* p_inPtr = input + inputOffset;
    uint8_t* p_outPtr = output + outputOffset;

    if (outputSize == inputSize)
    {
        if (m_dcparams.precondition)
            p_outPtr = new uint8_t[outputSize];

        memcpy(p_outPtr, p_inPtr, outputSize);
    }
    else
    {
        BrotligBitReaderLSB br;
        br.Initialize(p_inPtr, inputSize);

        // Read page header
        m_params.distance_postfix_bits = br.ReadAndConsume(BROTLIG_PAGE_HEADER_NPOSTFIX_BITS);
        uint32_t ndbits = br.ReadAndConsume(BROTLIG_PAGE_HEADER_NDIST_BITS);
        m_params.num_direct_distance_codes = ndbits << m_params.distance_postfix_bits;

        bool isDelta_Encoded = static_cast<bool>(br.ReadAndConsume(BROTLIG_PAGE_HEADER_ISDELTAENCODED_BITS));
        isDelta_Encoded &= (m_dcparams.precondition);
        br.Consume(1);

        size_t compressedOffsetBits = BROTLIG_PAGE_HEADER_SIZE_BITS;

        if (m_dcparams.precondition)
        {
            p_outPtr = new uint8_t[outputSize];
        }

        // Read bitstream size offset table and bitstreams
        // Compute base size in bits
        uint32_t rAvgBSSizeInBytes = static_cast<uint32_t>((inputSize + (m_params.num_bitstreams - 1)) / m_params.num_bitstreams);
        uint32_t baseSizeBits = Log2FloorNonZero(rAvgBSSizeInBytes) + 1;

        // Delta size in bits
        uint32_t logSize = Log2FloorNonZero(inputSize - 1) + 1;
        uint32_t deltaBitsSizeBits = Log2FloorNonZero(logSize) + 1;

        // Read base size and delta size size
        uint32_t baseSize = br.ReadAndConsume(baseSizeBits);
        uint32_t deltaSizeBits = br.ReadAndConsume(deltaBitsSizeBits);
        compressedOffsetBits += (baseSizeBits + deltaBitsSizeBits + (uint32_t)m_params.num_bitstreams * deltaSizeBits);
        compressedOffsetBits = ((compressedOffsetBits + BROTLIG_DWORD_SIZE_BITS - 1) / BROTLIG_DWORD_SIZE_BITS) * BROTLIG_DWORD_SIZE_BITS;
        size_t inIndex = compressedOffsetBits / 8;

        // read delta size of each bitstream and initialize bitstream reader for each stream
        for (size_t i = 0; i < m_params.num_bitstreams; ++i)
        {
            uint32_t delta = br.ReadAndConsume(deltaSizeBits);
            uint32_t bslength = baseSize + delta;
            m_pReader.SetReader(i, &p_inPtr[inIndex]);
            inIndex += bslength;
        }

        m_pReader.BSReset();

        // Load insert-and-copy lengths huffman table
        LoadHuffmanTable(
            m_pReader,
            BROLTIG_NUM_COMMAND_SYMBOLS_EFFECTIVE,
            m_symbols[BROTLIG_ICP_TREE_INDEX],
            m_codelens[BROTLIG_ICP_TREE_INDEX]
        );

        // Load distance huffman table
        LoadHuffmanTable(
            m_pReader,
            BROTLIG_NUM_DISTANCE_SYMBOLS,
            m_symbols[BROTLIG_DIST_TREE_INDEX],
            m_codelens[BROTLIG_DIST_TREE_INDEX]
        );

        // Load literal huffman table
        LoadHuffmanTable(
            m_pReader,
            BROTLI_NUM_LITERAL_SYMBOLS,
            m_symbols[BROTLIG_LIT_TREE_INDEX],
            m_codelens[BROTLIG_LIT_TREE_INDEX]
        );

        // Initialize distance ring buffer
        m_distring[0] = 4;
        m_distring[1] = 11;
        m_distring[2] = 15;
        m_distring[3] = 16;

        // Prepare the output
        memset(p_outPtr, 0, outputSize);

        // Decode compressed stream
        bool foundSentinel = false;
        BrotligCommand cmdQueue[BROTLIG_MAX_NUM_BITSTREAMS];
        BrotligCommand* cqfront = cmdQueue;
        BrotligCommand* cqback = cmdQueue;

        uint8_t* litQueue = new uint8_t[m_params.page_size];
        uint8_t* lqfront = litQueue;
        uint8_t* lqback = litQueue;

        uint32_t bs_processed = 0, num_bitstreams = (uint32_t)m_params.num_bitstreams, prev_tail = 0, litcount = 0, aclitcount = 0, mult = 0, rlitcount = 0;
        uint8_t* wPtr = p_outPtr;
        uint8_t* cPtr = nullptr;

        BrotligCommand cmd = {};

        while (!foundSentinel)
        {
            litcount = 0;
            bs_processed = 0;

            // Decode all the commands for the current round
            while (bs_processed != num_bitstreams)
            {
                if (DecodeCommand(cmd))
                {
                    foundSentinel = true;
                    break;
                }

                litcount += cmd.insert_len;
                *cqback++ = cmd;
                ++bs_processed;
                m_pReader.BSSwitch();
            }
            m_pReader.BSReset();

            // Compute the number of literals to decode in this round
            aclitcount = (litcount > prev_tail) ? litcount - prev_tail : 0;
            mult = (bs_processed != 0) ? (aclitcount + bs_processed - 1) / bs_processed : 0;
            rlitcount = bs_processed * mult;
            prev_tail = rlitcount + prev_tail - litcount;

            // Decode all the literals for the current round
            while (rlitcount--)
            {
                *lqback++ = DecodeLiteral();
                m_pReader.BSSwitch();
            }

            // Process inserts and copies
            while (cqfront != cqback)
            {
                cmd = *cqfront++;
                uint32_t toinsert = (cmd.insert_len / 4) * 4;
                if (toinsert > 0) {
                    memcpy(wPtr, lqfront, toinsert);
                    wPtr += toinsert;
                    lqfront += toinsert;
                    cmd.insert_len -= toinsert;
                }

                while (cmd.insert_len--) *wPtr++ = *lqfront++;

                cPtr = wPtr - cmd.dist;

                uint32_t tocopy = (cmd.copy_len / 4) * 4;
                while (tocopy > 0 && cPtr + tocopy < wPtr) {
                    memcpy(wPtr, cPtr, tocopy);
                    cPtr += tocopy;
                    wPtr += tocopy;
                    cmd.copy_len -= tocopy;
                    tocopy = (cmd.copy_len / 4) * 4;
                }
                while (cmd.copy_len--) *wPtr++ = *cPtr++;
            }

            cqfront = cqback = cmdQueue;
        }

        delete[] litQueue;

        if (isDelta_Encoded) DeltaDecode(outputOffset, outputOffset + outputSize, p_outPtr);
    }

    if (m_dcparams.precondition && (outputOffset < (m_dcparams.tNumBlocks * m_dcparams.blockSizeBytes)))
    {
        uint32_t tTexSize = m_dcparams.tNumBlocks * m_dcparams.blockSizeBytes, mip = 0, sub = 0;
        while (outputOffset >= m_dcparams.subStreamOffsets[sub + 1]) ++sub;

        size_t outindex = 0, index = 0, offsetIndex = 0;
        while (index < outputSize)
        {
            offsetIndex = index + outputOffset - m_dcparams.subStreamOffsets[sub];
            outindex = DeconditionBC1_5(static_cast<uint32_t>(offsetIndex), sub);

            output[outindex] = p_outPtr[index++];


            if (outputOffset + index >= tTexSize)
                break;

            if (outputOffset + index >= m_dcparams.subStreamOffsets[sub + 1])
                sub++;
        }

        delete[] p_outPtr;
    }

    return true;
}

void PageDecoder::Cleanup()
{
    for (size_t i = 0; i < BROTLIG_NUM_HUFFMAN_TREES; ++i)
    {
        if (m_symbols[i] != nullptr)
        {
            delete m_symbols[i];
            m_symbols[i] = nullptr;
        }

        if (m_codelens[i] != nullptr)
        {
            delete m_codelens[i];
            m_codelens[i] = nullptr;
        }
    }

    m_distring[0] = m_distring[1] = m_distring[2] = m_distring[3] = 0;
}

bool PageDecoder::DecodeCommand(BrotligCommand& cmd)
{
    uint16_t bits = sBrotligReverseBits15[m_pReader.ReadNoConsume15()];
    m_pReader.Consume(m_codelens[BROTLIG_ICP_TREE_INDEX][bits]);
    cmd.cmd_prefix = m_symbols[BROTLIG_ICP_TREE_INDEX][bits];

    if (cmd.cmd_prefix <= BROTLI_NUM_COMMAND_SYMBOLS)
    {
        BrotligCmdLutElement clut = sBrotligCmdLut[cmd.cmd_prefix];
        cmd.insert_len = clut.insert_len_offset;
        cmd.copy_len = clut.copy_len_offset;
        if (cmd.insert_len == 0 && cmd.copy_len == 0)
            return true;
        cmd.insert_len += m_pReader.ReadAndConsume(clut.insert_len_extra_bits);
        cmd.copy_len += m_pReader.ReadAndConsume(clut.copy_len_extra_bits);
        cmd.dist_code = (cmd.cmd_prefix >= 128) ? DecodeDistance() : 0;
        TranslateDistance(cmd);
    }
    else
    {
        uint16_t insert_code = cmd.cmd_prefix - BROTLI_NUM_COMMAND_SYMBOLS;
        uint32_t insnumextra = GetInsertExtra(insert_code);
        uint32_t insert_base = GetInsertBase(insert_code);
        uint32_t insert_extra_val = m_pReader.ReadAndConsume(insnumextra);
        cmd.insert_len = insert_base + insert_extra_val;
        cmd.copy_len = 0;
        cmd.dist_code = 0;
    }

    return false;
}

uint8_t PageDecoder::DecodeLiteral()
{
    uint16_t bits = sBrotligReverseBits15[m_pReader.ReadNoConsume15()];
    m_pReader.Consume(m_codelens[BROTLIG_LIT_TREE_INDEX][bits]);
    return (uint8_t)m_symbols[BROTLIG_LIT_TREE_INDEX][bits];
}

uint8_t PageDecoder::DecodeNFetchLiteral(uint16_t& code, size_t& codelen)
{
    code = static_cast<uint16_t>(m_pReader.ReadNoConsume15());
    uint16_t bits = BrotligReverseBits15(code);
    uint16_t lsymbol = m_symbols[BROTLIG_LIT_TREE_INDEX][bits];
    m_pReader.Consume(m_codelens[BROTLIG_LIT_TREE_INDEX][bits]);
    return (uint8_t)lsymbol;
}

uint32_t PageDecoder::DecodeDistance()
{
    uint16_t bits = BrotligReverseBits15(m_pReader.ReadNoConsume15());
    m_pReader.Consume(m_codelens[BROTLIG_DIST_TREE_INDEX][bits]);
    return m_symbols[BROTLIG_DIST_TREE_INDEX][bits];
}

void PageDecoder::TranslateDistance(BrotligCommand& cmd)
{
    uint32_t ndistbits = 0;
    uint32_t dist_code = static_cast<uint32_t>(cmd.dist_code);
    switch (dist_code)
    {
    case  0: cmd.dist = m_distring[0];		break;
    case  1: cmd.dist = m_distring[1];		break;
    case  2: cmd.dist = m_distring[2];		break;
    case  3: cmd.dist = m_distring[3];		break;
    case  4: cmd.dist = m_distring[0] - 1;	break;
    case  5: cmd.dist = m_distring[0] + 1;	break;
    case  6: cmd.dist = m_distring[0] - 2;	break;
    case  7: cmd.dist = m_distring[0] + 2;	break;
    case  8: cmd.dist = m_distring[0] - 3;	break;
    case  9: cmd.dist = m_distring[0] + 3;	break;
    case 10: cmd.dist = m_distring[1] - 1;	break;
    case 11: cmd.dist = m_distring[1] + 1;  break;
    case 12: cmd.dist = m_distring[1] - 2;  break;
    case 13: cmd.dist = m_distring[1] + 2;  break;
    case 14: cmd.dist = m_distring[1] - 3;  break;
    case 15: cmd.dist = m_distring[1] + 3;  break;
    default:
    {
        if (m_params.num_direct_distance_codes > 0
            && dist_code < 16 + m_params.num_direct_distance_codes)
        {
            cmd.dist = dist_code - 15;
        }
        else
        {
            ndistbits = 1 +
                ((dist_code - m_params.num_direct_distance_codes - 16)
                    >> (m_params.distance_postfix_bits + 1));

            cmd.dist_extra = m_pReader.ReadAndConsume(ndistbits);

            uint32_t hcode = (dist_code - m_params.num_direct_distance_codes - 16)
                >> m_params.distance_postfix_bits;

            uint32_t lcode = (dist_code - m_params.num_direct_distance_codes - 16)
                & Mask32(m_params.distance_postfix_bits);

            uint32_t offset = ((2 + (hcode & 1)) << ndistbits) - 4;
            cmd.dist = ((offset + cmd.dist_extra) << m_params.distance_postfix_bits)
                + lcode
                + m_params.num_direct_distance_codes
                + 1;
        }
    }
    }

    if (dist_code > 0)
    {
        m_distring[3] = m_distring[2];
        m_distring[2] = m_distring[1];
        m_distring[1] = m_distring[0];
        m_distring[0] = cmd.dist;
    }
}

uint32_t PageDecoder::DeconditionBC1_5(uint32_t offsetAddr, uint32_t sub)
{    
    uint32_t adjAddr = offsetAddr, mip = 0;
    while (adjAddr >= m_dcparams.mipOffsetBlocks[mip + 1] * m_dcparams.subBlockSizes[sub]) ++mip;
    adjAddr -= m_dcparams.mipOffsetBlocks[mip] * m_dcparams.subBlockSizes[sub];

    uint32_t block = adjAddr / m_dcparams.subBlockSizes[sub];
    uint32_t row = block / m_dcparams.widthInBlocks[mip];
    uint32_t col = block % m_dcparams.widthInBlocks[mip];

    bool isMipSwizzled = m_dcparams.swizzle && (m_dcparams.widthInBlocks[mip] >= BROTLIG_PRECON_SWIZZLE_REGION_SIZE && m_dcparams.heightInBlocks[mip] >= BROTLIG_PRECON_SWIZZLE_REGION_SIZE);
    uint32_t remWidthInBlocks = m_dcparams.widthInBlocks[mip] % BROTLIG_PRECON_SWIZZLE_REGION_SIZE, remHeightInBlocks = m_dcparams.heightInBlocks[mip] % BROTLIG_PRECON_SWIZZLE_REGION_SIZE;
    uint32_t effWidthInBlocks = m_dcparams.widthInBlocks[mip] - remWidthInBlocks, effHeightInBlocks = m_dcparams.heightInBlocks[mip] - remHeightInBlocks;

    if (isMipSwizzled && (row < effHeightInBlocks && col < effWidthInBlocks))
    {
        uint32_t effBlock = block - (row * remWidthInBlocks);
        uint32_t widthInBlockGrps = effWidthInBlocks / BROTLIG_PRECON_SWIZZLE_REGION_SIZE;

        uint32_t blockGrp = effBlock / (BROTLIG_PRECON_SWIZZLE_REGION_SIZE * BROTLIG_PRECON_SWIZZLE_REGION_SIZE);
        uint32_t blockInGrp = effBlock % (BROTLIG_PRECON_SWIZZLE_REGION_SIZE * BROTLIG_PRECON_SWIZZLE_REGION_SIZE);

        uint32_t oblockGroupRow = blockGrp / widthInBlockGrps;
        uint32_t oblockGroupCol = blockGrp % widthInBlockGrps;

        uint32_t oblockRowInGroup = blockInGrp / BROTLIG_PRECON_SWIZZLE_REGION_SIZE;
        uint32_t oblockColInGroup = blockInGrp % BROTLIG_PRECON_SWIZZLE_REGION_SIZE;

        row = BROTLIG_PRECON_SWIZZLE_REGION_SIZE * oblockGroupRow + oblockRowInGroup;
        col = BROTLIG_PRECON_SWIZZLE_REGION_SIZE * oblockGroupCol + oblockColInGroup;
    }

    uint32_t mipPos = m_dcparams.mipOffsetsBytes[mip];
    uint32_t blockPos = (row * m_dcparams.pitchInBytes[mip]) + (col * m_dcparams.blockSizeBytes);
    uint32_t subblockPos = m_dcparams.subBlockOffsets[sub];
    uint32_t bytePos = (adjAddr % m_dcparams.subBlockSizes[sub]);

    return  (mipPos + blockPos + subblockPos + bytePos);
}

void PageDecoder::DeltaDecode(size_t page_start, size_t page_end, uint8_t* data)
{
    uint32_t sub = 0, refIdx = 0;;
    size_t color_start = 0, color_end = 0, p_sub_start = 0, p_sub_end = 0, p_sub_size = 0;
    for (uint32_t i = 0; i < m_dcparams.numColorSubBlocks; ++i)
    {
        sub = m_dcparams.colorSubBlocks[i];
        color_start = (size_t)m_dcparams.subStreamOffsets[sub];
        color_end = (size_t)m_dcparams.subStreamOffsets[sub + 1];

        if (OVERLAP(color_start, color_end, page_start, page_end))
        {
            p_sub_start = (color_start > page_start) ? color_start - page_start : 0;
            p_sub_end = (color_end < page_end) ? color_end - page_start : page_end - page_start;
            p_sub_size = p_sub_end - p_sub_start;

            DeltaDecodeByte(p_sub_size, data + p_sub_start);
        }
    }
}

void PageDecoder::DeltaDecodeByte(size_t inSize, uint8_t* inData)
{
    for (size_t el = 1; el < inSize; ++el)
        inData[el] += inData[el - 1];
}
