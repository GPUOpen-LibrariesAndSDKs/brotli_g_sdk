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
#include "brotli/c/enc/command.h"
}

#include "common/BrotligBitWriter.h"
#include "common/BrotligCommon.h"

namespace BrotliG
{
    typedef struct BrotligCommand
    {
        uint32_t insert_pos;
        uint32_t insert_len;
        uint32_t copy_len;
        uint32_t dist_extra;
        uint16_t cmd_prefix;
        uint16_t dist_prefix;
        size_t dist;
        int32_t dist_code;

        BrotligCommand()
        {
            insert_pos = 0;
            insert_len = 0;
            copy_len = 0;
            dist_extra = 0;
            cmd_prefix = 0;
            dist_prefix = 0;
            dist = 0;
            dist_code = 0;
        }

        Command* ToBroliCommand()
        {
            Command* out = new Command;
            out->insert_len_ = insert_len;
            out->copy_len_ = copy_len;
            out->cmd_prefix_ = cmd_prefix;
            out->dist_prefix_ = dist_prefix;
            out->dist_extra_ = dist_extra;

            return out;
        }

        void Copy(Command* in)
        {
            insert_len = in->insert_len_;
            copy_len = in->copy_len_;
            cmd_prefix = in->cmd_prefix_;
            dist_prefix = in->dist_prefix_;
            dist_extra = in->dist_extra_;
            dist = 0;
            dist_code = 0;
        }

        uint32_t CopyLen()
        {
            return copy_len & 0x1FFFFFF;
        }

        uint32_t DistanceContext()
        {
            uint32_t r = cmd_prefix >> 6;
            uint32_t c = cmd_prefix & 7;
            if ((r == 0 || r == 2 || r == 4 || r == 7) && (c <= 2)) {
                return c;
            }
            return 3;
        }

        uint16_t Distance()
        {
            return dist_prefix & 0x3FF;
        }

        uint32_t CopyLenCode()
        {
            uint32_t modifier = copy_len >> 25;
            int32_t delta = (int8_t)((uint8_t)(modifier | ((modifier & 0x40) << 1)));
            return (uint32_t)((int32_t)(copy_len & 0x1FFFFFF) + delta);
        }

        uint16_t InsertLengthCode()
        {
            if (insert_len < 6) {
                return (uint16_t)insert_len;
            }
            else if (insert_len < 130) {
                uint32_t nbits = Log2FloorNonZero(insert_len - 2) - 1u;
                return (uint16_t)((nbits << 1) + ((insert_len - 2) >> nbits) + 2);
            }
            else if (insert_len < 2114) {
                return (uint16_t)(Log2FloorNonZero(insert_len - 66) + 10);
            }
            else if (insert_len < 6210) {
                return 21u;
            }
            else if (insert_len < 22594) {
                return 22u;
            }
            else {
                return 23u;
            }
        }

        uint16_t GetCopyLengthCode(size_t copylen) {
            if (copylen == 0) {
                return (uint16_t)(copylen);
            }
            else if (copylen < 10) {
                return (uint16_t)(copylen - 2);
            }
            else if (copylen < 134) {
                uint32_t nbits = Log2FloorNonZero(copylen - 6) - 1u;
                return (uint16_t)((nbits << 1u) + ((copylen - 6) >> nbits) + 4);
            }
            else if (copylen < 2118) {
                return (uint16_t)(Log2FloorNonZero(copylen - 70) + 12);
            }
            else {
                return 23u;
            }
        }

        void GetExtra(uint32_t& n_bits, uint64_t& bits)
        {
            if (cmd_prefix <= BROTLI_NUM_COMMAND_SYMBOLS)
            {
                uint32_t copylen_code = CopyLenCode();
                uint16_t inscode = InsertLengthCode();
                uint16_t copycode = GetCopyLengthCode(copylen_code);
                uint32_t insnumextra = GetInsertExtra(inscode);
                uint64_t insextraval = insert_len - GetInsertBase(inscode);
                uint64_t copyextraval = (copycode > 1) ? copylen_code - GetCopyBase(copycode) : copylen_code;
                bits = (copyextraval << insnumextra) | insextraval;
                n_bits = insnumextra + GetCopyExtra(copycode);
            }
            else
            {
                uint32_t inscode = InsertLengthCode();
                uint32_t insnumextra = GetInsertExtra(inscode);
                uint64_t insextraval = insert_len - GetInsertBase(inscode);
                bits = insextraval;
                n_bits = insnumextra;
            }
        }

        void StoreExtra(BrotligBitWriter* bw)
        {
            if (cmd_prefix <= BROTLI_NUM_COMMAND_SYMBOLS)
            {
                uint32_t copylen_code = CopyLenCode();
                uint16_t inscode = InsertLengthCode();
                uint16_t copycode = GetCopyLengthCode(copylen_code);
                uint32_t insnumextra = GetInsertExtra(inscode);
                uint64_t insextraval = insert_len - GetInsertBase(inscode);
                uint64_t copyextraval = (copycode > 1) ? copylen_code - GetCopyBase(copycode) : copylen_code;
                uint64_t bits = (copyextraval << insnumextra) | insextraval;
                bw->Write(insnumextra + GetCopyExtra(copycode), bits);
            }
            else
            {
                uint16_t inscode = InsertLengthCode();
                uint32_t insnumextra = GetInsertExtra(inscode);
                uint64_t insextraval = insert_len - GetInsertBase(inscode);
                uint64_t bits = insextraval;
                bw->Write(insnumextra, bits);
            }
        }

        uint16_t CombineLengthCodes(
            uint16_t inscode, uint16_t copycode, bool use_last_distance) {
            uint16_t bits64 =
                (uint16_t)((copycode & 0x7u) | ((inscode & 0x7u) << 3u));
            if (use_last_distance && inscode < 8u && copycode < 16u) {
                uint16_t combinedcode = (copycode < 8u) ? bits64 : (bits64 | 64u);
                assert(combinedcode < BROTLI_NUM_COMMAND_SYMBOLS);
                return combinedcode;
            }
            else {
                /* Specification: 5 Encoding of ... (last table) */
                /* offset = 2 * index, where index is in range [0..8] */
                uint32_t offset = 2u * ((copycode >> 3u) + 3u * (inscode >> 3u));
                /* All values in specification are K * 64,
                   where   K = [2, 3, 6, 4, 5, 8, 7, 9, 10],
                       i + 1 = [1, 2, 3, 4, 5, 6, 7, 8,  9],
                   K - i - 1 = [1, 1, 3, 0, 0, 2, 0, 1,  2] = D.
                   All values in D require only 2 bits to encode.
                   Magic constant is shifted 6 bits left, to avoid final multiplication. */
                offset = (offset << 5u) + 0x40u + ((0x520D40u >> offset) & 0xC0u);
                uint16_t combinedcode = (uint16_t)(offset | bits64);
                assert(combinedcode < BROTLI_NUM_COMMAND_SYMBOLS);
                return combinedcode;
            }
        }

    }BrotligCommand;
}
