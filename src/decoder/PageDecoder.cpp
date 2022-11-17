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

#include "PageDecoder.h"

using namespace BrotliG;

PageDecoder::PageDecoder()
    : m_state(nullptr)
{   
}

PageDecoder::~PageDecoder()
{
    Cleanup();
}

bool PageDecoder::Setup(
    const uint8_t* input, 
    size_t input_size,
    void* params,
    uint32_t flags, 
    uint32_t extra)
{
    assert(extra != 0);

    if (m_state != nullptr)
        Cleanup();

    m_input = input;
    m_inputSize = input_size;
    m_state = new BrotligDecoderState(static_cast<BrotligDecoderParams*>(params));

    m_outputSize = extra;
    m_output = new uint8_t[m_outputSize];

    return true;
}

bool PageDecoder::Run()
{
    if (m_outputSize == m_inputSize)
    {
        return WriteUncompressedMetaBlock();
    }
    else
    {
        m_state->br->Initialize(m_input, m_inputSize, 0);

        ReadMetaBlockHeader();

        return DecodeCompressedMetaBlock();
    }
}

void PageDecoder::Cleanup()
{
    delete m_state;
    m_state = nullptr;

    m_input = nullptr;
    m_output = nullptr;
}

void PageDecoder::ReadMetaBlockHeader()
{
    uint32_t bitsread = 0;
#if LGWIN_FIELD
    m_state->params->lgwin = static_cast<int>(m_state->br->ReadAndConsume(5)); bitsread += 5;
#endif // LGWIN_FIELD

#if ISLAST_FLAG
    m_state->isLast = static_cast<bool>(m_state->br->ReadAndConsume(1)); bitsread += 1;
#endif

#if ISEMPTY_FLAG
    if (m_state->isLast)
    {
        m_state->isEmpty = static_cast<bool>(m_state->br->ReadAndConsume(1)); bitsread += 1;
    }
#endif

#if ISUNCOMPRESSED_FLAG
    if (!m_state->isLast)
    {
        m_state->isUncompressed = static_cast<bool>(m_state->br->ReadAndConsume(1)); bitsread += 1;
    }
#endif

#if DIST_POSTFIX_BITS_FIELD
    m_state->params->distance_postfix_bits = m_state->br->ReadAndConsume(2); bitsread += 2;
#endif

#if NUM_DIRECT_DIST_CODES_FIELD
    uint32_t ndbits = m_state->br->ReadAndConsume(4); bitsread += 4;
    m_state->params->num_direct_distance_codes = ndbits << m_state->params->distance_postfix_bits;
#endif

#if RESERVE_BITS
    m_state->br->ReadAndConsume(2); bitsread += 2;
#endif

#if PAD_HEADER
    m_state->br->Consume(32 - bitsread);
    bitsread = 0;
#endif

#if UNCOMPLEN_FIELD
    uint32_t nibblebits = m_state->br->ReadAndConsume(2); bitsread += 2;
    uint32_t mnibbles = nibblebits + 4;
    uint32_t nlenbits = mnibbles * 4;
    m_state->uncompLen = static_cast<size_t>(m_state->br->ReadAndConsume(nlenbits)) + 1; bitsread += nlenbits;
#endif

#if PAD_HEADER
    m_state->br->Consume(32 - bitsread);
#endif
} 

bool PageDecoder::WriteUncompressedMetaBlock()
{
    memcpy(m_output, m_input, m_outputSize);

    return true;
}

bool PageDecoder::DecodeCompressedMetaBlock()
{
    BrotligDeswizzler* deswizzler = new BrotligDeswizzler(
        m_state->br,
        m_inputSize,
        m_state->params->num_bitstreams,
        m_state->params->swizzle_size
    );

    deswizzler->DeserializeCompact();

    LoadSymbolTables(deswizzler);

    DecodeCommands(deswizzler);

    delete deswizzler;
    return true;
}

void PageDecoder::LoadSymbolTables(BrotligDeswizzler* deswizzler)
{
    m_state->symbolTrees.push_back(BrotligHuffmanTree::Load(deswizzler, BROLTIG_NUM_COMMAND_SYMBOLS_EFFECTIVE, true));
    m_state->symbolTrees.push_back(BrotligHuffmanTree::Load(deswizzler, BROTLIG_NUM_DISTANCE_SYMBOLS));
    m_state->symbolTrees.push_back(BrotligHuffmanTree::Load(deswizzler, BROTLI_NUM_LITERAL_SYMBOLS, true));
}

void PageDecoder::DecodeCommands(BrotligDeswizzler* deswizzler)
{
#if REDISTRIBUTE_LITERALS
    DecodeCommandsWithLitDist(deswizzler);
#else
    DecodeCommandsNoLitDist(deswizzler);
#endif // REDISTRIBUTE_LITERALS
}

