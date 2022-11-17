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

extern "C" {
#include "brotli/c/enc/utf8_util.h"
#include "brotli/c/enc/backward_references_hq.h"
#include "brotli/c/common/dictionary.h"
#include "brotli/c/enc/dictionary_hash.h"
}

#include "common/BrotligUtils.h"

#include "BrotligMetaBlock.h"
#include "PageEncoder.h"

using namespace BrotliG;

static inline void ToBrotliEncoderParams(BrotligEncoderParams* in, BrotliEncoderParams* out)
{
    out->mode = in->mode;
    out->quality = in->quality;
    out->lgwin = in->lgwin;
    out->lgblock = in->lgblock;
    out->stream_offset = 0;
    out->size_hint = in->size_hint;
    out->disable_literal_context_modeling = in->disable_literal_context_modeling;
    out->large_window = in->large_window;
    out->hasher.type = in->hasher_params.type;
    out->hasher.bucket_bits = in->hasher_params.bucket_bits;
    out->hasher.block_bits = in->hasher_params.block_bits;
    out->hasher.hash_len = in->hasher_params.hash_len;
    out->hasher.num_last_distances_to_check = in->hasher_params.num_last_distances_to_check;
    out->dist.distance_postfix_bits = in->dist_params.distance_postfix_bits;
    out->dist.num_direct_distance_codes = in->dist_params.num_direct_distance_codes;
    out->dist.alphabet_size_max = in->dist_params.alphabet_size_max;
    out->dist.alphabet_size_limit = in->dist_params.alphabet_size_limit;
    out->dist.max_distance = in->dist_params.max_distance;
    out->dictionary = in->dictionary;
}

static inline void ToBrotligEncoderParams(BrotliEncoderParams* in, BrotligEncoderParams* out)
{
    out->mode = in->mode;
    out->quality = in->quality;
    out->lgwin = in->lgwin;
    out->lgblock = in->lgblock;
    out->size_hint = in->size_hint;
    out->disable_literal_context_modeling = in->disable_literal_context_modeling;
    out->large_window = in->large_window;
    out->hasher_params.type = in->hasher.type;
    out->hasher_params.bucket_bits = in->hasher.bucket_bits;
    out->hasher_params.block_bits = in->hasher.block_bits;
    out->hasher_params.hash_len = in->hasher.hash_len;
    out->hasher_params.num_last_distances_to_check = in->hasher.num_last_distances_to_check;
    out->dist_params.distance_postfix_bits = in->dist.distance_postfix_bits;
    out->dist_params.num_direct_distance_codes = in->dist.num_direct_distance_codes;
    out->dist_params.alphabet_size_max = in->dist.alphabet_size_max;
    out->dist_params.alphabet_size_limit = in->dist.alphabet_size_limit;
    out->dist_params.max_distance = in->dist.max_distance;
    out->dictionary = in->dictionary;
}

static ContextType ChooseBrotligContextMode(const BrotligEncoderParams* params,
    const uint8_t* data, const size_t mask,
    const size_t length) {
    /* We only do the computation for the option of something else than
       CONTEXT_UTF8 for the highest qualities */
    if (params->quality >= MIN_QUALITY_FOR_HQ_BLOCK_SPLITTING &&
        !BrotliIsMostlyUTF8(data, 0, mask, length, kMinUTF8Ratio)) {
        return CONTEXT_SIGNED;
    }
    return CONTEXT_UTF8;
}

