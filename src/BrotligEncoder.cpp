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


#include <iostream>
#include <thread>

#include "common/BrotligConstants.h"

#include "encoder/PageEncoder.h"

#include "DataStream.h"

#include "BrotliG.h"
#include "BrotligEncoder.h"

using namespace BrotliG;

uint32_t BROTLIG_API BrotliG::MaxCompressedSize(uint32_t input_size, bool precondition, bool deltaencode)
{    
    uint32_t numPages = (input_size + BROTLIG_DEFAULT_PAGE_SIZE - 1) / (BROTLIG_DEFAULT_PAGE_SIZE);
    uint32_t compressedPagesSize = static_cast<uint32_t>(PageEncoder::MaxCompressedSize(BROTLIG_DEFAULT_PAGE_SIZE));
    uint32_t estimatedSize = (numPages * compressedPagesSize) + (numPages * BROTLIG_PAGE_HEADER_SIZE_BYTES) + sizeof(StreamHeader);

    if (precondition) {
        estimatedSize += sizeof(PreconditionHeader);
        if (deltaencode)
            estimatedSize += numPages * BROTLIG_PRECON_DELTA_ENCODING_BASES_SIZE_BYTES;
    }

    return estimatedSize;
}

BROTLIG_ERROR BROTLIG_API BrotliG::CheckParams(uint32_t page_size, BrotligDataconditionParams dcParams)
{
    if (page_size < BROTLIG_MIN_PAGE_SIZE)
        return BROTLIG_ERROR_MIN_PAGE_SIZE;

    if (page_size > BROTLIG_MAX_PAGE_SIZE)
        return BROTLIG_ERROR_MAX_PAGE_SIZE;

    if (dcParams.precondition)
        return dcParams.CheckParams();

    return BROTLIG_OK;
}

namespace BrotliG {
    struct PageEncoderCtx
    {
        const uint8_t* inputPtr;
        uint8_t* outputPtr;

        size_t* outPageSizes;
        uint32_t numPages;

        uint32_t maxOutPageSize;
        uint32_t lastPageSize;

        std::atomic_uint32_t globalIndex;

        BROTLIG_Feedback_Proc feedbackProc;

        PageEncoderCtx()
        {
            inputPtr = nullptr;
            outputPtr = nullptr;

            outPageSizes = nullptr;
            numPages = 0;

            maxOutPageSize = 0;
            lastPageSize = 0;

            feedbackProc = nullptr;
        }

        ~PageEncoderCtx()
        {
            inputPtr = nullptr;
            outputPtr = nullptr;

            outPageSizes = nullptr;
            numPages = 0;

            maxOutPageSize = 0;
            lastPageSize = 0;

            feedbackProc = nullptr;
        }
    };
}

