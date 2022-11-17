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

#include "BrotligMultithreading.h"

using namespace BrotliG;

uint32_t BrotliG::GetNumberOfProcessorsThreads()
{
#ifndef _WIN32
    //    return sysconf(_SC_NPROCESSORS_ONLN);
    return std::thread::hardware_concurrency();
#else
    // Figure out how many cores there are on this machine
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return (sysinfo.dwNumberOfProcessors);
#endif
}

unsigned int    _stdcall BrotliG::ThreadProc(void* threadParam)
{
    ThreadParam* tp = (ThreadParam*)threadParam;

    while (tp->exit == FALSE) {
        if (tp->run == TRUE) {
            tp->worker->Setup(tp->block_in, tp->block_in_size, tp->params, tp->flags, tp->extra);
            tp->worker->Run();
            *tp->block_out_size = tp->worker->GetOutputSize();
            *tp->block_out = tp->worker->GetOutput();
            tp->worker->Cleanup();
            tp->run = false;
        }
    }

    return 0;
}

BROTLIG_ERROR   BrotligMultithreader::Initialize() {
    for (uint32_t i = 0; i < BROTLIG_MAX_WORKERS; ++i) {
        m_workers[i] = nullptr;
    }

    m_NumThreads = GetNumberOfProcessorsThreads();

    if (m_NumThreads == 0)
        m_NumThreads = 1;
    else
        if (m_NumThreads > BROTLIG_MAX_WORKERS)
            m_NumThreads = BROTLIG_MAX_WORKERS;

    m_LiveThreads = 0;
    m_LastThread = 0;

    m_ParameterStorage = new ThreadParam[m_NumThreads];
    if (!m_ParameterStorage) {
        return BROTLIG_ERROR_GENERIC;
    }

    m_ThreadHandle = new std::thread[m_NumThreads];
    if (!m_ThreadHandle) {
        delete[] m_ParameterStorage;
        m_ParameterStorage = nullptr;
        return BROTLIG_ERROR_GENERIC;
    }

    if (InitializeThreads() != BROTLIG_OK)
        return BROTLIG_ERROR_GENERIC;

    // Create the encoding threads in the suspended state
    for (uint32_t i = 0; i < m_NumThreads; ++i) {
        // Initialize thread parameters.
        m_ParameterStorage[i].worker = m_workers[i];

        // Inform the thread that at the moment it doesn't have any work to do
        // but that it should wait for some and not exit
        m_ParameterStorage[i].run = FALSE;
        m_ParameterStorage[i].exit = FALSE;

        m_ThreadHandle[i] = std::thread(ThreadProc, (void*)&m_ParameterStorage[i]);
        m_LiveThreads++;
    }

    return BROTLIG_OK;
}

BROTLIG_ERROR   BrotligMultithreader::ProcessBlock(
    const uint8_t* in,
    size_t in_size,
    uint8_t** out,
    size_t* out_size,
    uint32_t* userparams, 
    size_t num_userparams, 
    uint32_t flags,
    uint32_t extra)
{
    uint32_t    threadIndex;

    void* params = GenerateParamSet(userparams, num_userparams);

    bool found = FALSE;
    threadIndex = m_LastThread;
    while (found == FALSE) {
        
        if (m_ParameterStorage == nullptr)
            return BROTLIG_ERROR_GENERIC;

        if (m_ParameterStorage[threadIndex].run == FALSE) {
            found = TRUE;
            break;
        }

        threadIndex++;
        if (threadIndex == m_LiveThreads) {
            threadIndex = 0;
        }
    }

    m_LastThread = threadIndex;

    // Copy the input data into the thread storage
    m_ParameterStorage[threadIndex].block_in = in;
    m_ParameterStorage[threadIndex].block_in_size = in_size;

    // Set the output pointer for the thread to the provided location
    m_ParameterStorage[threadIndex].block_out = out;
    m_ParameterStorage[threadIndex].block_out_size = out_size;

    // Set the parameters and flags
    m_ParameterStorage[threadIndex].params = params;
    m_ParameterStorage[threadIndex].flags = flags;
    m_ParameterStorage[threadIndex].extra = extra;

    // Tell the thread to start working
    m_ParameterStorage[threadIndex].run = TRUE;

    return BROTLIG_OK;
}

BROTLIG_ERROR   BrotligMultithreader::FinishBlocks() {
    // Wait for all the live threads to finish any current work
    for (uint32_t i = 0; i < m_LiveThreads; i++) {

        // If a thread is in the running state then we need to wait for it to finish
        // its work from the producer
        while (m_ParameterStorage[i].run == TRUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(0));
        }

        m_ParameterStorage[i].exit = TRUE;

        if (m_ThreadHandle[i].joinable())
            m_ThreadHandle[i].join();
    }

    return BROTLIG_OK;
}
