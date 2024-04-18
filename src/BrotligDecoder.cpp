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


#include <iostream>
#include <thread>

#include "common/BrotligConstants.h"

#include "decoder/PageDecoder.h"

#include "DataStream.h"

#include "BrotliG.h"
#include "BrotligDecoder.h"

using namespace BrotliG;

uint32_t BROTLIG_API BrotliG::DecompressedSize(uint8_t* src)
{
    const StreamHeader* sheader = reinterpret_cast<const StreamHeader*>(src);
    return static_cast<uint32_t>(sheader->UncompressedSize());
}

namespace BrotliG {
    struct PageDecoderCtx
    {
        const uint8_t* inputPtr;
        uint8_t* outputPtr;

        const uint32_t* pageTable;
        uint32_t numPages;

        uint32_t lastPageSize;

        std::atomic_uint32_t globalIndex;

        BROTLIG_Feedback_Proc feedbackProc;

        PageDecoderCtx()
        {
            inputPtr = nullptr;
            outputPtr = nullptr;

            pageTable = nullptr;
            numPages = 0;

            lastPageSize = 0;

            feedbackProc = nullptr;
        }

        ~PageDecoderCtx()
        {
            inputPtr = nullptr;
            outputPtr = nullptr;

            pageTable = nullptr;
            numPages = 0;

            lastPageSize = 0;

            feedbackProc = nullptr;
        }
    };

    struct BlockDeconditionerCtx
    {
        uint8_t* inputPtr;
        uint8_t* outputPtr;

        uint8_t* blockModes;
        uint32_t suboffsets[BROTLIG_MAX_NUM_SUB_BLOCKS] = { 0 };
        uint32_t* subblockoffsets[BROTLIG_MAX_NUM_SUB_BLOCKS - 1] = { nullptr };
        uint32_t numBlocks;

        std::atomic_uint32_t globalIndex;

        BROTLIG_Feedback_Proc feedbackProc;

        BlockDeconditionerCtx()
        {
            inputPtr = nullptr;
            outputPtr = nullptr;

            blockModes = nullptr;
            numBlocks = 0;

            feedbackProc = nullptr;
        }

        ~BlockDeconditionerCtx()
        {
            inputPtr = nullptr;
            outputPtr = nullptr;

            blockModes = nullptr;
            numBlocks = 0;

            feedbackProc = nullptr;
        }
    };
}

void DecodeCPUWithPreconSingleThread(
    uint32_t input_size,
    const uint8_t* src,
    BrotligDecoderParams& params,
    BrotligDataconditionParams& dcParams,
    uint32_t numPages,
    uint32_t lastPageSize,
    uint32_t output_size,
    uint8_t* output,
    BROTLIG_Feedback_Proc feedbackProc
)
{
    const uint8_t* srcPtr = src;
    uint8_t* outPtr = output;
    uint32_t outSize = output_size;

    // Read the page table
    const uint32_t* pageTable = reinterpret_cast<const uint32_t*>(srcPtr);
    srcPtr += numPages * sizeof(uint32_t);

    PageDecoder pDecoder;
    pDecoder.Setup(params, dcParams);

    uint32_t pageIndex = 0;
    uint32_t curInOffset = 0, curOutOffset = 0;
    size_t inPageSize = 0, outPageSize = 0;

    while (pageIndex < numPages)
    {
        curInOffset = (pageIndex == 0) ? 0 : pageTable[pageIndex];
        inPageSize = (pageIndex < numPages - 1) ? (pageTable[pageIndex + 1] - curInOffset) : pageTable[0];

        curOutOffset = pageIndex * (uint32_t)params.page_size;
        outPageSize = ((pageIndex == numPages - 1) && (lastPageSize != 0)) ? lastPageSize : params.page_size;

        pDecoder.Run(srcPtr, inPageSize, curInOffset, outPtr, outPageSize, curOutOffset);

        if (feedbackProc)
        {
            float progress = 100.f * ((float)(pageIndex) / numPages);
            if (feedbackProc(BROTLIG_MESSAGE_TYPE::BROTLIG_PROGRESS, std::to_string(progress)))
            {
                break;
            }
        }

        ++pageIndex;
    }

    pDecoder.Cleanup();
}

