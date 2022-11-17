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
#include "encoder/PageEncoder.h"

#include "BrotliG.h"
#include "BrotligEncoder.h"

using namespace BrotliG;

uint32_t BROTLIG_API BrotliG::MaxCompressedSize(uint32_t input_size)
{
    uint32_t numPages = (input_size + BROTLIG_MIN_PAGE_SIZE - 1) / (BROTLIG_MIN_PAGE_SIZE);
    uint32_t compressedPagesSize = BROTLIG_MIN_PAGE_SIZE + 1 + 36;

    return numPages * compressedPagesSize + sizeof(StreamHeader);
}

namespace BrotliG {
    struct PageEncoderCtx
    {
        const uint8_t* inputPtr;
        size_t input_size;

        std::vector<BrotligByteBuffer> outputPages;

        uint32_t numPages;
        size_t page_size;

        std::atomic_uint32_t globalIndex;
    };
}

BROTLIG_ERROR EncodeSinglethreaded(
    uint32_t input_size,
    const uint8_t* src,
    uint32_t* output_size,
    uint8_t* output,
    BROTLIG_Feedback_Proc feedbackProc)
{
    uint32_t page_size = BROTLIG_DEFAULT_PAGE_SIZE;
    uint32_t TotalBlocks = (input_size + page_size - 1) / page_size;
    size_t   sizeLeftToRead = input_size;

    uint32_t processingBlock = 0;
    size_t   sizeToRead = 0;
    size_t   curoffset = 0;

    std::vector<BrotligByteBuffer> outputPages;
    outputPages.resize(TotalBlocks);

    PageEncoder encoder;

    BrotligEncoderParams params = {
        page_size,
        BROLTIG_NUM_BITSTREAMS,
        BROTLIG_COMMAND_GROUP_SIZE,
        BROTLIG_SWIZZLE_SIZE
    };

    while (processingBlock < TotalBlocks) {

        sizeToRead = (sizeLeftToRead > page_size) ? page_size : sizeLeftToRead;

        uint32_t flags = (processingBlock == (TotalBlocks - 1)) ? 1 : 0;

        encoder.Setup(src + curoffset, sizeToRead, static_cast<void*>(&params), flags, 0);

        encoder.Run();

        outputPages.at(processingBlock).data = encoder.GetOutput();
        outputPages.at(processingBlock).size = encoder.GetOutputSize();

        encoder.Cleanup();

        sizeLeftToRead -= sizeToRead;
        curoffset += sizeToRead;
        processingBlock++;
    }

    // Prepare page stream header and page table
    size_t tcompressedSize = 0;

    StreamHeader header;
    header.SetId(BROTLIG_STREAM_ID);
    header.SetPageSize(page_size);
    header.SetUncompressedSize(input_size);

    std::vector<uint32_t> pageTable;

    size_t compressedSize = 0;
    size_t outcuroffset = 0;

    for (size_t pindex = 0; pindex < TotalBlocks; ++pindex)
    {
        compressedSize += outputPages.at(pindex).size;
        pageTable.push_back(static_cast<uint32_t>(outcuroffset));
        outcuroffset += outputPages.at(pindex).size;
    }

    pageTable.at(0) = static_cast<uint32_t>(outputPages.at(outputPages.size() - 1).size);
    // Prepare the compressed stream
    size_t headerSize = sizeof(StreamHeader);
    size_t tableSize = TotalBlocks * sizeof(uint32_t);

    size_t compressedStreamSize = compressedSize + headerSize + tableSize;
    tcompressedSize += compressedStreamSize;

    memcpy(output, reinterpret_cast<char*>(&header), headerSize);
    outcuroffset = headerSize;
    uint32_t* pTable = pageTable.data();
    memcpy(output + outcuroffset, reinterpret_cast<char*>(pTable), tableSize);
    outcuroffset += tableSize;
    for (size_t pindex = 0; pindex < TotalBlocks; ++pindex)
    {
        memcpy(output + outcuroffset, outputPages.at(pindex).data, outputPages.at(pindex).size);
        outcuroffset += outputPages.at(pindex).size;
    }

    outputPages.clear();
    pageTable.clear();

    *output_size = (uint32_t)tcompressedSize;
    return BROTLIG_OK;
}