void PageDecoder::DecodeCommandsWithLitDist(BrotligDeswizzler* deswizzler)
{
    BrotligCommandDecoder* decoder = new BrotligCommandDecoder(m_state, deswizzler);

    memset(m_output, 0, m_outputSize);

    // Collect all the commands and distances for the round
        // i.e. till either we have read all num_bitstreams
        // or we encounter the sentinel
    // compute litcount
    // compute effective litcount
    // decode effective litcount literals and add it to the list
    // Now process commands but fetch literals from the list for inserts

    bool foundSentinel = false;
    size_t curBitstreamIndex = 0;
    std::queue<BrotligCommand> rCmds;
    std::vector<uint8_t> litQueue;
    size_t litcount = 0;
    size_t curoutpos = 0;
    size_t prev_tail = 0;
    size_t effNum_bitstreams = 1;
    while (!foundSentinel)
    {
        uint32_t wptr = (uint32_t)curoutpos;

        litcount = 0;
        curBitstreamIndex = 0;
        effNum_bitstreams = 0;

        // Collect all the commands and distances for the current round till
        // either we have read all num_bitstreams
        // or we encounter the sentinel
        while (curBitstreamIndex != m_state->params->num_bitstreams)
        {
            BrotligCommand cmd = decoder->DecodeLengths();

            if (cmd.insert_len == 0 && cmd.copy_len == 0)
            {
                foundSentinel = true;
                break;
            }
            cmd.insert_pos = static_cast<uint32_t>(curoutpos);
            litcount += cmd.insert_len;
            curoutpos += static_cast<size_t>(cmd.insert_len + cmd.copy_len);

            if (cmd.cmd_prefix >= 128 && cmd.cmd_prefix <= BROTLI_NUM_COMMAND_SYMBOLS)
                cmd.dist_code = decoder->DecodeDistance();
            else
                cmd.dist_code = 0;

            decoder->TranslateDistance(cmd);

            rCmds.push(cmd);
            ++curBitstreamIndex;
            ++effNum_bitstreams;
            decoder->SwitchStream();
        }

        // Compute effective litcount "rlitcount"
        size_t aclitcount = (litcount > prev_tail) ? litcount - prev_tail : 0;
        size_t mult = (effNum_bitstreams != 0) ? (aclitcount + effNum_bitstreams - 1) / effNum_bitstreams : 0;
        size_t rlitcount = effNum_bitstreams * mult;
        size_t totalcnt = rlitcount + prev_tail;
        prev_tail = totalcnt - litcount;

        // Decoded "rlitcount" literals and add it to the queue
        decoder->Reset();
        while (rlitcount != 0)
        {
            litQueue.push_back(decoder->DecodeLiteral());
            --rlitcount;
            decoder->SwitchStream();
        }

        // Process commands
        while (rCmds.size() > 0)
        {
            BrotligCommand cmd = rCmds.front();
            size_t curWrPos = cmd.insert_pos;

            if (cmd.insert_len > 0)
            {
                if (cmd.insert_len <= BROTLIG_MEMCOPY_BYTES_LIMIT)
                {
                    size_t insertLen = cmd.insert_len;
                    size_t litIndex = 0;
                    while (insertLen > 0)
                    {
                        m_output[curWrPos + litIndex] = litQueue[litIndex];
                        ++litIndex;
                        --insertLen;
                    }
                }
                else
                {
                    memcpy(&m_output[curWrPos], litQueue.data(), cmd.insert_len);
                }

                litQueue.erase(litQueue.begin(), litQueue.begin() + cmd.insert_len);
                curWrPos += cmd.insert_len;
            }
            

            if (cmd.copy_len > 0)
            {
                if (cmd.copy_len <= BROTLIG_MEMCOPY_BYTES_LIMIT)
                {
                    for (size_t i = 0; i < cmd.copy_len; ++i)
                    {
                        if (curWrPos < m_outputSize) m_output[curWrPos] = m_output[curWrPos - cmd.dist];
                        ++curWrPos;
                    }
                }
                else
                {
                    size_t copyLen = cmd.copy_len;
                    size_t curCopyPos = curWrPos - cmd.dist;
                    size_t effCopyLen = 0;
                    while (copyLen > 0)
                    {
                        effCopyLen = (curWrPos - curCopyPos < copyLen) ? curWrPos - curCopyPos : copyLen;
                        memcpy(&m_output[curWrPos], &m_output[curCopyPos], effCopyLen);
                        curWrPos += effCopyLen;
                        curCopyPos += effCopyLen;
                        copyLen -= effCopyLen;
                    }
                }
            }

            rCmds.pop();
        }
    }
}

void PageDecoder::DecodeCommandsNoLitDist(BrotligDeswizzler* deswizzler)
{
    BrotligCommandDecoder* decoder = new BrotligCommandDecoder(m_state, deswizzler);

    memset(m_output, 0, m_outputSize);

    size_t curoutpos = 0;
    while (curoutpos < m_outputSize)
    {
        BrotligCommand cmd = decoder->DecodeLengths();
        cmd.insert_pos = static_cast<uint32_t>(curoutpos);

        uint32_t numlits = cmd.insert_len;
        while (numlits > 0)
        {
            uint8_t byte = decoder->DecodeLiteral();

            if(curoutpos < m_outputSize) m_output[curoutpos] = byte;
            --numlits;
            ++curoutpos;
        }

        if (cmd.cmd_prefix >= 128 && cmd.cmd_prefix <= BROTLI_NUM_COMMAND_SYMBOLS)
            cmd.dist_code = decoder->DecodeDistance();
        else
            cmd.dist_code = 0;

        decoder->TranslateDistance(cmd);

        for (size_t i = 0; i < cmd.copy_len; ++i)
        {
            assert(curoutpos >= cmd.dist);
            if (curoutpos >= cmd.dist)
            {
                uint8_t byte = m_output[curoutpos - cmd.dist];
                if (curoutpos < m_outputSize) m_output[curoutpos] = byte;
            }
            
            ++curoutpos;
        }

        decoder->SwitchStream();
    }

    delete decoder;
}