void DecodeCPUNoPreconSingleThread(
    uint32_t input_size,
    const uint8_t* src,
    BrotligDecoderParams& params,
    uint32_t numPages,
    uint32_t lastPageSize,
    uint32_t output_size,
    uint8_t* output,
    BROTLIG_Feedback_Proc feedbackProc
)
{
    const uint8_t* srcPtr = src;

    uint8_t* outPtr = output;
    uint32_t outSize = output_size;

    // Read the page table
    const uint32_t* pageTable = reinterpret_cast<const uint32_t*>(srcPtr);
    srcPtr += numPages * sizeof(uint32_t);

    BrotligDataconditionParams dcParams = {};

    PageDecoder pDecoder;
    pDecoder.Setup(params, dcParams);

    uint32_t pageIndex = 0;
    uint32_t curInOffset = 0, curOutOffset = 0;
    size_t inPageSize = 0, outPageSize = 0;

    while (pageIndex < numPages)
    {
        curInOffset = (pageIndex == 0) ? 0 : pageTable[pageIndex];
        inPageSize = (pageIndex < numPages - 1) ? (pageTable[pageIndex + 1] - curInOffset) : pageTable[0];

        curOutOffset = pageIndex * (uint32_t)params.page_size;
        outPageSize = ((pageIndex == numPages - 1) && (lastPageSize != 0)) ? lastPageSize : params.page_size;

        pDecoder.Run(srcPtr, inPageSize, curInOffset, outPtr, outPageSize, curOutOffset);

        if (feedbackProc)
        {
            float progress = 100.f * ((float)(pageIndex) / numPages);
            if (feedbackProc(BROTLIG_MESSAGE_TYPE::BROTLIG_PROGRESS, std::to_string(progress)))
            {
                break;
            }
        }

        ++pageIndex;
    }

    pDecoder.Cleanup();
}

BROTLIG_ERROR DecodeCPUSingleThreaded(
    uint32_t input_size,
    const uint8_t* src,
    uint32_t* output_size,
    uint8_t* output,
    BROTLIG_Feedback_Proc feedbackProc)
{  
    const uint8_t* srcPtr = src;
    uint32_t srcSize = input_size;

    // Read the header
    const StreamHeader* sHeader = reinterpret_cast<const StreamHeader*>(srcPtr);
    if (!sHeader->Validate())
    {
        return BROTLIG_ERROR_CORRUPT_STREAM;
    }

    if (sHeader->Id != BROTLIG_STREAM_ID)
    {
        return BROTLIG_ERROR_INCORRECT_STREAM_FORMAT;
    }

    memset(output, 0, *output_size);

    BrotligDecoderParams params = {};
    params.num_bitstreams = BROLTIG_DEFAULT_NUM_BITSTREAMS;
    params.page_size = static_cast<uint32_t>(sHeader->PageSize());

    BrotligDataconditionParams dcParams = {};
    dcParams.precondition = sHeader->IsPreconditioned();

    uint32_t lastPageSize = sHeader->LastPageSize;
    uint32_t numPages = sHeader->NumPages;

    uint8_t* outPtr = output;
    uint32_t outSize = (uint32_t)sHeader->UncompressedSize();

    srcPtr += sizeof(StreamHeader);
    srcSize -= sizeof(StreamHeader);

    if (dcParams.precondition)
    {
        // Read the precondition header
        const PreconditionHeader* preHeader = reinterpret_cast<const PreconditionHeader*>(srcPtr);
        dcParams.swizzle = preHeader->Swizzled;
        dcParams.pitchd3d12aligned = preHeader->PitchD3D12Aligned;
        dcParams.widthInBlocks[0] = preHeader->WidthInBlocks + 1;
        dcParams.heightInBlocks[0] = preHeader->HeightInBlocks + 1;
        dcParams.format = preHeader->DataFormat();
        dcParams.numMipLevels = preHeader->NumMips + 1;
        dcParams.pitchInBytes[0] = preHeader->PitchInBytes + 1;
        
        dcParams.Initialize(*output_size);

        srcPtr += sizeof(PreconditionHeader);
        srcSize -= sizeof(PreconditionHeader);

        DecodeCPUWithPreconSingleThread(srcSize, srcPtr, params, dcParams, numPages, lastPageSize, outSize, output, feedbackProc);
    }
    else
    {
        DecodeCPUNoPreconSingleThread(srcSize, srcPtr, params, numPages, lastPageSize, outSize, output, feedbackProc);
    }

    *output_size = outSize;

    return BROTLIG_OK;
}

