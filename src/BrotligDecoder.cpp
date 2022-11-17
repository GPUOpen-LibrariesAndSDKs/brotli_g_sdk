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


#include <iostream>
#include <thread>

#include "common/BrotligConstants.h"
#include "common/BrotligMultithreading.h"
#include "DataStream.h"
#include "decoder/PageDecoder.h"

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
        std::vector<BrotligByteBuffer> outputPages;

        uint32_t* pageTable;
        uint32_t numPages;

        size_t pageSize;
        size_t lastPageSize;

        std::atomic_uint32_t globalIndex;
    };
}

BROTLIG_ERROR DecodeCPUSingleThreaded(
    uint32_t input_size,
    const uint8_t* src,
    uint32_t* output_size,
    uint8_t* output,
    BROTLIG_Feedback_Proc feedbackProc)
{
    uint32_t TotalPages = 0;
    const uint8_t* srcPtr = src;
    size_t sizeLeftToRead = input_size;

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

    PageDecoderCtx ctx{};
    ctx.globalIndex = 0;
    ctx.pageSize = sHeader->PageSize();
    ctx.lastPageSize = sHeader->LastPageSize;
    ctx.numPages = sHeader->NumPages;

    size_t headerSize = sizeof(StreamHeader);
    srcPtr += headerSize;
    sizeLeftToRead -= headerSize;

    // Read the page table
    ctx.pageTable = new uint32_t[ctx.numPages];
    size_t tableSize = ctx.numPages * sizeof(uint32_t);
    memcpy(ctx.pageTable, srcPtr, tableSize);
    srcPtr += tableSize;
    sizeLeftToRead -= tableSize;

    ctx.inputPtr = srcPtr;
    ctx.outputPages.resize(ctx.numPages);

    PageDecoder pDecoder;
    BrotligDecoderParams* params = new BrotligDecoderParams(
        ctx.pageSize,
        BROLTIG_NUM_BITSTREAMS,
        BROTLIG_COMMAND_GROUP_SIZE,
        BROTLIG_SWIZZLE_SIZE
    );

    while (true)
    {
        const uint32_t pageIndex = ctx.globalIndex.fetch_add(1, std::memory_order_relaxed);

        if (pageIndex >= ctx.numPages)
            break;

        const uint8_t* pagePtr = nullptr;
        uint32_t curoffset = (pageIndex == 0) ? 0 : ctx.pageTable[pageIndex];
        pagePtr = ctx.inputPtr + curoffset;

        size_t sizeToRead = 0;
        if (pageIndex < ctx.numPages - 1)
        {
            uint32_t nextoffset = ctx.pageTable[pageIndex + 1];
            sizeToRead = nextoffset - curoffset;
        }
        else
        {
            sizeToRead = ctx.pageTable[0];
        }

        uint32_t extra = ((pageIndex == (ctx.numPages - 1)) && (ctx.lastPageSize != 0))
            ? static_cast<uint32_t>(ctx.lastPageSize)
            : static_cast<uint32_t>(ctx.pageSize);

        pDecoder.Setup(pagePtr, sizeToRead, static_cast<void*>(params), 0, extra);
        pDecoder.Run();

        ctx.outputPages.at(pageIndex).data = pDecoder.GetOutput();
        ctx.outputPages.at(pageIndex).size = pDecoder.GetOutputSize();

        pDecoder.Cleanup();
    }

    delete params;

    size_t tDecompressedSize = sHeader->UncompressedSize();
    size_t outcuroffset = 0;
    for (size_t pindex = 0; pindex < ctx.numPages; ++pindex)
    {
        memcpy(output + outcuroffset, ctx.outputPages.at(pindex).data, ctx.outputPages.at(pindex).size);
        outcuroffset += ctx.outputPages.at(pindex).size;
    }

    ctx.outputPages.clear();
    delete ctx.pageTable;

    *output_size = (uint32_t)tDecompressedSize;

    return BROTLIG_OK;
}