void EncodeWithPreconSinglethreaded(
    uint32_t input_size,
    const uint8_t* src,
    uint32_t* output_size,
    uint8_t*& output,
    uint32_t page_size,
    BrotligDataconditionParams& dcParams,
    BROTLIG_Feedback_Proc feedbackProc
)
{   
    uint8_t* srcConditioned = nullptr;
    uint32_t srcCondSize = 0;

    BrotliG::Condition(input_size, src, dcParams, srcCondSize, srcConditioned);

    uint32_t numPages = (srcCondSize + page_size - 1) / page_size;

    size_t maxOutPageSize = PageEncoder::MaxCompressedSize(page_size);
    uint8_t* tOutput = new uint8_t[maxOutPageSize * numPages];
    size_t* tOutpageSizes = new size_t[numPages];

    std::vector<size_t> outputPageSize(numPages);

    BrotligEncoderParams params = {
        BROTLI_MAX_QUALITY,
        BROTLI_MAX_WINDOW_BITS,
        page_size
    };

    PageEncoder pEncoder;
    pEncoder.Setup(params, &dcParams);

    uint32_t pageIndex = 0;
    uint32_t sizeLeftToRead = srcCondSize, sizeToRead = 0, curInOffset = 0, curOutOffset = 0;
    uint8_t* srcPtr = srcConditioned;
    uint8_t* outPtr = tOutput;

    while (pageIndex < numPages) {
        sizeToRead = (sizeLeftToRead > page_size) ? page_size : sizeLeftToRead;

        tOutpageSizes[pageIndex] = maxOutPageSize;
        pEncoder.Run(srcPtr, sizeToRead, curInOffset, outPtr, &tOutpageSizes[pageIndex], curOutOffset, (pageIndex == numPages - 1));

        outputPageSize.at(pageIndex) = tOutpageSizes[pageIndex];

        sizeLeftToRead -= sizeToRead;
        curInOffset += sizeToRead;
        curOutOffset += (uint32_t)maxOutPageSize;
        ++pageIndex;

        if (feedbackProc)
        {
            float progress = 100.f * ((float)(pageIndex) / numPages);
            if (feedbackProc(BROTLIG_MESSAGE_TYPE::BROTLIG_PROGRESS, std::to_string(progress)))
            {
                break;
            }
        }
    }

    // Prepare page stream
    size_t tcompressedSize = 0;
    outPtr = output;

    StreamHeader header = {};
    header.SetId(BROTLIG_STREAM_ID);
    header.SetPageSize(page_size);
    header.SetUncompressedSize(input_size);
    header.SetPreconditioned(dcParams.precondition);
    size_t headersize = sizeof(StreamHeader);
    memcpy(outPtr, reinterpret_cast<char*>(&header), headersize);
    outPtr += headersize;
    tcompressedSize += headersize;

    if (dcParams.precondition)
    {
        PreconditionHeader preconHeader = {};
        preconHeader.Swizzled = dcParams.swizzle;
        preconHeader.PitchD3D12Aligned = dcParams.pitchd3d12aligned;
        preconHeader.WidthInBlocks = dcParams.widthInBlocks[0] - 1;
        preconHeader.HeightInBlocks = dcParams.heightInBlocks[0] - 1;
        preconHeader.SetDataFormat(dcParams.format);
        preconHeader.NumMips = dcParams.numMipLevels - 1;
        preconHeader.PitchInBytes = dcParams.pitchInBytes[0] - 1;

        size_t preconHeaderSize = sizeof(PreconditionHeader);
        memcpy(outPtr, reinterpret_cast<char*>(&preconHeader), preconHeaderSize);
        outPtr += preconHeaderSize;
        tcompressedSize += preconHeaderSize;
    }

    uint32_t* pageTable = reinterpret_cast<uint32_t*>(outPtr);
    size_t tablesize = numPages * sizeof(uint32_t);
    outPtr += tablesize;
    tcompressedSize += tablesize;
    srcPtr = tOutput;
    curInOffset = 0, curOutOffset = 0;

    for (size_t pindex = 0; pindex < numPages; ++pindex)
    {
        memcpy(outPtr + curOutOffset, srcPtr + curInOffset, tOutpageSizes[pindex]);
        pageTable[pindex] = curOutOffset;
        tcompressedSize += tOutpageSizes[pindex];
        curOutOffset += (uint32_t)tOutpageSizes[pindex];
        curInOffset += (uint32_t)maxOutPageSize;
    }

    pageTable[0] = (uint32_t)tOutpageSizes[numPages - 1];

    srcPtr = nullptr;
    outPtr = nullptr;
    pageTable = nullptr;

    delete[] tOutpageSizes;
    delete[] tOutput;
    delete[] srcConditioned;

    *output_size = (uint32_t)tcompressedSize;
}