static void PageDecoderJob(PageDecoderCtx& ctx, const BrotligDecoderParams& params, const BrotligDataconditionParams& dcParams)
{
    PageDecoder pDecoder;
    pDecoder.Setup(params, dcParams);

    uint32_t curInOffset = 0, curOutOffset = 0;
    size_t inPageSize = 0, outPageSize = 0;
    while (true)
    {
        const uint32_t pageIndex = ctx.globalIndex.fetch_add(1, std::memory_order_relaxed);

        if (pageIndex >= ctx.numPages)
            break;

        curInOffset = (pageIndex == 0) ? 0 : ctx.pageTable[pageIndex];
        inPageSize = (pageIndex < ctx.numPages - 1) ? (ctx.pageTable[pageIndex + 1] - curInOffset) : ctx.pageTable[0];

        curOutOffset = pageIndex * (uint32_t)params.page_size;
        outPageSize = ((pageIndex == ctx.numPages - 1) && (ctx.lastPageSize != 0)) ? ctx.lastPageSize : params.page_size;

        pDecoder.Run(ctx.inputPtr, inPageSize, curInOffset, ctx.outputPtr, outPageSize, curOutOffset);

        if (ctx.feedbackProc)
        {
            float progress = 100.f * ((float)(pageIndex) / ctx.numPages);
            if (ctx.feedbackProc(BROTLIG_MESSAGE_TYPE::BROTLIG_PROGRESS, std::to_string(progress)))
            {
                break;
            }
        }
    }

    pDecoder.Cleanup();
}

void DecodeCPUWithPreconMultiThread(
    uint32_t input_size,
    const uint8_t* src,
    BrotligDecoderParams& params,
    BrotligDataconditionParams& dcParams,
    uint32_t numPages,
    uint32_t lastPageSize,
    uint32_t output_size,
    uint8_t* output,
    BROTLIG_Feedback_Proc feedbackProc
)
{
    const uint8_t* srcPtr = src;
    uint8_t* outPtr = output;
    uint32_t outSize = output_size;

    PageDecoderCtx ctx{};
    ctx.globalIndex = 0;
    ctx.lastPageSize = lastPageSize;
    ctx.numPages = numPages;
    ctx.pageTable = reinterpret_cast<const uint32_t*>(srcPtr);
    srcPtr += ctx.numPages * sizeof(uint32_t);
    ctx.inputPtr = srcPtr;
    ctx.outputPtr = outPtr;

    const uint32_t maxWorkers = std::min(static_cast<unsigned int>(BROTLIG_MAX_WORKERS), BrotliG::GetNumberOfProcessorsThreads());
    std::thread workers[BROTLIG_MAX_WORKERS];

    uint32_t numWorkersLeft = (ctx.numPages > 2 * maxWorkers) ? maxWorkers : 1;
    for (auto& worker : workers)
    {
        if (numWorkersLeft == 1)
            break;

        worker = std::thread([&ctx, &params, &dcParams]() {PageDecoderJob(ctx, params, dcParams); });
        --numWorkersLeft;
    }

    PageDecoderJob(ctx, params, dcParams);

    for (auto& worker : workers)
    {
        if (worker.joinable())
            worker.join();
    }
}