BROTLIG_ERROR DecodeCPUMultithreadedVersion1(
    uint32_t input_size, 
    const uint8_t* src, 
    uint32_t* output_size, 
    uint8_t* output, 
    BROTLIG_Feedback_Proc feedbackProc)
{
    uint32_t TotalPages = 0;
    const uint8_t* srcPtr = src;
    size_t sizeLeftToRead = input_size;

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

    PageDecoderCtx ctx{};
    ctx.globalIndex = 0;
    ctx.pageSize = sHeader->PageSize();
    ctx.lastPageSize = sHeader->LastPageSize;
    ctx.numPages = sHeader->NumPages;

    size_t headerSize = sizeof(StreamHeader);
    srcPtr += headerSize;
    sizeLeftToRead -= headerSize;

    // Read the page table
    ctx.pageTable = new uint32_t[ctx.numPages];
    size_t tableSize = ctx.numPages * sizeof(uint32_t);
    memcpy(ctx.pageTable, srcPtr, tableSize);
    srcPtr += tableSize;
    sizeLeftToRead -= tableSize;

    ctx.inputPtr = srcPtr;
    ctx.outputPages.resize(ctx.numPages);

    auto PageDecoderJob = [&ctx]()
    {
        PageDecoder pDecoder;
        BrotligDecoderParams* params = new BrotligDecoderParams(
            ctx.pageSize,
            BROLTIG_NUM_BITSTREAMS,
            BROTLIG_COMMAND_GROUP_SIZE,
            BROTLIG_SWIZZLE_SIZE
        );

        while (true)
        {
            const uint32_t pageIndex = ctx.globalIndex.fetch_add(1, std::memory_order_relaxed);
        
            if (pageIndex >= ctx.numPages)
                break;

            const uint8_t* pagePtr = nullptr;
            uint32_t curoffset = (pageIndex == 0) ? 0 : ctx.pageTable[pageIndex];
            pagePtr = ctx.inputPtr + curoffset;

            size_t sizeToRead = 0;
            if (pageIndex < ctx.numPages - 1)
            {
                uint32_t nextoffset = ctx.pageTable[pageIndex + 1];
                sizeToRead = nextoffset - curoffset;
            }
            else
            {
                sizeToRead = ctx.pageTable[0];
            }

            uint32_t extra = ((pageIndex == (ctx.numPages - 1)) && (ctx.lastPageSize != 0))
                ? static_cast<uint32_t>(ctx.lastPageSize)
                : static_cast<uint32_t>(ctx.pageSize);

            pDecoder.Setup(pagePtr, sizeToRead, static_cast<void*>(params), 0, extra);
            pDecoder.Run();

            ctx.outputPages.at(pageIndex).data = pDecoder.GetOutput();
            ctx.outputPages.at(pageIndex).size = pDecoder.GetOutputSize();

            pDecoder.Cleanup();
        }

        delete params;
    };

    const uint32_t maxWorkers = std::min(static_cast<unsigned int>(BROTLIG_MAX_WORKERS), BrotliG::GetNumberOfProcessorsThreads());
    std::vector<std::thread> workers(maxWorkers);

    uint32_t numWorkersLeft = maxWorkers;
    for (auto& worker : workers)
    {
        if (numWorkersLeft == 0)
            break;
        
        worker = std::thread([PageDecoderJob]() {PageDecoderJob(); });
        --numWorkersLeft;
    }

    for (auto& worker : workers)
    {
        if (worker.joinable())
            worker.join();
    }

    size_t tDecompressedSize = sHeader->UncompressedSize();
    size_t outcuroffset = 0;
    for (size_t pindex = 0; pindex < ctx.numPages; ++pindex)
    {
        memcpy(output + outcuroffset, ctx.outputPages.at(pindex).data, ctx.outputPages.at(pindex).size);
        outcuroffset += ctx.outputPages.at(pindex).size;
    }

    ctx.outputPages.clear();
    delete ctx.pageTable;

    *output_size = (uint32_t)tDecompressedSize;

    return BROTLIG_OK;
}

class BrotligMultithreadDecoder : public BrotligMultithreader
{
private:
    BROTLIG_ERROR   InitializeThreads() {
        for (uint32_t i = 0; i < m_NumThreads; ++i) {
            // Create single decoder instance
            m_workers[i] = new BrotliG::PageDecoder();

            // Cleanup if problem!
            if (!m_workers[i]) {

                delete[] m_ParameterStorage;
                m_ParameterStorage = nullptr;

                delete[] m_ThreadHandle;
                m_ThreadHandle = nullptr;

                for (uint32_t j = 0; j < i; j++) {
                    delete m_workers[i];
                    m_workers[j] = nullptr;
                }

                return BROTLIG_ERROR_GENERIC;
            }
        }

        return BROTLIG_OK;
    }