void EncodeNoPreconSinglethreaded(
    uint32_t input_size,
    const uint8_t* src,
    uint32_t* output_size,
    uint8_t*& output,
    uint32_t page_size,
    BROTLIG_Feedback_Proc feedbackProc
)
{
    const uint8_t* srcPtr = src;
    uint32_t numPages = (input_size + page_size - 1) / page_size;

    size_t maxOutPageSize = PageEncoder::MaxCompressedSize(page_size);
    uint8_t* tOutput = new uint8_t[maxOutPageSize * numPages];
    size_t* tOutpageSizes = new size_t[numPages];

    std::vector<size_t> outputPageSize(numPages);

    BrotligEncoderParams params = {
        BROTLI_MAX_QUALITY,
        BROTLI_MAX_WINDOW_BITS,
        page_size
    };

    BrotligDataconditionParams dcParams = {};

    PageEncoder pEncoder;
    pEncoder.Setup(params, &dcParams);

    uint32_t pageIndex = 0;
    uint32_t sizeLeftToRead = input_size, sizeToRead = 0, curInOffset = 0, curOutOffset = 0;
    
    uint8_t* outPtr = tOutput;

    while (pageIndex < numPages) {

        sizeToRead = (sizeLeftToRead > page_size) ? page_size : sizeLeftToRead;

        tOutpageSizes[pageIndex] = maxOutPageSize;
        pEncoder.Run(srcPtr, sizeToRead, curInOffset, outPtr, &tOutpageSizes[pageIndex], curOutOffset, (pageIndex == numPages - 1));

        outputPageSize.at(pageIndex) = tOutpageSizes[pageIndex];

        sizeLeftToRead -= sizeToRead;
        curInOffset += sizeToRead;
        curOutOffset += (uint32_t)maxOutPageSize;
        ++pageIndex;

        if (feedbackProc)
        {
            float progress = 100.f * ((float)(pageIndex) / numPages);
            if (feedbackProc(BROTLIG_MESSAGE_TYPE::BROTLIG_PROGRESS, std::to_string(progress)))
            {
                break;
            }
        }
    }

    // Prepare page stream
    size_t tcompressedSize = 0;
    outPtr = output;

    StreamHeader header = {};
    header.SetId(BROTLIG_STREAM_ID);
    header.SetPageSize(page_size);
    header.SetUncompressedSize(input_size);
    header.SetPreconditioned(dcParams.precondition);
    size_t headersize = sizeof(StreamHeader);
    memcpy(outPtr, reinterpret_cast<char*>(&header), headersize);
    outPtr += headersize;
    tcompressedSize += headersize;

    uint32_t* pageTable = reinterpret_cast<uint32_t*>(outPtr);
    size_t tablesize = numPages * sizeof(uint32_t);
    outPtr += tablesize;
    tcompressedSize += tablesize;
    srcPtr = tOutput;
    curInOffset = 0, curOutOffset = 0;

    for (size_t pindex = 0; pindex < numPages; ++pindex)
    {
        memcpy(outPtr + curOutOffset, srcPtr + curInOffset, tOutpageSizes[pindex]);
        pageTable[pindex] = curOutOffset;
        tcompressedSize += tOutpageSizes[pindex];
        curOutOffset += (uint32_t)tOutpageSizes[pindex];
        curInOffset += (uint32_t)maxOutPageSize;
    }

    pageTable[0] = (uint32_t)tOutpageSizes[numPages - 1];

    srcPtr = nullptr;
    outPtr = nullptr;
    pageTable = nullptr;

    delete[] tOutpageSizes;
    delete[] tOutput;

    *output_size = (uint32_t)tcompressedSize;
}

BROTLIG_ERROR EncodeSinglethreaded(
    uint32_t input_size,
    const uint8_t* src,
    uint32_t* output_size,
    uint8_t*& output,
    uint32_t page_size,
    BrotligDataconditionParams dcParams,
    BROTLIG_Feedback_Proc feedbackProc)
{
    BROTLIG_ERROR status = BrotliG::CheckParams(page_size, dcParams);

    if (status != BROTLIG_OK) return status;

    if (dcParams.precondition)
    {
        if (!dcParams.Initialize(input_size))
        {
            dcParams.precondition = false;

            if (feedbackProc)
            {
                std::string msg = "Warning: Incorrect texture format. Preconditioning not applied.";
                feedbackProc(BROTLIG_MESSAGE_TYPE::BROTLIG_WARNING, msg);
            }
        }
    }

    if (dcParams.precondition)
        EncodeWithPreconSinglethreaded(
            input_size,
            src,
            output_size,
            output,
            page_size,
            dcParams,
            feedbackProc
        );
    else
      EncodeNoPreconSinglethreaded(
            input_size,
            src,
            output_size,
            output,
            page_size,
            feedbackProc
        );

    return BROTLIG_OK;
}

