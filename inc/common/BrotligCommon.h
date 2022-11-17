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

#define NOMINMAX

#include <algorithm>
#include <assert.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <variant>
#include <queue>
#include <set>
#include <Windows.h>

#include <d3d12.h>

#include "BrotligFlags.h"


// Compress error codes
typedef enum {
    BROTLIG_OK = 0,
    BROTLIG_ABORTED,
    BROTLIG_ERROR_MAX_NUM_PAGES, // Number of pages numpages exceeds maximum number of pages allowed BROTLIG_MAX_NUM_PAGES
    BROTLIG_ERROR_CORRUPT_STREAM, // Corrupt stream
    BROTLIG_ERROR_INCORRECT_STREAM_FORMAT,// Incorrect stream format
    BROTLIG_ERROR_GENERIC,
} BROTLIG_ERROR;

#if defined(WIN32) || defined(_WIN64)
#define BROTLIG_API __cdecl
#else
#define BROTLIG_API
#endif

// BROTLIG_Feedback_Proc for user to handle status on CPU encoder and CPU decoder processing cycles
typedef bool(BROTLIG_API* BROTLIG_Feedback_Proc)(float fProgress);