    void* GenerateParamSet(uint32_t* userparams, size_t num_userparams)
    {
        BrotligDecoderParams* params = new BrotligDecoderParams(
            userparams[0],
            BROLTIG_NUM_BITSTREAMS,
            BROTLIG_COMMAND_GROUP_SIZE,
            BROTLIG_SWIZZLE_SIZE
        );

        return static_cast<void*>(params);
    }
};


BROTLIG_ERROR DecodeCPUMultithreadedVersion2(
    uint32_t input_size, 
    const uint8_t* src, 
    uint32_t* output_size, 
    uint8_t* output, 
    BROTLIG_Feedback_Proc feedbackProc)
{
    uint32_t processingBlock = 0;
    uint32_t TotalBlocks = 0;
    const uint8_t* srcPtr = src;
    size_t sizeLeftToRead = input_size;

    // Read the header
    const StreamHeader* sheader = reinterpret_cast<const StreamHeader*>(srcPtr);
    if (!sheader->Validate())
    {
        return BROTLIG_ERROR_CORRUPT_STREAM;
    }

    if (sheader->Id != BROTLIG_STREAM_ID)
    {
        return BROTLIG_ERROR_INCORRECT_STREAM_FORMAT;
    }

    size_t headerSize = sizeof(StreamHeader);
    srcPtr += headerSize;
    sizeLeftToRead -= headerSize;
    TotalBlocks = sheader->NumPages;

    // Reader the page table
    std::vector<uint32_t> blockTable(TotalBlocks);
    size_t tableSize = TotalBlocks * sizeof(uint32_t);
    memcpy(blockTable.data(), srcPtr, tableSize);
    srcPtr += tableSize;
    sizeLeftToRead -= tableSize;

    size_t sizeToRead = 0;
    uint32_t pageindex = 0;
    size_t curoffset = 0;

    std::vector<BrotligByteBuffer> outputPages;
    outputPages.resize(TotalBlocks);

    BrotligMultithreadDecoder decoder;

    BROTLIG_ERROR err = decoder.Initialize();
    if (err != BROTLIG_OK) return err;

    uint32_t* userparams = new uint32_t[1];
    userparams[0] = static_cast<uint32_t>(sheader->PageSize());

    while (processingBlock < TotalBlocks)
    {
        if (processingBlock < blockTable.size() - 1)
        {
            uint32_t nextoffset = blockTable.at(processingBlock + 1);
            sizeToRead = nextoffset - curoffset;
            curoffset = nextoffset;
        }
        else
        {
            sizeToRead = blockTable.at(0);
        }

        uint32_t extra = ((processingBlock == (TotalBlocks - 1)) && (sheader->LastPageSize != 0))
            ? sheader->LastPageSize
            : static_cast<uint32_t>(sheader->PageSize());

        decoder.ProcessBlock(srcPtr, sizeToRead, &outputPages.at(processingBlock).data, &outputPages.at(processingBlock).size, userparams, 1, 0, extra);
        
        if (feedbackProc)
        {
            float fProgess = 100.f * ((float)(processingBlock) / TotalBlocks);
            if (feedbackProc(fProgess)) {
                break;
            }
        }

        srcPtr += sizeToRead;
        processingBlock++;
    }

    BROTLIG_ERROR DecodeResult = decoder.FinishBlocks();

    delete[] userparams;

    if (DecodeResult != BROTLIG_OK)
        return DecodeResult;

    // Prepare the output stream
    size_t tDecompressedSize = sheader->UncompressedSize();

    size_t outcuroffset = 0;
    for (size_t pindex = 0; pindex < TotalBlocks; ++pindex)
    {
        memcpy(output + outcuroffset, outputPages.at(pindex).data, outputPages.at(pindex).size);
        outcuroffset += outputPages.at(pindex).size;
    }
    
    outputPages.clear();
    blockTable.clear();

    *output_size = (uint32_t)tDecompressedSize;
    return BROTLIG_OK;
}

BROTLIG_ERROR BROTLIG_API BrotliG::DecodeCPU(
    uint32_t input_size,
    const uint8_t* src,
    uint32_t* output_size,
    uint8_t* output,
    BROTLIG_Feedback_Proc feedbackProc)
{
#if BROTLIG_CPU_DECODER_MULTITHREADED
#if BROTLIG_CPU_DECODER_MULITHREADED_VERSION
    return DecodeCPUMultithreadedVersion2(
        input_size,
        src,
        output_size,
        output,
        feedbackProc
    );
#else
    return DecodeCPUMultithreadedVersion1(
        input_size,
        src,
        output_size,
        output,
        feedbackProc
    );
#endif
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