static void PageEncoderJob(PageEncoderCtx& ctx, BrotligEncoderParams& params, BrotligDataconditionParams& dcParams)
{
    PageEncoder pEncoder;
    pEncoder.Setup(params, &dcParams);

    uint32_t curInOffset = 0, curOutOffset = 0;
    size_t inPageSize = 0;
    while (true)
    {
        const uint32_t pageIndex = ctx.globalIndex.fetch_add(1, std::memory_order_relaxed);

        if (pageIndex >= ctx.numPages)
            break;

        curInOffset = pageIndex * (uint32_t)params.page_size;
        inPageSize = (pageIndex < ctx.numPages - 1) ? params.page_size : ctx.lastPageSize;

        curOutOffset = pageIndex * ctx.maxOutPageSize;
        ctx.outPageSizes[pageIndex] = ctx.maxOutPageSize;

        pEncoder.Run(ctx.inputPtr, inPageSize, curInOffset, ctx.outputPtr, &ctx.outPageSizes[pageIndex], curOutOffset, (pageIndex == ctx.numPages - 1));

        if (ctx.feedbackProc)
        {
            float progress = 100.f * ((float)(pageIndex) / ctx.numPages);
            if (ctx.feedbackProc(BROTLIG_MESSAGE_TYPE::BROTLIG_PROGRESS, std::to_string(progress)))
            {
                break;
            }
        }
    }

    pEncoder.Cleanup();
}

