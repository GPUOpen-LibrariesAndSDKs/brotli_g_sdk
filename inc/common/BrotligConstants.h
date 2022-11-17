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

extern "C" {
#include "brotli/c/common/constants.h"
}

#include "common/BrotligFlags.h"

namespace BrotliG
{
    // BroltiG symbol limits
#define BROTLIG_NUM_COMMAND_SYMBOLS_WITH_SENTINEL BROTLI_NUM_COMMAND_SYMBOLS + 1
#define BROTLIG_NUM_END_LITERAL_SYMBOLS 23
#define BROTLIG_NUM_COMMAND_SYMBOLS_WITH_END_LITERALS BROTLIG_NUM_COMMAND_SYMBOLS_WITH_SENTINEL + BROTLIG_NUM_END_LITERAL_SYMBOLS
#if USE_INSERT_ONLY_COMMANDS
#define BROLTIG_NUM_COMMAND_SYMBOLS_EFFECTIVE BROTLIG_NUM_COMMAND_SYMBOLS_WITH_END_LITERALS
#else
#define BROLTIG_NUM_COMMAND_SYMBOLS_EFFECTIVE BROTLIG_NUM_COMMAND_SYMBOLS_WITH_SENTINEL
#endif // DEBUG

//#define BROTLIG_NUM_DISTANCE_SYMBOLS BROTLI_NUM_DISTANCE_SYMBOLS
#define BROTLIG_NUM_DISTANCE_SYMBOLS 544
//#define BROTLIG_NUM_DISTANCE_SYMBOLS 1128

// BrotliG stream settings
#define BROTLIG_STREAM_ID 5
#define BROLTIG_NUM_BITSTREAMS 32
#define BROTLIG_COMMAND_GROUP_SIZE 1
#define BROTLIG_SWIZZLE_SIZE 4

// BrotliG Minimum page size
#define BROTLIG_MIN_PAGE_SIZE 32 * 1024
// BrotliG Default page size
#define BROTLIG_DEFAULT_PAGE_SIZE 64 * 1024
// BrotliG Maximum read size
#define BROTLIG_MIN_MAX_READ_SIZE 1 * 1024 * 1024
// BrotliG Data alignment
#define BROTLIG_DATA_ALIGNMENT 4
// BrotliG Default substream size
#define BROTLIG_DEFAULT_MAX_READ_SIZE 4 * 1024 * 1024
// BrotliG Max num pages
#define BROTLIG_MAX_NUM_PAGES (1 << 16) - 1

// BrotliG Multi-threaded settings
#define BROTLIG_MAX_WORKERS 128

// BrotliG LZ77 settings
#define BROTLIG_LZ77_PAD_INPUT 1
#define BROTLIG_LZ77_PADDING_SIZE 8

// BrotliG Huffman settings
#define BROTLIG_MAX_HUFFMAN_DEPTH 15
#define BROTLIG_MAX_HUFFMAN_BITS 16

// BrotliG CPU Decoder Settings
#define BROTLIG_DWORD_SIZE_BYTES 4
#define BROTLIG_MEMCOPY_BYTES_LIMIT 16

// BrotliG GPU Decoder Settings
#define BROTLIG_GPUD_MIN_D3D_FEATURE_LEVEL 0xc000
#define BROTLIG_GPUD_MIN_D3D_SHADER_MODEL 0x60
#define BROTLIG_GPUD_MAX_D3D_SHADER_MODEL 0x65

#define BROTLIG_GPUD_DEFAULT_NUM_GROUPS 2560
#define BROTLIG_GPUD_DEFAULT_MAX_TEMP_BUFFER_SIZE 1 * 1024 * 1024 * 1024
#define BROTLIG_GPUD_DEFAULT_MAX_STREAMS_PER_LAUNCH 4096
#define BROTLIG_GPUD_DEFAULT_MAX_QUERIES 32
}


