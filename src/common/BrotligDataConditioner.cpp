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

#include "common/BrotligCommon.h"
#include "common/BrotligConstants.h"
#include "common/BrotligUtils.h"
#include "common/BrotligBitReader.h"
#include "common/BrotligBitWriter.h"

#include "BrotligDataConditioner.h"

bool Swizzle(uint32_t size, uint8_t* data, uint32_t blockSize, uint32_t widthInBlocks, uint32_t heightInBlocks, uint32_t pitch)
{
    if (widthInBlocks < BROTLIG_PRECON_SWIZZLE_REGION_SIZE || heightInBlocks < BROTLIG_PRECON_SWIZZLE_REGION_SIZE)
        return false;

    uint8_t* temp = new uint8_t[size];
    memcpy(temp, data, size);

    uint32_t effWidthInBlocks = widthInBlocks - (widthInBlocks % BROTLIG_PRECON_SWIZZLE_REGION_SIZE);
    uint32_t effHeightInBlocks = heightInBlocks - (heightInBlocks % BROTLIG_PRECON_SWIZZLE_REGION_SIZE);

    uint32_t inIndex = 0, outRow = 0, outCol = 0, outIndex = 0;
    for (uint32_t row = 0; row < effHeightInBlocks; row += BROTLIG_PRECON_SWIZZLE_REGION_SIZE)
    {
        for (uint32_t col = 0; col < effWidthInBlocks; col += BROTLIG_PRECON_SWIZZLE_REGION_SIZE)
        {
            for (uint32_t rowoffset = 0; rowoffset < BROTLIG_PRECON_SWIZZLE_REGION_SIZE; ++rowoffset)
            {
                for (uint32_t coloffset = 0; coloffset < BROTLIG_PRECON_SWIZZLE_REGION_SIZE; ++coloffset)
                {
                    inIndex = ((row + rowoffset) * pitch) + ((col + coloffset) * blockSize);
                    assert(inIndex < size);
                    outIndex = (outRow * pitch) + (outCol * blockSize);
                    assert(outIndex < size);

                    memcpy(&data[outIndex], &temp[inIndex], blockSize);
                    ++outCol;

                    if (outCol == effWidthInBlocks) 
                    { 
                        outCol = 0; ++outRow;
                    }
                }
            }
        }
    }

    delete[] temp;

    return true;
}

void ConditionBC1_5(uint32_t inSize, const uint8_t* inData, BrotliG::BrotligDataconditionParams& params, uint32_t& outSize, uint8_t*& outData)
{
    uint8_t* temp = new uint8_t[inSize];
    memcpy(temp, inData, inSize);

    outSize = inSize;
    outData = new uint8_t[outSize];
    memset(outData, 0, outSize);

    if (params.swizzle)
    {
        for (uint32_t mip = 0; mip < params.numMipLevels; ++mip)
        {
            Swizzle(
                params.pitchInBytes[mip] * params.heightInBlocks[mip],
                temp + params.mipOffsetsBytes[mip],
                params.blockSizeBytes,
                params.widthInBlocks[mip],
                params.heightInBlocks[mip],
                params.pitchInBytes[mip]
            );
        }
    }

    uint32_t subStreamCopyPtrs[BROTLIG_MAX_NUM_SUB_BLOCKS];
    memcpy(&subStreamCopyPtrs, &params.subStreamOffsets, BROTLIG_MAX_NUM_SUB_BLOCKS * sizeof(uint32_t));
    for (uint32_t mip = 0; mip < params.numMipLevels; ++mip)
    {
        uint32_t rowpaddingInBytes = params.pitchInBytes[mip] - (params.widthInBlocks[mip] * params.blockSizeBytes);
        uint32_t mipOffset = params.mipOffsetsBytes[mip], rowOffset = 0, inIndex = 0;

        for (uint32_t row = 0; row < params.heightInBlocks[mip]; ++row)
        {
            rowOffset = params.pitchInBytes[mip] * row;
            for (uint32_t col = 0; col < params.widthInBlocks[mip]; ++col)
            {
                inIndex = mipOffset + rowOffset + (col * params.blockSizeBytes);

                for (uint32_t sub = 0; sub < params.numSubBlocks; ++sub)
                {
                    memcpy(&outData[subStreamCopyPtrs[sub]], &temp[inIndex], params.subBlockSizes[sub]);
                    inIndex += params.subBlockSizes[sub];
                    subStreamCopyPtrs[sub] += params.subBlockSizes[sub];
                }
            }
        }
    }

    delete[] temp;
}

void BrotliG::Condition(uint32_t inSize, const uint8_t* inData, BrotligDataconditionParams& params, uint32_t& outSize, uint8_t*& outData)
{
    switch (params.format)
    {
    case BROTLIG_DATA_FORMAT_BC1:
    case BROTLIG_DATA_FORMAT_BC2:
    case BROTLIG_DATA_FORMAT_BC3:
    case BROTLIG_DATA_FORMAT_BC4:
    case BROTLIG_DATA_FORMAT_BC5: return ConditionBC1_5(inSize, inData, params, outSize, outData);
    default:
        throw std::exception("Brotli-G preconditioning unrecognized format");
    }
}
