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

/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/


extern "C" {
#include "brotli/c/enc/utf8_util.h"
#include "brotli/c/enc/backward_references_hq.h"
#include "brotli/c/enc/cluster.h"
}

#include "common/BrotligUtils.h"
#include "common/BrotligBitWriter.h"
#include "common/BrotligDataConditioner.h"

#include "encoder/BrotligHuffman.h"

#include "PageEncoder.h"

#define MAX(a, b) ((a > b) ? a : b)
#define MIN(a, b) ((a < b) ? a : b)
#define OVERLAP(x1, x2, y1, y2) (x1 < y2 && y1 < x2)

using namespace BrotliG;

/* Chooses the literal context mode for a metablock */
static ContextType ChooseContextMode(const BrotliEncoderParams* params,
    const uint8_t* data, const size_t pos, const size_t mask,
    const size_t length) {
    /* We only do the computation for the option of something else than
       CONTEXT_UTF8 for the highest qualities */
    if (params->quality >= MIN_QUALITY_FOR_HQ_BLOCK_SPLITTING &&
        !BrotliIsMostlyUTF8(data, pos, mask, length, kMinUTF8Ratio)) {
        return CONTEXT_SIGNED;
    }
    return CONTEXT_UTF8;
}

static BROTLI_BOOL ShouldCompress(
    const uint8_t* data, const size_t mask, const uint64_t last_flush_pos,
    const size_t bytes, const size_t num_literals, const size_t num_commands) {
    /* TODO: find more precise minimal block overhead. */
    if (bytes <= 2) return BROTLI_FALSE;
    if (num_commands < (bytes >> 8) + 2) {
        if ((double)num_literals > 0.99 * (double)bytes) {
            uint32_t literal_histo[256] = { 0 };
            static const uint32_t kSampleRate = 13;
            static const double kMinEntropy = 7.92;
            const double bit_cost_threshold =
                (double)bytes * kMinEntropy / kSampleRate;
            size_t t = (bytes + kSampleRate - 1) / kSampleRate;
            uint32_t pos = (uint32_t)last_flush_pos;
            size_t i;
            for (i = 0; i < t; i++) {
                ++literal_histo[data[pos & mask]];
                pos += kSampleRate;
            }
            if (BitsEntropy(literal_histo, 256) > bit_cost_threshold) {
                return BROTLI_FALSE;
            }
        }
    }
    return BROTLI_TRUE;
}

static void BrotligCreateHqZopfliBackwardReferences(
    BrotligEncoderState* state,
    const uint8_t* input,
    size_t input_size,
    ContextLut literal_context_lut,
    bool isLast
)
{
    state->commands_ = BROTLI_ALLOC(&state->memory_manager_, Command, input_size / 2 + 5);

    InitOrStitchToPreviousBlock(
        &state->memory_manager_,
        &state->hasher_,
        input,
        BROTLIG_INPUT_BIT_MASK,
        &state->params,
        0,
        input_size,
        isLast
    );

    BrotliCreateHqZopfliBackwardReferences(
        &state->memory_manager_,
        input_size,
        0,
        input,
        BROTLIG_INPUT_BIT_MASK,
        literal_context_lut,
        &state->params,
        &state->hasher_,
        state->dist_cache_,
        &state->last_insert_len_,
        state->commands_,
        &state->num_commands_,
        &state->num_literals_
    );

    size_t endPos = 0, cmdIndex = 0;
    while (cmdIndex < state->num_commands_) {
        assert((state->commands_[cmdIndex].copy_len_ & 0x1FFFFFF) < input_size);
        endPos += state->commands_[cmdIndex].insert_len_ + (state->commands_[cmdIndex++].copy_len_ & 0x1FFFFFF); 
    }

    if (endPos < input_size)
    {
        size_t litsLeft = input_size - endPos;

        Command extra = {};
        extra.insert_len_ = (uint32_t)litsLeft;
        extra.cmd_prefix_ = (uint16_t)(BROTLI_NUM_COMMAND_SYMBOLS) + GetInsertLengthCode(extra.insert_len_);
    
        assert(extra.cmd_prefix_ < BROLTIG_NUM_COMMAND_SYMBOLS_EFFECTIVE);

        state->commands_[state->num_commands_++] = extra;
        state->num_literals_ += litsLeft;
    }

    Command sentinel = {};
    sentinel.cmd_prefix_ = BROTLIG_NUM_COMMAND_SYMBOLS_WITH_SENTINEL - 1;
    state->commands_[state->num_commands_++] = sentinel;
}

