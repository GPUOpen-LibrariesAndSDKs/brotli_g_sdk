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

namespace BrotliG
{
// Header flags
#define PAD_HEADER 0
#define LGWIN_FIELD 0
#define ISLAST_FLAG 0
#define ISEMPTY_FLAG 0
#define ISUNCOMPRESSED_FLAG 0
#define DIST_POSTFIX_BITS_FIELD 1
#define NUM_DIRECT_DIST_CODES_FIELD 1
#define RESERVE_BITS 1
#define UNCOMPLEN_FIELD 0
#define PREAMBLE 0
#define BS_SIZES_STORE 1

// Command generation flags
#define USE_INSERT_ONLY_COMMANDS 1                                  // 0 - off, 1 - on  - to remove

// Stream serialization flags
#define USE_COMPACT_SERIALIZTION 1                                  // 0 - off, 1 - on  - to remove
#define REDISTRIBUTE_LITERALS 1                                     // 0 - off, 1 - on  - to remove

// Display flags: default off
#define SHOW_PROGRESS 0                                             // 0 - off, 1 - on

// Multithread flags
#define BROTLIG_ENCODER_MULTITHREADING_MODE 1                       // 0 - single threaded, 1 - multi-threader
#define BROTLIG_CPU_DECODER_MULTITHREADING_MODE 1                   // 0 - single threaded, 1 - multi-threader
}