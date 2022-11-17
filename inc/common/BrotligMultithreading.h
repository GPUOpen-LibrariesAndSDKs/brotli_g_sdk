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

#include <thread>

#include "BrotligCommon.h"
#include "DataStream.h"

namespace BrotliG
{
    class BrotligWorker
    {
    protected:
        const uint8_t* m_input;
        size_t m_inputSize;

        uint8_t* m_output;
        size_t m_outputSize;

    public:
        BrotligWorker()
            : m_input(nullptr)
            , m_inputSize(0)
            , m_output(nullptr)
            , m_outputSize(0)
        {};

        ~BrotligWorker() { m_input = nullptr; m_output = nullptr; }

        virtual bool Setup(const uint8_t* input, size_t input_size, void* params, uint32_t flags, uint32_t extra) = 0;
        virtual bool Run() = 0;
        virtual void Cleanup() = 0;

        uint8_t* GetOutput() { return m_output; };
        size_t GetOutputSize() { return m_outputSize; }
    };

    struct ThreadParam
    {
        BrotligWorker* worker;
        const uint8_t* block_in;
        size_t block_in_size;
        uint8_t** block_out;
        size_t* block_out_size;
        void* params;
        bool flags;
        uint32_t extra;

        volatile bool run;
        volatile bool exit;
    };

    uint32_t                 GetNumberOfProcessorsThreads();
    unsigned int    _stdcall ThreadProc(void* threadParam);

    class BrotligMultithreader
    {
    protected:
        ThreadParam* m_ParameterStorage;

        // User configurable variables
        uint32_t    m_NumThreads;
        uint32_t    m_LiveThreads;
        uint32_t    m_LastThread;

        std::thread* m_ThreadHandle;
        BrotligWorker* m_workers[BROTLIG_MAX_WORKERS];

        virtual BROTLIG_ERROR   InitializeThreads() = 0;
        virtual void* GenerateParamSet(uint32_t* userparams, size_t num_userparams) = 0;

    public:
        BrotligMultithreader()
            : m_ParameterStorage(nullptr)
            , m_NumThreads(0)
            , m_LiveThreads(0)
            , m_LastThread(0)
            , m_ThreadHandle(nullptr)
        {
            for (uint32_t i = 0; i < BROTLIG_MAX_WORKERS; ++i)
                m_workers[i] = nullptr;
        }

        ~BrotligMultithreader()
        {
            for (uint32_t i = 0; i < BROTLIG_MAX_WORKERS; ++i)
                if (m_workers[i])
                    delete m_workers[i];

            if (m_ParameterStorage)
                delete[] m_ParameterStorage;

            if (m_ThreadHandle)
                delete[] m_ThreadHandle;

            m_LiveThreads = 0;
            m_NumThreads = 0;
        }

        BROTLIG_ERROR   Initialize();
        BROTLIG_ERROR   ProcessBlock(const uint8_t* in, size_t in_size, uint8_t** out, size_t* out_size, uint32_t* userparams, size_t num_userparams, uint32_t flags, uint32_t extra);
        BROTLIG_ERROR   FinishBlocks();
    };
}