static bool StoreUncompressed(size_t inputSize, const uint8_t* input, size_t* outputSize, uint8_t* output)
{
    memset(output, 0, *outputSize);
    memcpy(output, input, inputSize);
    *outputSize = inputSize;
    return true;
}

static bool BrotligComputeDistanceCost(const Command* cmds,
    size_t num_commands,
    const BrotliDistanceParams* orig_params,
    const BrotliDistanceParams* new_params,
    double* cost) {
    size_t i;
    BROTLI_BOOL equal_params = BROTLI_FALSE;
    uint16_t dist_prefix;
    uint32_t dist_extra;
    double extra_bits = 0.0;
    HistogramDistance histo;
    HistogramClearDistance(&histo);

    if (orig_params->distance_postfix_bits == new_params->distance_postfix_bits &&
        orig_params->num_direct_distance_codes ==
        new_params->num_direct_distance_codes) {
        equal_params = BROTLI_TRUE;
    }

    for (i = 0; i < num_commands; i++) {
        const Command* cmd = &cmds[i];
        if (CommandCopyLen(cmd) && cmd->cmd_prefix_ >= 128 /*&& cmd->cmd_prefix_ < BROTLI_NUM_COMMAND_SYMBOLS*/) {
            if (equal_params) {
                dist_prefix = cmd->dist_prefix_;
            }
            else {
                uint32_t distance = CommandRestoreDistanceCode(cmd, orig_params);
                if (distance > new_params->max_distance) {
                    return BROTLI_FALSE;
                }
                PrefixEncodeCopyDistance(distance,
                    new_params->num_direct_distance_codes,
                    new_params->distance_postfix_bits,
                    &dist_prefix,
                    &dist_extra);
            }
            HistogramAddDistance(&histo, dist_prefix & 0x3FF);
            extra_bits += dist_prefix >> 10;
        }
    }

    *cost = BrotliPopulationCostDistance(&histo); // +extra_bits;
    return BROTLI_TRUE;
}

static void BrotligRecomputeDistancePrefixes(
    Command* cmds,
    size_t num_commands,
    const BrotliDistanceParams* orig_params,
    const BrotliDistanceParams* new_params
)
{
    size_t i;

    if (orig_params->distance_postfix_bits == new_params->distance_postfix_bits &&
        orig_params->num_direct_distance_codes ==
        new_params->num_direct_distance_codes) {
        return;
    }

    for (i = 0; i < num_commands; ++i) {
        Command* cmd = &cmds[i];
        if (CommandCopyLen(cmd) && cmd->cmd_prefix_ >= 128) {
            PrefixEncodeCopyDistance(CommandRestoreDistanceCode(cmd, orig_params),
                new_params->num_direct_distance_codes,
                new_params->distance_postfix_bits,
                &cmd->dist_prefix_,
                &cmd->dist_extra_);
        }
    }
}

PageEncoder::PageEncoder()
{
    m_state = nullptr;
    m_dcparams = nullptr;
}

PageEncoder::~PageEncoder()
{
    Cleanup();
}

bool PageEncoder::Setup(BrotligEncoderParams& params, BrotligDataconditionParams* dcparams)
{
    m_params = params;
    m_dcparams = dcparams;
    return true;
}

