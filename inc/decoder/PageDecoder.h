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

#include "common/BrotligDeswizzler.h"
#include "common/BrotligCommandLut.h"
#include "common/BrotligMultithreading.h"

#include "DataStream.h"
#include "BrotligDecoderState.h"

namespace BrotliG
{
    class BrotligCommandDecoder
    {
    public:
        BrotligCommandDecoder(BrotligDecoderState* decoderState, BrotligDeswizzler* deswizzler)
        {
            m_decoderState = decoderState;
            m_deswizzler = deswizzler;
        }

        ~BrotligCommandDecoder()
        {
            m_decoderState = nullptr;
            m_deswizzler = nullptr;
        }

        BrotligCommand DecodeLengths()
        {
            BrotligCommand cmd;
            size_t codelen = 0;
            cmd.cmd_prefix = DecodeICP(m_deswizzler->ReadNoConsume(16), codelen);
            m_deswizzler->Consume(static_cast<uint32_t>(codelen));
            if (cmd.cmd_prefix <= BROTLI_NUM_COMMAND_SYMBOLS)
            {
                BrotligCmdLutElement clut = sBrotligCmdLut[cmd.cmd_prefix];
                cmd.insert_len = clut.insert_len_offset;
                if (clut.insert_len_extra_bits != 0)
                    cmd.insert_len += m_deswizzler->ReadAndConsume(clut.insert_len_extra_bits);
                cmd.copy_len = m_deswizzler->ReadAndConsume(clut.copy_len_extra_bits);
                cmd.copy_len += clut.copy_len_offset;
                cmd.dist_code = clut.distance_code;
            }
            else
            {
                uint16_t insert_code = cmd.cmd_prefix - BROTLI_NUM_COMMAND_SYMBOLS;
                uint32_t insnumextra = GetInsertExtra(insert_code);
                uint32_t insert_base = GetInsertBase(insert_code);
                uint32_t insert_extra_val = m_deswizzler->ReadAndConsume(insnumextra);
                cmd.insert_len = insert_base + insert_extra_val;
                cmd.copy_len = 0;
                cmd.dist_code = 0;
            }

            return cmd;
        }

        uint8_t DecodeLiteral()
        {
            uint16_t lsymbol = 0;
            size_t codelen = 0;
            lsymbol = DecodeLit(m_deswizzler->ReadNoConsume(16), codelen);
            m_deswizzler->Consume(static_cast<uint32_t>(codelen));

            return (uint8_t)lsymbol;
        }

        uint8_t DecodeNFetchLiteral(uint16_t& code, size_t& codelen)
        {
            uint16_t lsymbol = 0;
            code = static_cast<uint16_t>(m_deswizzler->ReadNoConsume(16));
            lsymbol = DecodeLit(code, codelen);
            m_deswizzler->Consume(static_cast<uint32_t>(codelen));

            return (uint8_t)lsymbol;
        }

        uint32_t DecodeDistance()
        {
            uint16_t dsymbol = 0;
            size_t codelen = 0;
            dsymbol = DecodeDist(m_deswizzler->ReadNoConsume(16), codelen);
            m_deswizzler->Consume(static_cast<uint32_t>(codelen));

            return dsymbol;
        }