void EncodeWithPreconMultithreaded(
    uint32_t input_size,
    const uint8_t* src,
    uint32_t* output_size,
    uint8_t*& output,
    uint32_t page_size,
    BrotligDataconditionParams& dcParams,
    BROTLIG_Feedback_Proc feedbackProc
)
{
    uint8_t* srcConditioned = nullptr;
    uint32_t srcCondSize = 0;

    BrotliG::Condition(input_size, src, dcParams, srcCondSize, srcConditioned);

    size_t maxOutPageSize = PageEncoder::MaxCompressedSize(page_size);

    PageEncoderCtx ctx{};
    ctx.globalIndex = 0;
    ctx.maxOutPageSize = (uint32_t)maxOutPageSize;
    ctx.inputPtr = srcConditioned;
    ctx.numPages = (srcCondSize + page_size - 1) / page_size;
    ctx.lastPageSize = srcCondSize - ((ctx.numPages - 1) * page_size);
    ctx.outputPtr = new uint8_t[maxOutPageSize * ctx.numPages];
    ctx.outPageSizes = new size_t[ctx.numPages];
    ctx.feedbackProc = feedbackProc;

    BrotligEncoderParams params = {
        BROTLI_MAX_QUALITY,
        BROTLI_MAX_WINDOW_BITS,
        page_size
    };

    const uint32_t maxWorkers = std::min(static_cast<unsigned int>(BROTLIG_MAX_WORKERS), BrotliG::GetNumberOfProcessorsThreads());
    std::thread workers[BROTLIG_MAX_WORKERS];

    uint32_t numWorkersLeft = (ctx.numPages > 2 * maxWorkers) ? maxWorkers : 1;
    for (auto& worker : workers)
    {
        if (numWorkersLeft == 1)
            break;

        worker = std::thread([&ctx, &params, &dcParams]() {PageEncoderJob(ctx, params, dcParams); });
        --numWorkersLeft;
    }

    PageEncoderJob(ctx, params, dcParams);

    for (auto& worker : workers)
    {
        if (worker.joinable())
            worker.join();
    }

    // Prepare page stream
    size_t tcompressedSize = 0;
    uint8_t* outPtr = output;

    StreamHeader header = {};
    header.SetId (BROTLIG_STREAM_ID);
    header.SetPageSize (page_size);
    header.SetUncompressedSize (input_size);
    header.SetPreconditioned (dcParams.precondition);
    size_t headersize = sizeof(StreamHeader);
    memcpy(outPtr, reinterpret_cast<char*>(&header), headersize);
    outPtr += headersize;
    tcompressedSize += headersize;

    if (dcParams.precondition)
    {
        PreconditionHeader preconHeader = {};
        preconHeader.Swizzled           = dcParams.swizzle;
        preconHeader.PitchD3D12Aligned  = dcParams.pitchd3d12aligned;
        preconHeader.WidthInBlocks      = dcParams.widthInBlocks[0] - 1;
        preconHeader.HeightInBlocks     = dcParams.heightInBlocks[0] - 1;
        preconHeader.SetDataFormat (dcParams.format);
        preconHeader.NumMips            = dcParams.numMipLevels - 1;
        preconHeader.PitchInBytes       = dcParams.pitchInBytes[0] - 1;

        size_t preconHeaderSize = sizeof(PreconditionHeader);
        memcpy(outPtr, reinterpret_cast<char*>(&preconHeader), preconHeaderSize);
        outPtr += preconHeaderSize;
        tcompressedSize += preconHeaderSize;
    }

    uint32_t* pageTable = reinterpret_cast<uint32_t*>(outPtr);
    size_t tablesize = ctx.numPages * sizeof(uint32_t);
    outPtr += tablesize;
    tcompressedSize += tablesize;
    const uint8_t* srcPtr = ctx.outputPtr;
    uint32_t curInOffset = 0, curOutOffset = 0;

    for (size_t pindex = 0; pindex < ctx.numPages; ++pindex)
    {
        memcpy(outPtr + curOutOffset, srcPtr + curInOffset, ctx.outPageSizes[pindex]);
        pageTable[pindex] = curOutOffset;
        tcompressedSize += ctx.outPageSizes[pindex];
        curOutOffset += (uint32_t)ctx.outPageSizes[pindex];
        curInOffset += (uint32_t)maxOutPageSize;
    }

    pageTable[0] = (uint32_t)ctx.outPageSizes[ctx.numPages - 1];

    delete[] ctx.outPageSizes;
    delete[] ctx.outputPtr;
    delete[] srcConditioned;

    *output_size = (uint32_t)tcompressedSize;
}

