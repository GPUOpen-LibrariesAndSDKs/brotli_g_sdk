// Brotli-G SDK 1.2
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
#include "common/BrotligUtils.h"

namespace BrotliG
{
    struct StreamHeader
    {
        uint8_t  Id;
        uint8_t  Magic;
        uint16_t NumPages;
        uint32_t PageSizeIdx            : BROTLIG_STREAM_PAGE_SIZE_IDX_BITS;
        uint32_t LastPageSize           : BROTLIG_STREAM_LASTPAGE_SIZE_BITS;
        uint32_t Preconditioned         : BROTLIG_STREAM_PRECONDITION_BITS;
        uint32_t Reserved               : BROTLIG_STREAM_RESERVED_BITS;

        inline void SetId(uint8_t id)
        {
            Id = id;
            Magic = id ^ 0xff;
        }

        inline bool Validate() const
        {
            return Id == (Magic ^ 0xff);
        }

        inline void SetUncompressedSize(size_t size)
        {
            size_t page_size = PageSize();
            size_t num_pages = size / page_size;
            NumPages = static_cast<uint16_t>(num_pages);
            LastPageSize =
                static_cast<uint32_t>(size - NumPages * PageSize());

            NumPages += LastPageSize != 0 ? 1 : 0;
        }

        inline size_t UncompressedSize() const
        {
            return NumPages * PageSize() -
                (LastPageSize == 0 ? 0 : PageSize() - LastPageSize);
        }

        inline void SetPageSize(size_t size)
        {
            PageSizeIdx = CountBits(size / (BROTLIG_MIN_PAGE_SIZE));

            assert(PageSize() == size && "Incorrect page size!");
        }

        inline size_t PageSize() const
        {
            return static_cast<size_t>(BROTLIG_MIN_PAGE_SIZE) << PageSizeIdx;
        }

        inline void SetPreconditioned(bool flag)
        {
            Preconditioned = static_cast<uint32_t>(flag);
        }

        inline bool IsPreconditioned() const
        {
            return (Preconditioned == 1);
        }
    };

    struct PreconditionHeader
    {        
        uint32_t Swizzled               : BROTLIG_PRECON_SWIZZLING_BITS;
        uint32_t PitchD3D12Aligned      : BROTLIG_PRECON_PITCH_D3D12_ALIGNED_FLAG_BITS;
        uint32_t WidthInBlocks          : BROTLIG_PRECON_TEX_WIDTH_BLOCK_BITS;
        uint32_t HeightInBlocks         : BROTLIG_PRECON_TEX_HEIGHT_BLOCK_BITS;
        uint32_t Format                 : BROTLIG_PRECON_DATA_FORMAT;
        uint32_t NumMips                : BROTLIG_PRECON_TEX_NUMMIPLEVELS_BITS;
        uint32_t PitchInBytes           : BROTLIG_PRECON_TEX_PITCH_BYTES_BITS;

        inline void SetDataFormat(BROTLIG_DATA_FORMAT format)
        {
            Format = static_cast<uint32_t>(format);
        }

        inline BROTLIG_DATA_FORMAT DataFormat() const
        {
            return static_cast<BROTLIG_DATA_FORMAT>(Format);
        }
    };
}