static void BrotligCreateHqZopfliBackwardReferences(
    BrotligEncoderState* state,
    const uint8_t* input,
    size_t input_size,
    ContextLut literal_context_lut)
{
    BrotligByteBuffer* lz77_in = nullptr;
    BrotligEncoderParams* newparams = nullptr;
#if LZ77_PAD_INPUT
    lz77_in = new BrotligByteBuffer(input->size + BROTLIG_LZ77_PADDING_SIZE);

    memcpy(lz77_in->data, input->data, input->size);
    memset(&lz77_in->data[input->size], 0, BROTLIG_LZ77_PADDING_SIZE);

    newparams = new BrotligEncoderParams;
    newparams->page_size = state->params->page_size;
    newparams->num_bitstreams = state->params->num_bitstreams;
    newparams->cmd_group_size = state->params->cmd_group_size;
    newparams->swizzle_size = state->params->swizzle_size;
    newparams->UpdateParams(lz77_in->size);

    EncodeWindowBits(newparams->lgwin, newparams->large_window,
        &state->last_bytes, &state->last_bytes_bits);
#else
    lz77_in = new BrotligByteBuffer(input_size);
    memcpy(lz77_in->data, input, input_size);
    newparams = state->params;
#endif // LZ77_PAD_INPUT

    /*Compute theoretical max number of commands*/
    size_t newsize = lz77_in->size / 2 + 1;
    Command* new_commands = new Command[newsize];
    size_t num_commands = 0;

    BrotliEncoderParams* bParams = new BrotliEncoderParams;
    uint8_t* inData = lz77_in->data;
    ToBrotliEncoderParams(newparams, bParams);

    InitOrStitchToPreviousBlock(
        state->mem_manager,
        &state->hasher,
        lz77_in->data,
        state->mask,
        bParams,
        0,
        lz77_in->size,
        state->isLast
    );
    ToBrotligEncoderParams(bParams, state->params);

    BrotliCreateHqZopfliBackwardReferences(
        state->mem_manager,
        lz77_in->size,
        0,
        inData,
        state->mask,
        literal_context_lut,
        bParams,
        &state->hasher,
        state->dist_cache,
        &state->last_insert_len,
        new_commands,
        &num_commands,
        &state->num_literals
    );

    delete bParams;

#if LZ77_PAD_INPUT
     delete newparams;
#endif
    newparams = nullptr;

    state->commands.resize(state->params->num_bitstreams);
    state->bs_command_sizes.resize(state->params->num_bitstreams, 0);

    int inputindex = 0;
    int bitstreamIndex = 0;
    for (size_t index = 0; index < num_commands; index += state->params->cmd_group_size)
    {
        for (size_t k = 0; k < state->params->cmd_group_size; ++k)
        {
            if (index + k >= num_commands)
                break;

            Command command = new_commands[index + k];
            
            BrotligCommand* bCommand = new BrotligCommand;
            bCommand->Copy(&command);

            bCommand->insert_pos = inputindex;
            size_t totalCmdLen = static_cast<size_t>(bCommand->insert_len) + static_cast<size_t>(bCommand->copy_len & 0x1FFFFFF);
            inputindex += static_cast<int>(totalCmdLen);

            assert(bCommand->Distance() < BROTLIG_NUM_DISTANCE_SYMBOLS);
            //assert(bCommand->insert_pos + bCommand->insert_len >= bCommand->dist);
            /*assert(bCommand->insert_len <= input->size);
            assert(bCommand->CopyLen() <= input->size);*/

            state->commands.at(bitstreamIndex).push_back(bCommand);
            state->bs_command_sizes.at(bitstreamIndex) += totalCmdLen;
        }
        bitstreamIndex++;
        if (bitstreamIndex == state->params->num_bitstreams)
            bitstreamIndex = 0;
    }

#if USE_INSERT_ONLY_COMMANDS
    if (inputindex < input_size)
    {
        while (inputindex < input_size)
        {
            size_t numLitsLeft = input_size - inputindex;

            BrotligCommand* extra = new BrotligCommand;
            extra->insert_pos = inputindex;
            extra->insert_len = static_cast<uint32_t>(numLitsLeft);
            extra->copy_len = 0;
            extra->dist_extra = 0;
            extra->dist_prefix = 0;
            extra->cmd_prefix = static_cast<uint16_t>(static_cast<size_t>(BROTLI_NUM_COMMAND_SYMBOLS) + extra->InsertLengthCode());

            assert(extra->cmd_prefix < BROLTIG_NUM_COMMAND_SYMBOLS_EFFECTIVE);

            state->commands.at(bitstreamIndex).push_back(extra);
            state->bs_command_sizes.at(bitstreamIndex) += numLitsLeft;

            bitstreamIndex++;
            if (bitstreamIndex == state->params->num_bitstreams)
                bitstreamIndex = 0;

            inputindex += static_cast<int>(numLitsLeft);
        }
    }
#endif

#if ADD_SENTINEL_PER_BITSTREAM
    // Add sentinel commands
    for (size_t i = 0; i < state->params->num_bitstreams; ++i)
    {
        BrotligCommand* sentinel = new BrotligCommand;
        sentinel->insert_pos = 0;
        sentinel->insert_len = 0;
        sentinel->copy_len = 0;
        sentinel->dist_extra = 0;
        sentinel->cmd_prefix = BROTLI_NUM_COMMAND_SYMBOLS_WITH_SENTINEL - 1;
        sentinel->dist_prefix = 0;

        state->commands.at(i).push_back(sentinel);
    }
#else
    BrotligCommand* sentinel = new BrotligCommand;
    sentinel->insert_pos = 0;
    sentinel->insert_len = 0;
    sentinel->copy_len = 0;
    sentinel->dist_extra = 0;
    sentinel->cmd_prefix = BROTLIG_NUM_COMMAND_SYMBOLS_WITH_SENTINEL - 1;
    sentinel->dist_prefix = 0;

    state->commands.at(bitstreamIndex).push_back(sentinel);
#endif

    delete[] new_commands;
    delete lz77_in;
}