void EncodeNoPreconMultithreaded(
    uint32_t input_size,
    const uint8_t* src,
    uint32_t* output_size,
    uint8_t*& output,
    uint32_t page_size,
    BROTLIG_Feedback_Proc feedbackProc
)
{
    size_t maxOutPageSize = PageEncoder::MaxCompressedSize(page_size);

    PageEncoderCtx ctx{};
    ctx.globalIndex = 0;
    ctx.maxOutPageSize = (uint32_t)maxOutPageSize;
    ctx.inputPtr = src;
    ctx.numPages = (input_size + page_size - 1) / page_size;
    ctx.lastPageSize = input_size - ((ctx.numPages - 1) * page_size);
    ctx.outputPtr = new uint8_t[maxOutPageSize * ctx.numPages];
    ctx.outPageSizes = new size_t[ctx.numPages];
    ctx.feedbackProc = feedbackProc;

    BrotligEncoderParams params = {
        BROTLI_MAX_QUALITY,
        BROTLI_MAX_WINDOW_BITS,
        page_size
    };

    BrotligDataconditionParams dcParams = {};

    const uint32_t maxWorkers = std::min(static_cast<unsigned int>(BROTLIG_MAX_WORKERS), BrotliG::GetNumberOfProcessorsThreads());
    std::thread workers[BROTLIG_MAX_WORKERS];

    uint32_t numWorkersLeft = (ctx.numPages > 2 * maxWorkers) ? maxWorkers : 1;
    for (auto& worker : workers)
    {
        if (numWorkersLeft == 1)
            break;

        worker = std::thread([&ctx, &params, &dcParams]() {PageEncoderJob(ctx, params, dcParams); });
        --numWorkersLeft;
    }

    PageEncoderJob(ctx, params, dcParams);

    for (auto& worker : workers)
    {
        if (worker.joinable())
            worker.join();
    }

    // Prepare page stream
    size_t tcompressedSize = 0;
    uint8_t* outPtr = output;

    StreamHeader header = {};
    header.SetId(BROTLIG_STREAM_ID);
    header.SetPageSize(page_size);
    header.SetUncompressedSize(input_size);
    header.SetPreconditioned(dcParams.precondition);
    size_t headersize = sizeof(StreamHeader);
    memcpy(outPtr, reinterpret_cast<char*>(&header), headersize);
    outPtr += headersize;
    tcompressedSize += headersize;

    uint32_t* pageTable = reinterpret_cast<uint32_t*>(outPtr);
    size_t tablesize = ctx.numPages * sizeof(uint32_t);
    outPtr += tablesize;
    tcompressedSize += tablesize;
    const uint8_t* srcPtr = ctx.outputPtr;
    uint32_t curInOffset = 0, curOutOffset = 0;

    for (size_t pindex = 0; pindex < ctx.numPages; ++pindex)
    {
        memcpy(outPtr + curOutOffset, srcPtr + curInOffset, ctx.outPageSizes[pindex]);
        pageTable[pindex] = curOutOffset;
        tcompressedSize += ctx.outPageSizes[pindex];
        curOutOffset += (uint32_t)ctx.outPageSizes[pindex];
        curInOffset += (uint32_t)maxOutPageSize;
    }

    pageTable[0] = (uint32_t)ctx.outPageSizes[ctx.numPages - 1];

    delete[] ctx.outPageSizes;
    delete[] ctx.outputPtr;

    *output_size = (uint32_t)tcompressedSize;
}

BROTLIG_ERROR EncodeMultithreaded(
    uint32_t input_size,
    const uint8_t* src,
    uint32_t* output_size,
    uint8_t*& output,
    uint32_t page_size,
    BrotligDataconditionParams dcParams,
    BROTLIG_Feedback_Proc feedbackProc)
{
    BROTLIG_ERROR status = BrotliG::CheckParams(page_size, dcParams);

    if (status != BROTLIG_OK) return status;

    if (dcParams.precondition)
    {
        if (!dcParams.Initialize(input_size))
        {
            dcParams.precondition = false;

            if (feedbackProc)
            {
                std::string msg = "Warning: Incorrect texture format. Preconditioning not applied.";
                feedbackProc(BROTLIG_MESSAGE_TYPE::BROTLIG_WARNING, msg);
            }
        }
    }

    if (dcParams.precondition)
        EncodeWithPreconMultithreaded(
            input_size,
            src,
            output_size,
            output,
            page_size,
            dcParams,
            feedbackProc
        );
    else
        EncodeNoPreconMultithreaded(
            input_size,
            src,
            output_size,
            output,
            page_size,
            feedbackProc
        );

    return BROTLIG_OK;
}

BROTLIG_ERROR BROTLIG_API BrotliG::Encode(
    uint32_t input_size,
    const uint8_t* src,
    uint32_t* output_size,
    uint8_t*& output,
    uint32_t page_size,
    BrotligDataconditionParams dcParams,
    BROTLIG_Feedback_Proc feedbackProc)
{
#if BROTLIG_ENCODER_MULTITHREADING_MODE
    return EncodeMultithreaded(
        input_size,
        src,
        output_size,
        output,
        page_size,
        dcParams,
        feedbackProc
    );
#else
    return EncodeSinglethreaded(
        input_size,
        src,
        output_size,
        output,
        page_size,
        dcParams,
        feedbackProc
    );
#endif // BROTLIG_ENCODER_MULTITHREADED
}