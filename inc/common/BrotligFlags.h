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

// command generation flags
#define LZ77_PAD_INPUT  0
#define USE_INSERT_ONLY_COMMANDS 1

// command distribution flags
#define ADD_SENTINEL_PER_BITSTREAM 0

// stream serialization flags
#define USE_COMPACT_SERIALIZTION 1
#define REDISTRIBUTE_LITERALS 1

// display flags: default off
#define SHOW_PROGRESS 0

// multithread flags
#define BROTLIG_ENCODER_MULTITHREADED 1
#define BROTLIG_ENCODER_MULITHREADED_VERSION 0
#define BROTLIG_CPU_DECODER_MULTITHREADED 1
#define BROTLIG_CPU_DECODER_MULITHREADED_VERSION 0
}