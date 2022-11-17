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

#include "common/BrotligCommon.h"
#include "common/BrotligConstants.h"
#include "common/BrotligUtils.h"

namespace BrotliG
{
    typedef uint8_t Byte;

    typedef struct BrotligByteBuffer
    {
        Byte* data;
        size_t size;

        BrotligByteBuffer()
        {
            data = nullptr;
            size = 0;
        }

        BrotligByteBuffer(const size_t bufSize)
        {
            size = bufSize;
            data = new Byte[bufSize];
        }

        BrotligByteBuffer(const BrotligByteBuffer& b)
        {
            data = new Byte[b.size];
            memcpy(data, b.data, b.size);
            size = b.size;
        }

        void operator=(const BrotligByteBuffer& b)
        {
            if (data != nullptr)
                delete[] data;
            data = new Byte[b.size];
            memcpy(data, b.data, b.size);
            size = b.size;
        }

        ~BrotligByteBuffer()
        {
            if (data != nullptr)
                delete[] data;
            data = nullptr;
            size = 0;
        }
    } BrotligByteBuffer;

    struct StreamHeader
    {
        uint8_t Id;
        uint8_t Magic;
        uint16_t NumPages;
        uint32_t PageSizeIdx : 2;
        uint32_t LastPageSize : 18;
        uint32_t Reserved : 12;

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
            PageSizeIdx = CountBits(size / BROTLIG_MIN_PAGE_SIZE);

            assert(PageSize() == size && "Incorrect page size!");
        }

        inline size_t PageSize() const
        {
            return static_cast<size_t>(BROTLIG_MIN_PAGE_SIZE) << PageSizeIdx;
        }
    };
}

