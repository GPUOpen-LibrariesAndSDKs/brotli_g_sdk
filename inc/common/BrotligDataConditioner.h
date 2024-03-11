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

#include "BrotligConstants.h"
#include "BrotligCommon.h"
#include "BrotligUtils.h"

namespace BrotliG
{
    typedef struct BrotligDataconditionParams
    {
        bool precondition = false;
        bool swizzle = false;
        bool delta_encode = false;

        BROTLIG_DATA_FORMAT format = BROTLIG_DATA_FORMAT_UNKNOWN;
        uint32_t widthInPixels = 0;
        uint32_t heightInPixels = 0;
        uint32_t numMipLevels = 1;
        uint32_t rowPitchInBytes = 0;
        bool pitchd3d12aligned = false;

        uint32_t blockSizeBytes = BROTLIG_DEFAULT_BLOCK_SIZE_BYTES;
        uint32_t blockSizePixels = BROTLIG_DEFAULT_BLOCK_SIZE_PIXELS;
        uint32_t colorSizeBits = BROTLIG_DEFAULT_COLOR_SIZE_BITS;
        uint32_t numSubBlocks = 0;

        uint32_t subBlockSizes[BROTLIG_MAX_NUM_SUB_BLOCKS] = { 0 };
        uint32_t subBlockOffsets[BROTLIG_MAX_NUM_SUB_BLOCKS] = { 0 };

        uint32_t colorSubBlocks[BROTLIG_MAX_NUM_SUB_BLOCKS] = { 0 };
        uint32_t numColorSubBlocks = 0;

        uint32_t widthInBlocks[BROTLIG_PRECON_MAX_NUM_MIP_LEVELS] = { 0 };
        uint32_t heightInBlocks[BROTLIG_PRECON_MAX_NUM_MIP_LEVELS] = { 0 };
        uint32_t pitchInBytes[BROTLIG_PRECON_MAX_NUM_MIP_LEVELS] = { 0 };
        uint32_t numBlocks[BROTLIG_PRECON_MAX_NUM_MIP_LEVELS] = { 0 };
        uint32_t subStreamOffsets[BROTLIG_MAX_NUM_SUB_BLOCKS + 1] = { 0 };
        uint32_t mipOffsetsBytes[BROTLIG_PRECON_MAX_NUM_MIP_LEVELS + 1] = { 0 };
        uint32_t mipOffsetBlocks[BROTLIG_PRECON_MAX_NUM_MIP_LEVELS + 1] = { 0 };
        uint32_t tNumBlocks = 0;

        bool isInitialized = false;

        BROTLIG_ERROR CheckParams()
        {
            if (widthInPixels < BROTLIG_PRECON_MIN_TEX_WIDTH_PIXEL)
                return BROTLIG_ERROR_PRECON_MIN_TEX_WIDTH;

            if (widthInPixels > BROTLIG_PRECON_MAX_TEX_WIDTH_PIXEL)
                return BROTLIG_ERROR_PRECON_MAX_TEX_WIDTH;

            if (heightInPixels < BROTLIG_PRECON_MIN_TEX_HEIGHT_PIXEL)
                return BROTLIG_ERROR_PRECON_MIN_TEX_HEIGHT;

            if (heightInPixels > BROTLIG_PRECON_MAX_TEX_HEIGHT_PIXEL)
                return BROTLIG_ERROR_PRECON_MAX_TEX_HEIGHT;

            if (rowPitchInBytes < BROTLIG_PRECON_MIN_TEX_PITCH_BYTES)
                return BROTLIG_ERROR_PRECON_MIN_TEX_PITCH;

            if (rowPitchInBytes > BROTLIG_PRECON_MAX_TEX_PITCH_BYTES)
                return BROTLIG_ERROR_PRECON_MAX_TEX_PITCH;

            if (numMipLevels < BROTLIG_PRECON_MIN_NUM_MIP_LEVELS)
                return BROTLIG_ERROR_PRECON_MIN_TEX_MIPLEVELS;

            if (numMipLevels > BROTLIG_PRECON_MAX_NUM_MIP_LEVELS)
                return BROTLIG_ERROR_PRECON_MAX_TEX_MIPLEVELS;

            return BROTLIG_OK;
        }

        bool Initialize(uint32_t inSize)
        {
            if (isInitialized) return true;

            switch (format)
            {
            case BROTLIG_DATA_FORMAT_BC1:
                blockSizePixels = BROTLIG_BC1_BLOCK_SIZE_PIXELS;
                blockSizeBytes = BROTLIG_BC1_BLOCK_SIZE_BYTES;
                colorSizeBits = BROTLIG_BC1_COLOR_SIZE_BITS;

                numSubBlocks = BROTLIG_BC1_NUM_SUB_BLOCKS;
                subBlockSizes[0] = BROTLIG_BC1_COLOR_TOP_REF_SIZE_BYTES;
                subBlockSizes[1] = BROTLIG_BC1_COLOR_BOT_REF_SIZE_BYTES;
                subBlockSizes[2] = BROTLIG_BC1_COLOR_INDEX_SIZE_BYTES;

                colorSubBlocks[numColorSubBlocks++] = 0;
                colorSubBlocks[numColorSubBlocks++] = 1;
                break;

            case BROTLIG_DATA_FORMAT_BC2:
                blockSizePixels = BROTLIG_BC2_BLOCK_SIZE_PIXELS;
                blockSizeBytes = BROTLIG_BC2_BLOCK_SIZE_BYTES;
                colorSizeBits = BROTLIG_BC2_COLOR_SIZE_BITS;

                numSubBlocks = BROTLIG_BC2_NUM_SUB_BLOCKS;
                subBlockSizes[0] = BROTLIG_BC2_ALPHA_VECTOR_SIZE_BYTES;
                subBlockSizes[1] = BROTLIG_BC2_COLOR_TOP_REF_SIZE_BYTES;
                subBlockSizes[2] = BROTLIG_BC2_COLOR_BOT_REF_SIZE_BYTES;
                subBlockSizes[3] = BROTLIG_BC2_COLOR_INDEX_SIZE_BYTES;

                colorSubBlocks[numColorSubBlocks++] = 1;
                colorSubBlocks[numColorSubBlocks++] = 2;
                break;

            case BROTLIG_DATA_FORMAT_BC3:
                blockSizePixels = BROTLIG_BC3_BLOCK_SIZE_PIXELS;
                blockSizeBytes = BROTLIG_BC3_BLOCK_SIZE_BYTES;
                colorSizeBits = BROTLIG_BC3_COLOR_SIZE_BITS;

                numSubBlocks = BROTLIG_BC3_NUM_SUB_BLOCKS;
                subBlockSizes[0] = BROTLIG_BC3_ALPHA_TOP_REF_SIZE_BYTES;
                subBlockSizes[1] = BROTLIG_BC3_ALPHA_BOT_REF_SIZE_BYTES;
                subBlockSizes[2] = BROTLIG_BC3_ALPHA_INDEX_SIZE_BYTES;
                subBlockSizes[3] = BROTLIG_BC3_COLOR_TOP_REF_SIZE_BYTES;
                subBlockSizes[4] = BROTLIG_BC3_COLOR_BOT_REF_SIZE_BYTES;
                subBlockSizes[5] = BROTLIG_BC3_COLOR_INDEX_SIZE_BYTES;

                colorSubBlocks[numColorSubBlocks++] = 3;
                colorSubBlocks[numColorSubBlocks++] = 4;
                break;

            case BROTLIG_DATA_FORMAT_BC4:
                blockSizePixels = BROTLIG_BC4_BLOCK_SIZE_PIXELS;
                blockSizeBytes = BROTLIG_BC4_BLOCK_SIZE_BYTES;
                colorSizeBits = BROTLIG_BC4_COLOR_SIZE_BITS;

                numSubBlocks = BROTLIG_BC4_NUM_SUB_BLOCKS;
                subBlockSizes[0] = BROTLIG_BC4_COLOR_TOP_REF_SIZE_BYTES;
                subBlockSizes[1] = BROTLIG_BC4_COLOR_BOT_REF_SIZE_BYTES;
                subBlockSizes[2] = BROTLIG_BC4_COLOR_INDEX_SIZE_BYTES;

                colorSubBlocks[numColorSubBlocks++] = 0;
                colorSubBlocks[numColorSubBlocks++] = 1;
                break;

            case BROTLIG_DATA_FORMAT_BC5:
                blockSizePixels = BROTLIG_BC5_BLOCK_SIZE_PIXELS;
                blockSizeBytes = BROTLIG_BC5_BLOCK_SIZE_BYTES;
                colorSizeBits = BROTLIG_BC5_COLOR_SIZE_BITS;

                numSubBlocks = BROTLIG_BC5_NUM_SUB_BLOCKS;
                subBlockSizes[0] = BROTLIG_BC5_RED_TOP_REF_SIZE_BYTES;
                subBlockSizes[1] = BROTLIG_BC5_RED_BOT_REF_SIZE_BYTES;
                subBlockSizes[2] = BROTLIG_BC5_RED_INDEX_SIZE_BYTES;
                subBlockSizes[3] = BROTLIG_BC5_GREEN_TOP_REF_SIZE_BYTES;
                subBlockSizes[4] = BROTLIG_BC5_GREEN_BOT_REF_SIZE_BYTES;
                subBlockSizes[5] = BROTLIG_BC5_GREEN_INDEX_SIZE_BYTES;

                colorSubBlocks[numColorSubBlocks++] = 0;
                colorSubBlocks[numColorSubBlocks++] = 1;
                colorSubBlocks[numColorSubBlocks++] = 3;
                colorSubBlocks[numColorSubBlocks++] = 4;
                break;
            default:
                blockSizePixels = BROTLIG_DEFAULT_BLOCK_SIZE_PIXELS;
                blockSizeBytes = BROTLIG_DEFAULT_BLOCK_SIZE_BYTES;

                numSubBlocks = 1;
                subBlockSizes[0] = BROTLIG_DEFAULT_BLOCK_SIZE_BYTES;
                break;
            }

            if (numMipLevels == 0) numMipLevels = 1;

            if (widthInBlocks[0] == 0) widthInBlocks[0] = (widthInPixels + blockSizePixels - 1) / blockSizePixels;
            if (heightInBlocks[0] == 0) heightInBlocks[0] = (heightInPixels + blockSizePixels - 1) / blockSizePixels;
            
            if (widthInPixels == 0) widthInPixels = widthInBlocks[0] * blockSizePixels;
            if (heightInPixels == 0) heightInPixels = heightInBlocks[0] * blockSizePixels;
            
            tNumBlocks = numBlocks[0] = widthInBlocks[0] * heightInBlocks[0];

            if (pitchInBytes[0] == 0)
            {
                if (rowPitchInBytes != 0) pitchInBytes[0] = rowPitchInBytes;
                else pitchInBytes[0] = (pitchd3d12aligned) ? (RoundUp(widthInBlocks[0] * blockSizeBytes, BROTLIG_D3D12_TEXTURE_DATA_PITCH_ALIGNMENT_BTYES)) : (widthInBlocks[0] * blockSizeBytes);
            }

            uint32_t mipwidthpx = (widthInBlocks[0] * blockSizePixels) / 2, mipheightpx = (heightInBlocks[0] * blockSizePixels) / 2, mip = 1;
            for (; mip <= numMipLevels; ++mip)
            {
                if (mip < numMipLevels)
                {
                    widthInBlocks[mip] = (mipwidthpx + blockSizePixels - 1) / blockSizePixels;
                    heightInBlocks[mip] = (mipheightpx + blockSizePixels - 1) / blockSizePixels;
                    numBlocks[mip] = widthInBlocks[mip] * heightInBlocks[mip];
                    pitchInBytes[mip] = (pitchd3d12aligned) ? (RoundUp(widthInBlocks[mip] * blockSizeBytes, BROTLIG_D3D12_TEXTURE_DATA_PITCH_ALIGNMENT_BTYES)) : (widthInBlocks[mip] * blockSizeBytes);
                    tNumBlocks += numBlocks[mip];
                }
                mipOffsetsBytes[mip] = mipOffsetsBytes[mip - 1] + (pitchInBytes[mip - 1] * heightInBlocks[mip - 1]);
                mipOffsetBlocks[mip] = mipOffsetBlocks[mip - 1] + numBlocks[mip - 1];

                mipwidthpx /= 2;
                mipheightpx /= 2;
            }

            if (mipOffsetsBytes[numMipLevels] != inSize) return false;

            uint32_t sub = 1;
            for (; sub <= numSubBlocks; ++sub)
            {
                if (sub < numSubBlocks) subBlockOffsets[sub] = (sub == 0) ? 0 : subBlockOffsets[sub - 1] + subBlockSizes[sub - 1];
                subStreamOffsets[sub] = subStreamOffsets[sub - 1];
                for (uint32_t mip = 0; mip < numMipLevels; ++mip)
                {
                    subStreamOffsets[sub] += (numBlocks[mip] * subBlockSizes[sub - 1]);
                }
            }

            if (subStreamOffsets[numSubBlocks] != tNumBlocks * blockSizeBytes) return false;

            isInitialized = true;

            return true;
        }

        BrotligDataconditionParams()
        {}

        ~BrotligDataconditionParams()
        {}
    } BrotligDataconditionParams;

    void Condition(uint32_t inSize, const uint8_t* inData, BrotligDataconditionParams& params, uint32_t& outSize, uint8_t*& outData);
}