PageEncoder::PageEncoder()
    : m_state(nullptr)
{}

PageEncoder::~PageEncoder()
{
    Cleanup();
}

bool PageEncoder::Setup(
    const uint8_t* input,
    size_t input_size,
    void* params,
    uint32_t flags,
    uint32_t extra)
{
    assert(extra == 0);
    
    if (m_state != nullptr)
        Cleanup();
    
    m_input  = input;
    m_inputSize = input_size;
    m_state = new BrotligEncoderState(static_cast<BrotligEncoderParams*>(params), m_inputSize, ((flags & 1) == 1));

    return true;
}

bool PageEncoder::Run()
{
    const uint32_t bytes = static_cast<uint32_t>(m_inputSize);
    MemoryManager* m = m_state->mem_manager;
    ContextType literal_context_mode;
    ContextLut literal_context_lut;

    literal_context_mode = ChooseBrotligContextMode(
        m_state->params,
        m_input,
        m_state->mask,
        m_inputSize
    );

    literal_context_lut = BROTLI_CONTEXT_LUT(literal_context_mode);

    BrotligCreateHqZopfliBackwardReferences(
        m_state,
        m_input,
        m_inputSize,
        literal_context_lut
    );

    BrotligMetaBlock* mb = new BrotligMetaBlock(
        m_state,
        m_input,
        m_inputSize,
        literal_context_mode,
        &m_output,
        &m_outputSize
    );

    if (!ShouldCompress())
    {
        if (!StoreUncompressed())
            return false;

        delete mb;
        mb = nullptr;
    }
    else
    {
        mb->Build();
        if (!mb->Store())
        {
            if (!StoreUncompressed())
                return false;

            delete mb;
            mb = nullptr;
        }
        else
        {
            delete mb;
            mb = nullptr;

            // Resize output;
            size_t newsize = (m_state->bw->GetPosition() + 8 - 1) / 8;
            if (newsize >= m_inputSize)
            {
                if (!StoreUncompressed())
                    return false;
            }
            else
            {
                Byte* temp = new Byte[newsize];
                memset(temp, 0, newsize);
                memcpy(temp, m_output, newsize);
                Byte* temp2 = m_output;
                m_output = temp;
                m_outputSize = newsize;
                delete[] temp2;
            }
        }
    }

    return true;
}

void PageEncoder::Cleanup()
{
    delete m_state;
    m_state = nullptr;

    m_input = nullptr;
    m_output = nullptr;
}

bool PageEncoder::ShouldCompress()
{
    size_t bytes = m_inputSize;
    if (bytes <= 2) return false;

    size_t num_total_cmds = 0;
    for (size_t i = 0; i < m_state->commands.size(); ++i)
    {
        num_total_cmds += m_state->commands.at(i).size();
    }
         
    if (num_total_cmds < ((bytes + 12) >> 8) + 2)
    {
        if ((double)m_state->num_literals > 0.99 * (double)bytes)
        {
            uint32_t literal_histo[256] = { 0 };
            static const uint32_t kSampleRate = 13;
            static const double kMinEntropy = 7.92;
            //static const double kMinEntropy = 7.01;
            const double bit_cost_threshold =
                (double)bytes * kMinEntropy / kSampleRate;
            size_t t = (bytes + kSampleRate - 1) / kSampleRate;
            uint32_t pos = 0;
            size_t i;
            for (i = 0; i < t; i++) {
                ++literal_histo[m_input[pos]];
                pos += kSampleRate;
            }
            if (BitsEntropy(literal_histo, 256) > bit_cost_threshold)
                return false;
            //return false;
        }
    }

    return true;
}

bool PageEncoder::StoreUncompressed()
{
    if (m_output != nullptr)
        delete[] m_output;

    m_outputSize = m_inputSize;
    m_output = new Byte[m_outputSize];

    memcpy(m_output, m_input, m_outputSize);

    return true;
}