void DecodeCPUNoPreconMultiThread(
    uint32_t input_size,
    const uint8_t* src,
    BrotligDecoderParams& params,
    uint32_t numPages,
    uint32_t lastPageSize,
    uint32_t output_size,
    uint8_t* output,
    BROTLIG_Feedback_Proc feedbackProc
)
{
    const uint8_t* srcPtr = src;
    uint8_t* outPtr = output;
    uint32_t outSize = output_size;

    PageDecoderCtx ctx{};
    ctx.globalIndex = 0;
    ctx.lastPageSize = lastPageSize;
    ctx.numPages = numPages;
    ctx.pageTable = reinterpret_cast<const uint32_t*>(srcPtr);
    srcPtr += ctx.numPages * sizeof(uint32_t);
    ctx.inputPtr = srcPtr;
    ctx.outputPtr = outPtr;

    BrotligDataconditionParams dcParams = {};

    const uint32_t maxWorkers = std::min(static_cast<unsigned int>(BROTLIG_MAX_WORKERS), BrotliG::GetNumberOfProcessorsThreads());
    std::thread workers[BROTLIG_MAX_WORKERS];

    uint32_t numWorkersLeft = (ctx.numPages > 2 * maxWorkers) ? maxWorkers : 1;
    for (auto& worker : workers)
    {
        if (numWorkersLeft == 1)
            break;

        worker = std::thread([&ctx, &params, &dcParams]() {PageDecoderJob(ctx, params, dcParams); });
        --numWorkersLeft;
    }

    PageDecoderJob(ctx, params, dcParams);

    for (auto& worker : workers)
    {
        if (worker.joinable())
            worker.join();
    }
}

BROTLIG_ERROR DecodeCPUMultithreaded(
    uint32_t input_size,
    const uint8_t* src,
    uint32_t* output_size,
    uint8_t* output,
    BROTLIG_Feedback_Proc feedbackProc)
{
    const uint8_t* srcPtr = src;
    uint32_t srcSize = input_size;

    // Read the header
    const StreamHeader* sHeader = reinterpret_cast<const StreamHeader*>(srcPtr);
    if (!sHeader->Validate())
    {
        return BROTLIG_ERROR_CORRUPT_STREAM;
    }

    if (sHeader->Id != BROTLIG_STREAM_ID)
    {
        return BROTLIG_ERROR_INCORRECT_STREAM_FORMAT;
    }

    memset(output, 0, *output_size);

    BrotligDecoderParams params = {};
    params.num_bitstreams = BROLTIG_DEFAULT_NUM_BITSTREAMS;
    params.page_size = static_cast<uint32_t>(sHeader->PageSize());

    BrotligDataconditionParams dcParams = {};
    dcParams.precondition = sHeader->IsPreconditioned();

    uint32_t lastPageSize = sHeader->LastPageSize;
    uint32_t numPages = sHeader->NumPages;

    uint8_t* outPtr = output;
    uint32_t outSize = (uint32_t)sHeader->UncompressedSize();

    srcPtr += sizeof(StreamHeader);
    srcSize -= sizeof(StreamHeader);

    if (dcParams.precondition)
    {
        // Read the precondition header
        const PreconditionHeader* preHeader = reinterpret_cast<const PreconditionHeader*>(srcPtr);
        dcParams.swizzle = preHeader->Swizzled;
        dcParams.pitchd3d12aligned = preHeader->PitchD3D12Aligned;
        dcParams.widthInBlocks[0] = preHeader->WidthInBlocks + 1;
        dcParams.heightInBlocks[0] = preHeader->HeightInBlocks + 1;
        dcParams.format = preHeader->DataFormat();
        dcParams.numMipLevels = preHeader->NumMips + 1;
        dcParams.pitchInBytes[0] = preHeader->PitchInBytes + 1;

        dcParams.Initialize(*output_size);

        srcPtr += sizeof(PreconditionHeader);
        srcSize -= sizeof(PreconditionHeader);

        DecodeCPUWithPreconMultiThread(srcSize, srcPtr, params, dcParams, numPages, lastPageSize, outSize, output, feedbackProc);
    }
    else
    {
        DecodeCPUNoPreconMultiThread(srcSize, srcPtr, params, numPages, lastPageSize, outSize, output, feedbackProc);
    }

    *output_size = outSize;

    return BROTLIG_OK;
}

BROTLIG_ERROR BROTLIG_API BrotliG::DecodeCPU(
    uint32_t input_size,
    const uint8_t* src,
    uint32_t* output_size,
    uint8_t* output,
    BROTLIG_Feedback_Proc feedbackProc)
{
#if BROTLIG_CPU_DECODER_MULTITHREADING_MODE
    return DecodeCPUMultithreaded(
        input_size,
        src,
        output_size,
        output,
        feedbackProc
    );
#else
    return DecodeCPUSingleThreaded(
        input_size,
        src,
        output_size,
        output,
        feedbackProc
    );
#endif // BROTLIG_CPU_DECODER_MULTITHREADED
}