BROTLIG_ERROR EncodeMultithreadedVersion1(
    uint32_t input_size,
    const uint8_t* src,
    uint32_t* output_size,
    uint8_t* output,
    BROTLIG_Feedback_Proc feedbackProc)
{
    uint32_t page_size = BROTLIG_DEFAULT_PAGE_SIZE;

    PageEncoderCtx ctx{};
    ctx.inputPtr = src;
    ctx.input_size = input_size;
    ctx.page_size = page_size;
    ctx.numPages = (input_size + page_size - 1) / page_size;
    ctx.outputPages.resize(ctx.numPages);

    auto PageEncoderJob = [&ctx]()
    {
        PageEncoder pEncoder;
        BrotligEncoderParams* params = new BrotligEncoderParams(
            ctx.page_size,
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
            uint32_t curroffset = pageIndex * static_cast<uint32_t>(ctx.page_size);
            pagePtr = ctx.inputPtr + curroffset;
            
            size_t sizeToRead = 0;
            uint32_t flags = 0;
            if (pageIndex < ctx.numPages - 1)
            {
                sizeToRead = ctx.page_size;
            }
            else
            {
                sizeToRead = ctx.input_size - curroffset;
                assert(sizeToRead <= ctx.page_size);
                flags = (pageIndex == (ctx.numPages - 1)) ? 1 : 0;
            }

            pEncoder.Setup(pagePtr, sizeToRead, static_cast<void*>(params), flags, 0);
            pEncoder.Run();

            ctx.outputPages.at(pageIndex).data = pEncoder.GetOutput();
            ctx.outputPages.at(pageIndex).size = pEncoder.GetOutputSize();

            pEncoder.Cleanup();
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

        worker = std::thread([PageEncoderJob]() {PageEncoderJob(); });
        --numWorkersLeft;
    }

    for (auto& worker : workers)
    {
        if (worker.joinable())
            worker.join();
    }

    // Prepare page stream header and page table
    size_t tcompressedSize = 0;

    StreamHeader header;
    header.SetId(BROTLIG_STREAM_ID);
    header.SetPageSize(page_size);
    header.SetUncompressedSize(input_size);

    std::vector<uint32_t> pageTable;

    size_t compressedSize = 0;
    size_t outcuroffset = 0;

    for (size_t pindex = 0; pindex < ctx.numPages; ++pindex)
    {
        compressedSize += ctx.outputPages.at(pindex).size;
        pageTable.push_back(static_cast<uint32_t>(outcuroffset));
        outcuroffset += ctx.outputPages.at(pindex).size;
    }

    pageTable.at(0) = static_cast<uint32_t>(ctx.outputPages.at(ctx.outputPages.size() - 1).size);
    // Prepare the compressed stream
    size_t headerSize = sizeof(StreamHeader);
    size_t tableSize = ctx.numPages * sizeof(uint32_t);

    size_t compressedStreamSize = compressedSize + headerSize + tableSize;
    tcompressedSize += compressedStreamSize;

    memcpy(output, reinterpret_cast<char*>(&header), headerSize);
    outcuroffset = headerSize;
    uint32_t* pTable = pageTable.data();
    memcpy(output + outcuroffset, reinterpret_cast<char*>(pTable), tableSize);
    outcuroffset += tableSize;
    for (size_t pindex = 0; pindex < ctx.numPages; ++pindex)
    {
        memcpy(output + outcuroffset, ctx.outputPages.at(pindex).data, ctx.outputPages.at(pindex).size);
        outcuroffset += ctx.outputPages.at(pindex).size;
    }

    ctx.outputPages.clear();
    pageTable.clear();

    *output_size = (uint32_t)tcompressedSize;

    return BROTLIG_OK;
}

class BrotligMultithreadEncoder : public BrotligMultithreader
{
private:
    BROTLIG_ERROR    InitializeThreads() {
        for (uint32_t i = 0; i < m_NumThreads; ++i) {
            // Create single encoder instance
            m_workers[i] = new BrotliG::PageEncoder();


            // Cleanup if problem!
            if (!m_workers[i]) {

                delete[] m_ParameterStorage;
                m_ParameterStorage = nullptr;

                delete[] m_ThreadHandle;
                m_ThreadHandle = nullptr;

                for (uint32_t j = 0; j < i; j++) {
                    delete m_workers[j];
                    m_workers[j] = nullptr;
                }

                return BROTLIG_ERROR_GENERIC;
            }

        }

        return BROTLIG_OK;
    }

    void*   GenerateParamSet(uint32_t* userparams, size_t num_userparams)
    {
        BrotligEncoderParams* params = new BrotligEncoderParams (
            userparams[0],
            BROLTIG_NUM_BITSTREAMS,
            BROTLIG_COMMAND_GROUP_SIZE,
            BROTLIG_SWIZZLE_SIZE
        );

        return static_cast<void*>(params);
    }
};

BROTLIG_ERROR EncodeMultithreadedVersion2(
    uint32_t input_size, 
    const uint8_t* src, 
    uint32_t* output_size, 
    uint8_t* output, 
    BROTLIG_Feedback_Proc feedbackProc)
{
    uint32_t page_size = BROTLIG_DEFAULT_PAGE_SIZE;

    uint32_t TotalBlocks = (input_size + page_size - 1) / page_size;
    size_t   sizeLeftToRead = input_size;

    uint32_t processingBlock = 0;
    size_t   sizeToRead = 0;
    size_t   curoffset = 0;

    std::vector<BrotligByteBuffer> outputPages;
    outputPages.resize(TotalBlocks);

    BrotligMultithreadEncoder encoder;

    BROTLIG_ERROR err = encoder.Initialize();
    if (err != BROTLIG_OK) return err;

    uint32_t* userparams = new uint32_t[1];
    userparams[0] = page_size;

    while (processingBlock < TotalBlocks) {
        sizeToRead = (sizeLeftToRead > page_size) ? page_size : sizeLeftToRead;

        uint32_t flags = (processingBlock == (TotalBlocks - 1)) ? 1 : 0;

        encoder.ProcessBlock(src + curoffset, sizeToRead, &outputPages.at(processingBlock).data, &outputPages.at(processingBlock).size, userparams, 1, flags, 0);

        if (feedbackProc) {
            float fProgress = 100.f * ((float)(processingBlock) / TotalBlocks);
            if (feedbackProc(fProgress)) {
                break;
            }
        }

        sizeLeftToRead -= sizeToRead;
        curoffset += sizeToRead;
        processingBlock++;
    }

    BROTLIG_ERROR EncodeResult = encoder.FinishBlocks();

    delete[] userparams;

    if (EncodeResult != BROTLIG_OK)
        return EncodeResult;

    // Prepare page stream header and page table
    size_t tcompressedSize = 0;

    StreamHeader header;
    header.SetId(BROTLIG_STREAM_ID);
    header.SetPageSize(page_size);
    header.SetUncompressedSize(input_size);

    std::vector<uint32_t> pageTable;

    size_t compressedSize = 0;
    size_t outcuroffset = 0;

    for (size_t pindex = 0; pindex < TotalBlocks; ++pindex)
    {
        compressedSize += outputPages.at(pindex).size;
        pageTable.push_back(static_cast<uint32_t>(outcuroffset));
        outcuroffset += outputPages.at(pindex).size;
    }

    pageTable.at(0) = static_cast<uint32_t>(outputPages.at(outputPages.size() - 1).size);
    // Prepare the compressed stream
    size_t headerSize = sizeof(StreamHeader);
    size_t tableSize = TotalBlocks * sizeof(uint32_t);

    size_t compressedStreamSize = compressedSize + headerSize + tableSize;
    tcompressedSize += compressedStreamSize;

    memcpy(output, reinterpret_cast<char*>(&header), headerSize);
    outcuroffset = headerSize;
    uint32_t* pTable = pageTable.data();
    memcpy(output + outcuroffset, reinterpret_cast<char*>(pTable), tableSize);
    outcuroffset += tableSize;
    for (size_t pindex = 0; pindex < TotalBlocks; ++pindex)
    {
        memcpy(output + outcuroffset, outputPages.at(pindex).data, outputPages.at(pindex).size);
        outcuroffset += outputPages.at(pindex).size;
    }

    outputPages.clear();
    pageTable.clear();

    *output_size = (uint32_t)tcompressedSize;
    return BROTLIG_OK;
}

BROTLIG_ERROR BROTLIG_API BrotliG::Encode(
    uint32_t input_size,
    const uint8_t* src,
    uint32_t* output_size,
    uint8_t* output,
    BROTLIG_Feedback_Proc feedbackProc)
{
#if BROTLIG_ENCODER_MULTITHREADED
#if BROTLIG_ENCODER_MULITHREADED_VERSION
    return EncodeMultithreadedVersion2(
        input_size,
        src,
        output_size,
        output,
        feedbackProc
    );
#else
    return EncodeMultithreadedVersion1(
        input_size,
        src,
        output_size,
        output,
        feedbackProc
    );
#endif
#else
    return EncodeSinglethreaded(
        input_size,
        src,
        output_size,
        output,
        feedbackProc
    );
#endif // BROTLIG_ENCODER_MULTITHREADED

}