        void TranslateDistance(BrotligCommand& cmd)
        {
            uint32_t dist = 0;
            uint32_t ndistbits = 0;
            uint32_t dist_code = static_cast<uint32_t>(cmd.dist_code);
            switch (dist_code)
            {
            case  0: dist = m_decoderState->distring[0];		break;
            case  1: dist = m_decoderState->distring[1];		break;
            case  2: dist = m_decoderState->distring[2];		break;
            case  3: dist = m_decoderState->distring[3];		break;
            case  4: dist = m_decoderState->distring[0] - 1;	break;
            case  5: dist = m_decoderState->distring[0] + 1;	break;
            case  6: dist = m_decoderState->distring[0] - 2;	break;
            case  7: dist = m_decoderState->distring[0] + 2;	break;
            case  8: dist = m_decoderState->distring[0] - 3;	break;
            case  9: dist = m_decoderState->distring[0] + 3;	break;
            case 10: dist = m_decoderState->distring[1] - 1;	break;
            case 11: dist = m_decoderState->distring[1] + 1;    break;
            case 12: dist = m_decoderState->distring[1] - 2;    break;
            case 13: dist = m_decoderState->distring[1] + 2;    break;
            case 14: dist = m_decoderState->distring[1] - 3;    break;
            case 15: dist = m_decoderState->distring[1] + 3;    break;
            default:
            {
                if (m_decoderState->params->num_direct_distance_codes > 0
                    && dist_code < 16 + m_decoderState->params->num_direct_distance_codes)
                {
                    dist = dist_code - 15;
                }
                else
                {
                    ndistbits = 1 +
                        ((dist_code - m_decoderState->params->num_direct_distance_codes - 16)
                            >> (m_decoderState->params->distance_postfix_bits + 1));

                    cmd.dist_extra = m_deswizzler->ReadAndConsume(ndistbits);

                    uint32_t hcode = (dist_code - m_decoderState->params->num_direct_distance_codes - 16)
                        >> m_decoderState->params->distance_postfix_bits;

                    uint32_t lcode = (dist_code - m_decoderState->params->num_direct_distance_codes - 16)
                        & Mask32(m_decoderState->params->distance_postfix_bits);

                    uint32_t offset = ((2 + (hcode & 1)) << ndistbits) - 4;
                    dist = ((offset + cmd.dist_extra) << m_decoderState->params->distance_postfix_bits)
                        + lcode
                        + m_decoderState->params->num_direct_distance_codes
                        + 1;
                }
            }
            }

            cmd.dist_prefix = ndistbits << 10;
            cmd.dist_prefix |= dist_code;

            cmd.dist = dist;

            if (dist_code > 0)
            {
                m_decoderState->distring[3] = m_decoderState->distring[2];
                m_decoderState->distring[2] = m_decoderState->distring[1];
                m_decoderState->distring[1] = m_decoderState->distring[0];
                m_decoderState->distring[0] = dist;
            }
        }

        void SwitchStream()
        {
            m_deswizzler->BSSwitch();
        }

        void Reset()
        {
            m_deswizzler->Reset();
        }

    private:
        BrotligDecoderState* m_decoderState;
        BrotligDeswizzler* m_deswizzler;

        uint16_t DecodeLit(uint16_t bits, size_t& codelen)
        {
            return m_decoderState->symbolTrees[2]->Symbol(BrotligReverse16Bits(bits), codelen);
        }

        uint16_t DecodeICP(uint16_t bits, size_t& codelen)
        {
            return m_decoderState->symbolTrees[0]->Symbol(BrotligReverse16Bits(bits), codelen);
        }

        uint16_t DecodeDist(uint16_t bits, size_t& codelen)
        {
            return m_decoderState->symbolTrees[1]->Symbol(BrotligReverse16Bits(bits), codelen);
        }
    };

    class PageDecoder : public BrotligWorker
    {
    public:

        PageDecoder();
        ~PageDecoder();

        bool Setup(const uint8_t* input, size_t input_size, void* params, uint32_t flags, uint32_t extra);
        bool Run();
        void Cleanup();

    private:
        BrotligDecoderState* m_state;

        void ReadMetaBlockHeader();
        bool WriteUncompressedMetaBlock();
        bool DecodeCompressedMetaBlock();
        void LoadSymbolTables(BrotligDeswizzler* deswizzler);
        void DecodeCommands(BrotligDeswizzler* deswizzler);
        void DecodeCommandsWithLitDist(BrotligDeswizzler* deswizzler);
        void DecodeCommandsNoLitDist(BrotligDeswizzler* deswizzler);
    };
}
