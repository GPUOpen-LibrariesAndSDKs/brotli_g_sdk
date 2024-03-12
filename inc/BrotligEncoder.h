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

#pragma once

#include "common/BrotligCommon.h"
#include "common/BrotligConstants.h"
#include "common/BrotligFlags.h"
#include "common/BrotligDataConditioner.h"

namespace BrotliG
{
#ifdef __cplusplus
    extern "C"
    {
#endif // __cplusplus

        uint32_t BROTLIG_API MaxCompressedSize(uint32_t inputSize, bool precondition = false, bool deltaencode = false);

        BROTLIG_ERROR BROTLIG_API CheckParams(uint32_t page_size, BrotligDataconditionParams dcParams);
        BROTLIG_ERROR BROTLIG_API Encode(uint32_t input_size, const uint8_t* src, uint32_t* output_size, uint8_t*& output, uint32_t page_size, BrotligDataconditionParams dcParams, BROTLIG_Feedback_Proc feedbackProc);

#ifdef __cplusplus
    };
#endif // __cplusplus
}