bool PageEncoder::Run(const uint8_t* input, size_t inputSize, size_t inputOffset, uint8_t* output, size_t* outputSize, size_t outputOffset, bool isLast)
{
    bool Isdeltaencoded = false;

    const uint8_t* p_inPtr = input + inputOffset;
    size_t inSize = inputSize;
    if (m_dcparams->precondition && m_dcparams->delta_encode)
    {
        uint8_t* pageCopy = new uint8_t[inSize];
        memcpy(pageCopy, input + inputOffset, inSize);

        Isdeltaencoded = DeltaEncode(inputOffset, inputOffset + inputSize, pageCopy);

        if (Isdeltaencoded)
            p_inPtr = pageCopy;
        else
            delete[] pageCopy;
    }
    
    uint8_t* p_outPtr = output + outputOffset;

    // Create encoder instance
    m_state = new BrotligEncoderState(0, 0, 0);
    if (!m_state) return false;

    m_state->SetParameter(BROTLI_PARAM_QUALITY, (uint32_t)m_params.quality);
    m_state->SetParameter(BROTLI_PARAM_LGWIN, (uint32_t)m_params.lgwin);
    m_state->SetParameter(BROTLI_PARAM_MODE, (uint32_t)m_params.mode);
    m_state->SetParameter(BROTLI_PARAM_SIZE_HINT, (uint32_t)inSize);

    if (m_params.lgwin > BROTLI_MAX_WINDOW_BITS) {
        m_state->SetParameter(BROTLI_PARAM_LARGE_WINDOW, BROTLI_TRUE);
    }

    if (!m_state->EnsureInitialized())
        return false;

    // Generate LZ77 commands
    ContextType literal_context_mode = ChooseContextMode(
        &m_state->params,
        p_inPtr,
        0,
        BROTLIG_INPUT_BIT_MASK,
        inSize
    );

    ContextLut literal_context_lut = BROTLI_CONTEXT_LUT(literal_context_mode);

    BrotligCreateHqZopfliBackwardReferences(
        m_state,
        p_inPtr,
        inSize,
        literal_context_lut,
        isLast
    );

    // Check if input is compressible
    if (!ShouldCompress(
        p_inPtr,
        BROTLIG_INPUT_BIT_MASK,
        0,
        inSize,
        m_state->num_literals_,
        m_state->num_commands_
    ))
    {
        // If not compressible, destroy encoder instance,
        // copy input directly to output and return
        delete m_state;
        m_state = nullptr;

        if (Isdeltaencoded)
            delete[] p_inPtr;

        return StoreUncompressed(inputSize, input + inputOffset, outputSize, p_outPtr);
    }

    // Optimize distance prefixes
    uint32_t ndirect_msb = 0, ndirect = 0;
    bool skip = false, check_orig = true;
    double dist_cost = 0.0, best_dist_cost = 1e99;
    BrotliEncoderParams orig_params = m_state->params;
    BrotliEncoderParams new_params = m_state->params;
    for (uint32_t npostfix = 0; npostfix <= BROTLI_MAX_NPOSTFIX; ++npostfix)
    {
        for (; ndirect_msb < 16; ++ndirect_msb)
        {
            ndirect = ndirect_msb << npostfix;
            BrotliInitDistanceParams(&new_params, npostfix, ndirect);
            if (npostfix == orig_params.dist.distance_postfix_bits
                && ndirect == orig_params.dist.num_direct_distance_codes)
                check_orig = false;

            skip = !BrotligComputeDistanceCost(
                m_state->commands_,
                m_state->num_commands_,
                &orig_params.dist,
                &new_params.dist,
                &dist_cost
            );

            if (skip || (dist_cost > best_dist_cost))
                break;

            best_dist_cost = dist_cost;
            m_state->params.dist = new_params.dist;
        }

        if (ndirect_msb > 0) --ndirect_msb;
        ndirect_msb /= 2;
    }

    if (check_orig)
    {
        BrotligComputeDistanceCost(
            m_state->commands_,
            m_state->num_commands_,
            &orig_params.dist,
            &orig_params.dist,
            &dist_cost
        );

        if (dist_cost < best_dist_cost) m_state->params.dist = orig_params.dist;
    }

    BrotligRecomputeDistancePrefixes(
        m_state->commands_,
        m_state->num_commands_,
        &orig_params.dist,
        &m_state->params.dist
    );

    // Compute Histograms
    memset(m_histDistances, 0, sizeof(m_histDistances));
    memset(m_histCommands, 0, sizeof(m_histCommands));
    memset(m_histLiterals, 0, sizeof(m_histLiterals));

    size_t cmdIndex = 0, pos = 0, numbitstreams = m_params.num_bitstreams;
    Command cmd;
    uint32_t insertLen = 0, distContext = 0;
    uint8_t* litqueue = new uint8_t[inSize];
    uint8_t* litqfront = litqueue;
    uint8_t* litqback = litqueue;
    uint32_t numLits = 0;

    HistogramDistance distCtxHists[BROTLIG_NUM_DIST_CONTEXT_HISTOGRAMS];
    ClearHistogramsDistance(distCtxHists, BROTLIG_NUM_DIST_CONTEXT_HISTOGRAMS);

    while (cmdIndex < m_state->num_commands_)
    {
        cmd = m_state->commands_[cmdIndex++];
        ++m_histCommands[cmd.cmd_prefix_];
        if ((cmd.copy_len_ & 0x1FFFFFF) &&
            (cmd.cmd_prefix_ >= 128 && cmd.cmd_prefix_ < BROTLI_NUM_COMMAND_SYMBOLS))
        {
            distContext = CommandDistanceContext(&cmd);
            HistogramAddDistance(&distCtxHists[distContext], cmd.dist_prefix_ & 0x3FF);
        }
        insertLen = cmd.insert_len_;
        while (insertLen--) {
            ++m_histLiterals[p_inPtr[pos]];
            *litqback++ = p_inPtr[pos++];
            ++numLits;
        }
        pos += (cmd.copy_len_ & 0x1FFFFFF);
    }

    // Cluster distance histograms
    HistogramDistance out[BROTLIG_NUM_DIST_CONTEXT_HISTOGRAMS];
    size_t num_out = 0;
    uint32_t dist_context_map[BROTLIG_NUM_DIST_CONTEXT_HISTOGRAMS];
    BrotliClusterHistogramsDistance(
        &m_state->memory_manager_,
        distCtxHists,
        BROTLIG_NUM_DIST_CONTEXT_HISTOGRAMS,
        BROTLIG_MAX_NUM_DIST_HISTOGRAMS,
        out,
        &num_out,
        dist_context_map
    );

    memcpy(m_histDistances, out[0].data_, sizeof(out[0].data_));

    // Optimize Histograms
    if (m_state->params.quality >= MIN_QUALITY_FOR_OPTIMIZE_HISTOGRAMS)
    {
        uint8_t good_for_rle[BROLTIG_NUM_COMMAND_SYMBOLS_EFFECTIVE];
        BrotliOptimizeHuffmanCountsForRle(BROTLI_NUM_LITERAL_SYMBOLS, m_histLiterals, good_for_rle);
        BrotliOptimizeHuffmanCountsForRle(BROLTIG_NUM_COMMAND_SYMBOLS_EFFECTIVE, m_histCommands, good_for_rle);
        BrotliOptimizeHuffmanCountsForRle(BROTLIG_NUM_DISTANCE_SYMBOLS, m_histDistances, good_for_rle);
    }

    uint8_t mostFreqLit = (uint8_t)(std::max_element(m_histLiterals, m_histLiterals + BROTLI_NUM_LITERAL_SYMBOLS) - m_histLiterals);

    // Store compressed
    memset(p_outPtr, 0, *outputSize);
    BrotligBitWriterLSB bw;
    bw.SetStorage(p_outPtr);
    bw.SetPosition(0);

    m_pWriter = new BrotligSwizzler(m_params.num_bitstreams, m_params.page_size);
    m_pWriter->SetOutWriter(&bw, *outputSize);

    // Build and Store Huffman tables
    BuildStoreHuffmanTable(
        m_histCommands,
        BROLTIG_NUM_COMMAND_SYMBOLS_EFFECTIVE,
        *m_pWriter,
        m_cmdCodes,
        m_cmdCodelens
    );

    BuildStoreHuffmanTable(
        m_histDistances,
        BROTLIG_NUM_DISTANCE_SYMBOLS,
        *m_pWriter,
        m_distCodes,
        m_distCodelens
    );

    BuildStoreHuffmanTable(
        m_histLiterals,
        BROTLI_NUM_LITERAL_SYMBOLS,
        *m_pWriter,
        m_litCodes,
        m_litCodelens
    );

    // Encode and store commands and literals
    size_t highestFreq = 0;
    cmdIndex = 0;
    BrotligBitWriterLSB* bs_bw = nullptr;
    size_t bsindex = 0;
    bool sentinelFound = false;

    size_t nRounds = (m_state->num_commands_ + numbitstreams - 1) / numbitstreams;
    Command* cqfront = m_state->commands_;
    size_t litcount = 0, aclitcount = 0, mult = 0, rlitcount = 0, prev_tail = 0;
    size_t effNumbitstreams = (m_state->num_commands_ >= numbitstreams) ? numbitstreams : m_state->num_commands_;

    while (nRounds--)
    {
        bsindex = 0;
        litcount = 0;

        while (bsindex < numbitstreams)
        {
            cmd = *cqfront++;
            litcount += cmd.insert_len_;
            StoreCommand(cmd);

            if (cmd.insert_len_ == 0 && (cmd.copy_len_ & 0x1FFFFFF) == 0)
            {
                sentinelFound = true;
                break;
            }

            if ((cmd.copy_len_ & 0x1FFFFFF)
                && cmd.cmd_prefix_ >= 128
                && cmd.cmd_prefix_ < BROTLI_NUM_COMMAND_SYMBOLS)
            {
                StoreDistance(cmd.dist_prefix_, cmd.dist_extra_);
            }

            ++bsindex;

            m_pWriter->BSSwitch();
        }

        m_pWriter->BSReset();

        effNumbitstreams = (m_state->num_commands_ >= numbitstreams) ? numbitstreams : m_state->num_commands_;
        aclitcount = (litcount > prev_tail) ? litcount - prev_tail : 0;
        mult = (aclitcount + effNumbitstreams - 1) / effNumbitstreams;
        rlitcount = effNumbitstreams * mult;
        prev_tail = rlitcount + prev_tail - litcount;

        while (rlitcount--)
        {
            if (litqfront >= litqback)
            {
                if (nRounds > 0 || isLast)
                    StoreLiteral(mostFreqLit);
                else
                    break;
            }
            else
                StoreLiteral(*litqfront++);

            m_pWriter->BSSwitch();
        }

        m_pWriter->BSReset();
    }

    delete[] litqueue;

    // Store header
    m_pWriter->AppendToHeader(BROTLIG_PAGE_HEADER_NPOSTFIX_BITS, m_state->params.dist.distance_postfix_bits);
    m_pWriter->AppendToHeader(BROTLIG_PAGE_HEADER_NDIST_BITS, m_state->params.dist.num_direct_distance_codes >> m_state->params.dist.distance_postfix_bits);
    m_pWriter->AppendToHeader(BROTLIG_PAGE_HEADER_ISDELTAENCODED_BITS, Isdeltaencoded);
    m_pWriter->AppendToHeader(BROTLIG_PAGE_HEADER_RESERVED_BITS, 0);

    m_pWriter->AppendBitstreamSizes();

    // Serialize header
    m_pWriter->SerializeHeader();
    // Serialize bitstreams
    m_pWriter->SerializeBitstreams();

    if (Isdeltaencoded)
        delete[] p_inPtr;

    delete m_pWriter;

    delete m_state;
    m_state = nullptr;

    size_t newsize = (bw.GetPosition() + 8 - 1) / 8;

    if (newsize >= inputSize)
        return StoreUncompressed(inputSize, input + inputOffset, outputSize, p_outPtr);
    else
    {
        *outputSize = newsize;
        return true;
    }
}

