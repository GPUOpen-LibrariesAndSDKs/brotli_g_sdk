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

/* Copyright 2013 Google Inc. All Rights Reserved.

   Distributed under MIT license.
   See file LICENSE for detail or copy at https://opensource.org/licenses/MIT
*/

#pragma once

extern "C" {
#include "brotli/c/enc/hash.h"
#include "brotli/c/enc/memory.h"
}

#include "common/BrotligCommand.h"
#include "common/BrotligBitWriter.h"
#include "common/BrotligUtils.h"

#include "BrotligEncoderParams.h"

namespace BrotliG
{
    typedef struct BrotligEncoderState
    {
        MemoryManager* mem_manager;

        BrotligEncoderParams* params;
        std::vector<std::vector<BrotligCommand*>> commands;
        std::vector<size_t> bs_command_sizes;
        size_t num_literals;
        size_t last_insert_len;
        int dist_cache[BROTLI_NUM_DISTANCE_SHORT_CODES];
        int saved_dist_cache[4];
        Hasher hasher;
        uint16_t last_bytes;
        uint8_t last_bytes_bits;
        uint32_t mask;
        bool isLast;
        BrotligBitWriter* bw;

        BrotligEncoderState(BrotligEncoderParams* inparams, size_t input_size, bool islast)
        {
            mem_manager = new MemoryManager;
            BrotliInitMemoryManager(mem_manager, NULL, NULL, NULL); /*To do: try to eliminate use of this as much as possible.*/

            BrotligEncoderParams* temp = new BrotligEncoderParams;
            (*temp) = (*inparams);
            params = temp;

            num_literals = 0;
            last_insert_len = 0;
            mask = 262143; /*To do: is this required anymore.*/
            isLast = islast;
            HasherInit(&hasher);
            dist_cache[0] = 4;
            dist_cache[1] = 11;
            dist_cache[2] = 15;
            dist_cache[3] = 16;
            memcpy(saved_dist_cache, dist_cache, sizeof(saved_dist_cache));
            last_bytes = 0;
            last_bytes_bits = 0;

            params->UpdateParams(input_size);

            EncodeWindowBits(params->lgwin, params->large_window,
                &last_bytes, &last_bytes_bits);

            bw = new BrotligBitWriter;
        }

        ~BrotligEncoderState()
        {
            for (int index = 0; index < commands.size(); index++)
            {
                for (int j = 0; j < commands.at(index).size(); j++)
                {
                    if (commands.at(index).at(j) != nullptr)
                    {
                        BrotligCommand* cmd = commands.at(index).at(j);
                        commands.at(index).at(j) = nullptr;
                        delete cmd;
                    }
                }

                commands.at(index).clear();
            }

            commands.clear();
            bs_command_sizes.clear();

            DestroyHasher(mem_manager, &hasher);

            delete params;
            BrotliWipeOutMemoryManager(mem_manager);

            delete bw;
            bw = nullptr;
        }

    } BrotligEncoderState;
}