bool PageEncoder::DeltaEncode(size_t page_start, size_t page_end, uint8_t* data)
{
    uint32_t sub = 0;
    size_t color_start = 0, color_end = 0, p_sub_start = 0, p_sub_end = 0, p_sub_size = 0;
    bool iseconded = false;
    for (uint32_t i = 0; i < m_dcparams->numColorSubBlocks; ++i)
    {
        sub = m_dcparams->colorSubBlocks[i];
        color_start = (size_t)m_dcparams->subStreamOffsets[sub];
        color_end = (size_t)m_dcparams->subStreamOffsets[sub + 1];

        if (OVERLAP(color_start, color_end, page_start, page_end))
        {
            p_sub_start = (color_start > page_start) ? color_start - page_start : 0;
            p_sub_end = (color_end < page_end) ? color_end - page_start : page_end - page_start;
            p_sub_size = p_sub_end - p_sub_start;

            DeltaEncodeByte(p_sub_size, data + p_sub_start);
            iseconded |= true;
        }
    }

    return iseconded;
}

void PageEncoder::DeltaEncodeByte(size_t inSize, uint8_t* inData)
{
    uint8_t ref = inData[0];

    uint8_t prev = ref, cur = 0;
    for (size_t el = 1; el < inSize; ++el)
    {
        cur = inData[el];
        inData[el] -= prev;
        prev = cur;
    }
}

void PageEncoder::StoreCommand(Command& cmd)
{
    uint16_t nbits = m_cmdCodelens[cmd.cmd_prefix_];
    m_pWriter->Append(nbits, m_cmdCodes[cmd.cmd_prefix_]);
    if (cmd.cmd_prefix_ <= BROTLI_NUM_COMMAND_SYMBOLS)
    {
        uint32_t copylen_code = CommandCopyLenCode(&cmd);
        uint16_t inscode = GetInsertLengthCode(cmd.insert_len_);
        uint16_t copycode = (copylen_code == 0) ? 0 : GetCopyLengthCode(copylen_code);
        uint32_t insnumextra = GetInsertExtra(inscode);
        uint64_t insextraval = cmd.insert_len_ - GetInsertBase(inscode);
        uint64_t copyextraval = (copycode > 1) ? copylen_code - GetCopyBase(copycode) : copylen_code;
        uint64_t bits = (copyextraval << insnumextra) | insextraval;
        m_pWriter->Append(insnumextra + GetCopyExtra(copycode), bits);
    }
    else
    {
        uint16_t inscode = GetInsertLengthCode(cmd.insert_len_);
        uint32_t insnumextra = GetInsertExtra(inscode);
        uint64_t insextraval = cmd.insert_len_ - GetInsertBase(inscode);
        m_pWriter->Append(insnumextra, insextraval);
    }
}

void PageEncoder::StoreLiteral(uint8_t literal)
{
    uint16_t nbits = m_litCodelens[literal];
    m_pWriter->Append(nbits, m_litCodes[literal]);
}

void PageEncoder::StoreDistance(uint16_t dist_prefix, uint32_t distextra)
{
    uint16_t dist_code = dist_prefix & 0x3FF;
    uint16_t nbits = m_distCodelens[dist_code];
    uint32_t distnumextra = dist_prefix >> 10;
    m_pWriter->Append(nbits, m_distCodes[dist_code]);
    m_pWriter->Append(distnumextra, distextra);   
}

void PageEncoder::Cleanup()
{
    if (m_state != nullptr)
    {
        delete m_state;
        m_state = nullptr;
    }
}
