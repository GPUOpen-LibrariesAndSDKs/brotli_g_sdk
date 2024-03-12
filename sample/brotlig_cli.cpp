// Brotli-G SDK 1.1 Sample
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


#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <stdexcept>
#include <fstream>
#include <chrono>

#include "BrotliG.h"

#ifdef USE_GPU_DECOMPRESSION
#include "BrotligGPUDecoder.h"
#endif

#ifdef USE_BROTLI_CODEC
#include "brotli/c/common/constants.h"
#include "brotli/encode.h"
#include "brotli/decode.h"
#endif

#define GIGABYTES   (1024.0 * 1024.0 * 1024.0)

#define BROTLIG_FILE_EXTENSION ".brotlig"
#define DEFAULT_NUM_REPEAT 1

#ifdef USE_BROTLI_CODEC
#define BROTLI_FILE_EXTENSION ".brotli"
#define DEFAULT_BROTLI_QUALITY BROTLI_MAX_QUALITY
#define DEFAULT_BROTLI_LGWIN 24
#define DEFAULT_BROTLI_DECODE_OUTPUT_SIZE 256 * 1024 * 1024
#endif

typedef struct BROTLIG_OPTIONS_T {
    uint32_t page_size;                             // page size for compressing the source file
    uint32_t num_repeat;                            // number of times to repeat the task
    bool use_preconditioning;                       // use format-based preconditioning
    bool use_swizzling;                             // use block swizzling, BC1-5 textures only
    bool use_delta_encoding;                        // use delta encoding on color, BC1-5 texture only
    uint32_t data_format;                           // data format
    uint32_t tex_width;                             // width of texture (in pixels)
    uint32_t tex_height;                            // height of texture (in pixels)
    uint32_t tex_pitch;                             // row pitch of texture (in bytes)
    uint32_t num_packed_miplevels;                  // number of packed mip map levels
    bool is_pitch_d3d12_aligned;                    // is the texture pitch aligned with D3D12 specifications

#ifdef USE_GPU_DECOMPRESSION
    bool use_gpu;                                   // use the GPU to decompress the source file
    bool use_warp;                                  // use the warp adapter for GPU decompression
#endif

#ifdef USE_BROTLI_CODEC
    bool use_brotli;                                // use Brotli code to process the source file, else use BrotliG code
    uint32_t brotli_quality;                        // Brotli compression quality
    int32_t brotli_lgwin;                           // Brotli compression window size
    uint32_t brotli_decode_output_size;             // Brotli decompression output size
#endif

    bool verbose;                                   // print process status on console output, this will slow down performance

    BROTLIG_OPTIONS_T()
    {
        page_size = BROTLIG_DEFAULT_PAGE_SIZE;
        num_repeat = 1;
        use_preconditioning = FALSE;
        use_swizzling = FALSE;
        use_delta_encoding = FALSE;
        data_format = 0;
        tex_width = 0;
        tex_height = 0;
        tex_pitch = 0;
        num_packed_miplevels = 0;
        is_pitch_d3d12_aligned = FALSE;

#ifdef USE_GPU_DECOMPRESSION
        use_gpu = FALSE;
        use_warp = FALSE;
#ifdef USE_AGS
        use_ags = FALSE;
#endif
#endif

#ifdef USE_BROTLI_CODEC
        use_brotli = FALSE;
        brotli_quality = DEFAULT_BROTLI_QUALITY;
        brotli_lgwin = DEFAULT_BROTLI_LGWIN;
        brotli_decode_output_size = DEFAULT_BROTLI_DECODE_OUTPUT_SIZE;
#endif

        verbose = FALSE;
    }
} BROTLIG_OPTIONS;

// check if a file extension exists
inline bool EndsWith(const char* s, const char* subS)
{
    uint32_t sLen = (uint32_t)strlen(s);
    uint32_t subSLen = (uint32_t)strlen(subS);
    if (sLen >= subSLen)
    {
        return strcmp(s + (sLen - subSLen), subS) == 0;
    }
    return false;
}

// Remove a file extension
inline void RemoveExtension(std::string& srcString, const std::string& extension)
{
    uint32_t pos = (uint32_t)srcString.find(extension);
    if (pos != std::string::npos)
        srcString.erase(pos, extension.length());
}

// Read a binary file
bool ReadBinaryFile(std::string filepath, uint8_t* & content, uint32_t* size)
{
    std::ifstream ifs(filepath, std::ios::in | std::ios::binary);

    if (!ifs.is_open())
        return false;

    auto start = ifs.tellg();
    ifs.seekg(0, std::ios::end);
    auto end = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    uint32_t fsize = static_cast<uint32_t>(end - start);

    content = new uint8_t[fsize];

    ifs.read(reinterpret_cast<char*>(content), static_cast<std::streamsize>(fsize));

    ifs.close();

    *size = fsize;

    return true;
}

// Write a binary file
bool WriteBinaryFile(std::string filepath, uint8_t* content, uint32_t size)
{
    std::ofstream ofs(filepath, std::ios::out | std::ios::binary);

    if (!ofs.is_open())
        return false;

    ofs.write(reinterpret_cast<char*>(content), static_cast<std::streamsize>(size));

    ofs.close();

    return true;
}

void PrintCommandLineSyntax()
{
    printf("Sample application shows how to use Brotli-G\n\n");
    printf("It processes an input filename into a compressed file with an added extension .brotlig\n");
    printf("To decompress the file use brotlig filename.brotlig.\n");
    printf("By default, filename.brotlig decompresses to filename. For different output, provide outfilename.\n\n");
    printf(
        "Usage: brotlig [Options] filename [outfilename]\n"
        "Options:\n"
        " -pagesize <value>                     : Set encode page byte size (Default is %d, Max is %d, Min is %d)\n"
        " -precondition                         : Apply format-based preconditioning to input data before compression\n"
        "\n"
        " Preconditioning Options: \n"
        " -swizzle                              : Apply block swizzling (For Preconditioning only)\n"
        " -delta-encode                         : Apply delta encoding to color (For preconditioning only)\n"
        " -data-format <value>                  : Input data format (For preconditioning only, Default: 0, See Supported formats below for more options)\n"
        " -texture-width <value>                : Width of (top level) texture in pixels (For preconditioning only)\n"
        " -texture-height <value>               : Height of (top level) texture in pixels (For preconditioning only)\n"
        " -row-pitch <value>                    : Pitch of (top level) texutre in bytes (For preconditioning only)\n"
        " -num-mip-levels <value>               : Number of mip levels packed in this texture (Min: 1, Max: 16, For preconditioning only)\n"
        " -texture-pitch-d3d12-aligned          : Texture pitch is aligned to D3D12 specifications (For preconditioning only)\n"
#ifdef USE_GPU_DECOMPRESSION
        "\n"
        " GPU Decompression Optionn: \n"
        " -gpu                                  : Use GPU to decompress srcfile\n"
        " -warp                                 : Use warp adapter for GPU decompression\n"
#endif
#ifdef USE_BROTLI_CODEC
        "\n"
        " Brotli Options: \n"
        " -brotli                               : Use Brotli to compress into a filename.brotli file\n"
        " -brotli-quality <value>               : Set Brotli quality (Min: 0, Max: 11, Default: 11)\n"
        " -brotli-windowsize <value>            : Set Brotli window size (Min: 10, Max: 24, Default: 24)\n"
        " -brotli-decode-output-size <value>    : Set Brotli output buffer byte size\n"
#endif
        "\n"
        " Miscellaneous: \n"
        " -num-repeat <value>                   : Number of times to repeat the task (Default is 1)\n"
        " -verbose                              : Print progress\n"
        "\n"
        " Formats supported for preconditioning:\n"
        " Block compressed texture format BC1   : 1\n"
        " Block compressed texture format BC2   : 2\n"
        " Block compressed texture format BC3   : 3\n"
        " Block compressed texture format BC4   : 4\n"
        " Block compressed texture format BC5   : 5\n"
        " Unknown format                        : 0\n"
    , BROTLIG_DEFAULT_PAGE_SIZE, BROTLIG_MAX_PAGE_SIZE, BROTLIG_MIN_PAGE_SIZE);
}

void ParseCommandLine(int argCount, char* args[], std::string& srcFilePath, std::string& dstFilePath, BROTLIG_OPTIONS& params)
{
    int i = 0;

    // Options
    for (; i < argCount && args[i][0] == L'-'; ++i)
    {
        if (strcmp(args[i], "-pagesize") == 0 && i + 1 < argCount)
        {
            params.page_size = (uint32_t)std::stoi(args[++i]);
        }
        else if (strcmp(args[i], "-precondition") == 0)
        {
            params.use_preconditioning = true;
        }
        else if (strcmp(args[i], "-swizzle") == 0)
        {
            params.use_swizzling = true;
        }
        else if (strcmp(args[i], "-delta-encode") == 0)
        {
            params.use_delta_encoding = true;
        }
        else if (strcmp(args[i], "-data-format") == 0)
        {
            params.data_format = (uint32_t)std::stoi(args[++i]);
        }
        else if (strcmp(args[i], "-texture-width") == 0)
        {
            params.tex_width = (uint32_t)std::stoi(args[++i]);
        }
        else if (strcmp(args[i], "-texture-height") == 0)
        {
            params.tex_height = (uint32_t)std::stoi(args[++i]);
        }
        else if (strcmp(args[i], "-row-pitch") == 0)
        {
            params.tex_pitch = (uint32_t)std::stoi(args[++i]);
        }
        else if (strcmp(args[i], "-num-mip-levels") == 0)
        {
            params.num_packed_miplevels = (uint32_t)std::stoi(args[++i]);
        }
        else if (strcmp(args[i], "-texture-pitch-d3d12-aligned") == 0)
        {
            params.is_pitch_d3d12_aligned = true;
        }
#ifdef  USE_GPU_DECOMPRESSION
        else if (strcmp(args[i], "-gpu") == 0)
        {
            params.use_gpu = true;
        }
        else if (strcmp(args[i], "-warp") == 0)
        {
            params.use_warp = true;
        }
#endif
#ifdef USE_BROTLI_CODEC
        else if (strcmp(args[i], "-brotli") == 0)
        { 
            params.use_brotli = true;
        }
        else if (strcmp(args[i], "-brotli-quality") == 0)
        {
            params.brotli_quality = (uint32_t)std::stoi(args[++i]);
        }
        else if (strcmp(args[i], "-brotli-windowsize") == 0)
        {
            params.brotli_lgwin = (uint32_t)std::stoi(args[++i]);
        }
        else if (strcmp(args[i], "-brotli-decode-output-size") == 0)
        {
            params.brotli_decode_output_size = (uint32_t)std::stoi(args[++i]);
        }
#endif
        else if (strcmp(args[i], "-num-repeat") == 0)
        {
            params.num_repeat = (uint32_t)std::stoi(args[++i]);
        }
        else if (strcmp(args[i], "-verbose") == 0)
        {
            params.verbose = true;
        }
        else
        {
            throw std::runtime_error("Unknown command line option.");
        }
    }

    // Files
    if ((argCount - i) < 1)
    {
        throw std::runtime_error("Invalid command line syntax.");
    }

    srcFilePath = args[i++];

    if ((argCount - i) > 0)
    {
        dstFilePath = args[i];
    }
    else
        dstFilePath = "";
}

bool keypressed()
{
    if (GetAsyncKeyState(VK_ESCAPE) & 0x01)
        return true;
    return false;
}

// user can overide this to direct status messages 
// to a specific device output
#define MAX_STATUS_STRING_BUFFER 4096
void printStatus(const char* status, ...)
{
    char text[MAX_STATUS_STRING_BUFFER];
    va_list list;
    va_start(list, status);
    vsprintf_s(text, MAX_STATUS_STRING_BUFFER, status, list);
    va_end(list);
    printf(text);
}

// Called internally by the Encoder a % value ranging from 0.0% to 100% is passed in 
// Return true if processing needs to be aborted by the user
// user can print out the processCompleted using printf or cout, 
// doing so will slow down the encoding process
bool processFeedback(BROTLIG_MESSAGE_TYPE mtype, std::string msg)
{
    if (keypressed())
        return true;
    if (mtype == BROTLIG_MESSAGE_TYPE::BROTLIG_WARNING)
        printf("%s\n", msg.c_str());
    else
    {
        float v = std::stof(msg);
        printStatus("\rProcessing %3.0f %%", v);
    }
    return false;
}

int main(int argc, char* argv[])
{
    std::string      srcFilePath;
    std::string      dstFilePath;
    try
    {
        if (argc <= 1)
        {
            PrintCommandLineSyntax();
            return 1;
        }

        BROTLIG_OPTIONS pParams;
        memset(&pParams, 0, sizeof(BROTLIG_OPTIONS));
        pParams.page_size = BROTLIG_DEFAULT_PAGE_SIZE;
        pParams.num_repeat = DEFAULT_NUM_REPEAT;

#ifdef USE_BROTLI_CODEC
        pParams.brotli_quality = DEFAULT_BROTLI_QUALITY;
        pParams.brotli_lgwin = DEFAULT_BROTLI_LGWIN;
        pParams.brotli_decode_output_size = DEFAULT_BROTLI_DECODE_OUTPUT_SIZE;
#endif

        ParseCommandLine(argc - 1, argv + 1, srcFilePath, dstFilePath, pParams);

        printf("Processing %s\n", srcFilePath.c_str());

        uint32_t src_size = 0;
        uint8_t* src_data = nullptr;
        uint32_t output_size = 0;
        uint8_t* output_data = nullptr;
        double   deltaTime = 0.0;

        bool     isCompressed = false;
        double   compression_ratio = 0.0;
        double   bandwidth = 0.0;
        char*    processMessage = "";

#ifdef USE_BROTLI_CODEC
        if (pParams.use_brotli == false)
#endif // USE_BROTLI_CODEC
        {
            //--------------------------
            // Brotli-G  decompressor
            //--------------------------
            if (EndsWith(srcFilePath.c_str(), BROTLIG_FILE_EXTENSION)) {

                if (dstFilePath == "")
                {
                    dstFilePath = srcFilePath;
                    RemoveExtension(dstFilePath, BROTLIG_FILE_EXTENSION);
                }

                if (!ReadBinaryFile(srcFilePath, src_data, &src_size))
                    throw std::exception("File Not Found.");

                // DeCompress the data
                output_size = BrotliG::DecompressedSize(src_data);
                output_data = new uint8_t[output_size];
#ifdef USE_GPU_DECOMPRESSION
                if (pParams.use_gpu)
                {
                    //--------------------------
                    // Brotli-G  GPU decompressor
                    //--------------------------

                    processMessage = "BrotliG GPU decompressor";

                    uint32_t rep = 0;
                    while (rep != pParams.num_repeat)
                    {
                        printf("Round %d of %d\n", rep + 1, pParams.num_repeat);
                        double observedTime = 0.0;

                        if (DecodeGPU(pParams.use_warp, src_size, src_data, &output_size, output_data, observedTime) != BROTLIG_OK)
                            throw std::exception("BrotliG GPU Decoder Failed or Aborted.");

                        deltaTime += observedTime;
                        ++rep;
                    }
                }
                else
#endif
                {
                    //--------------------------
                    // Brotli-G  CPU decompressor
                    //--------------------------

                    processMessage = "BrotliG CPU decompressor";

                    uint32_t rep = 0;
                    while (rep != pParams.num_repeat)
                    {
                        printf("Round %d of %d\n", rep + 1, pParams.num_repeat);
                        auto start = std::chrono::high_resolution_clock::now();

                        if (BrotliG::DecodeCPU(src_size, src_data, &output_size, output_data, pParams.verbose ? processFeedback : nullptr) != BROTLIG_OK)
                            throw std::exception("BrotliG CPU Decoder Failed or Aborted.");

                        auto end = std::chrono::high_resolution_clock::now();

                        deltaTime += static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
                        ++rep;
                    }
                }
                
                printf("Saving decompressed file %s\n", dstFilePath.c_str());
                if (!WriteBinaryFile(dstFilePath, output_data, output_size))
                    throw std::exception("File Not Saved.");
            }
            else {

                //-----------------------
                // Brotli-G CPU compressor
                //-----------------------
                if (pParams.page_size < BROTLIG_MIN_PAGE_SIZE)
                    throw std::exception("Page Size is less than minimum allowed page size.");

                if (pParams.page_size > BROTLIG_MAX_PAGE_SIZE)
                    throw std::exception("Page Size exceeds maximum allowed page size.");

                processMessage = "BrotliG CPU compressor";

                dstFilePath = srcFilePath;
                dstFilePath.append(BROTLIG_FILE_EXTENSION);

                if (!ReadBinaryFile(srcFilePath, src_data, &src_size))
                    throw std::exception("File Not Found.");

                output_size = BrotliG::MaxCompressedSize(src_size);
                output_data = new uint8_t[output_size];

                BrotliG::BrotligDataconditionParams dcParams = { };
                dcParams.precondition = pParams.use_preconditioning;
                if (pParams.use_preconditioning)
                {
                    dcParams.swizzle            = pParams.use_swizzling;
                    dcParams.delta_encode       = pParams.use_delta_encoding;
                    dcParams.format             = static_cast<BROTLIG_DATA_FORMAT>(pParams.data_format);
                    dcParams.widthInPixels      = pParams.tex_width;
                    dcParams.heightInPixels     = pParams.tex_height;
                    dcParams.numMipLevels       = pParams.num_packed_miplevels;
                    dcParams.rowPitchInBytes    = pParams.tex_pitch;
                    dcParams.pitchd3d12aligned  = pParams.is_pitch_d3d12_aligned;
                }

                uint32_t rep = 0;
                while (rep != pParams.num_repeat)
                {
                    printf("Round %d of %d\n", rep + 1, pParams.num_repeat);
                    auto start = std::chrono::high_resolution_clock::now();
                    if (BrotliG::Encode(src_size, src_data, &output_size, output_data, pParams.page_size, dcParams, pParams.verbose ? processFeedback : nullptr) != BROTLIG_OK)
                        throw std::exception("BrotliG Encoder Failed or Aborted.");
                    auto end = std::chrono::high_resolution_clock::now();

                    deltaTime += static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
                    ++rep;
                }

                printf("\nSaving compressed file %s\n", dstFilePath.c_str());
                if (!WriteBinaryFile(dstFilePath, output_data, output_size))
                    throw std::exception("File Not Saved.");
                isCompressed = true;
            }
        }
#ifdef USE_BROTLI_CODEC
        else {

            //-------------------------
            // Brotli CPU decompressor
            //-------------------------
            if (EndsWith(srcFilePath.c_str(), BROTLI_FILE_EXTENSION)) {

                processMessage = "Brotli CPU decompressor";

                if (dstFilePath == "")
                {
                    dstFilePath = srcFilePath;
                    // remove the BROTLI_FILE_EXTENSION in dstFilePath 
                    RemoveExtension(dstFilePath, BROTLI_FILE_EXTENSION);
                }

                if (!ReadBinaryFile(srcFilePath, src_data, &src_size))
                    throw std::exception("File Not Found.");

                // DeCompress the data
                size_t output_sizet = (size_t)pParams.brotli_decode_output_size;
                output_data = new uint8_t[output_sizet];

                uint32_t rep = 0;
                while (rep != pParams.num_repeat)
                {
                    printf("Round %d of %d\n", rep + 1, pParams.num_repeat);
                    auto start = std::chrono::high_resolution_clock::now();
                    BrotliDecoderResult result = BrotliDecoderDecompress(src_size, src_data, &output_sizet, output_data);
                    auto end = std::chrono::high_resolution_clock::now();

                    if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT)
                        throw std::exception("Brotli Decoder Failed. Set larger output buffer size.");
                    else if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT
                        || result == BROTLI_DECODER_RESULT_ERROR)
                        throw std::exception("Brotil Decoder Failed. Input file may be corrupted.");

                    deltaTime += static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
                    output_size = (uint32_t)output_sizet;
                    ++rep;
                }

                // Save the uncompressed file
                printf("\nSaving decompressed file %s\n", dstFilePath.c_str());
                if (!WriteBinaryFile(dstFilePath, output_data, output_size))
                    throw std::exception("File Not Saved.");

            }
            else {

                //-----------------------
                // Brotli CPU compressor
                //-----------------------
                processMessage = "Brotli CPU compressor";

                dstFilePath = srcFilePath;
                dstFilePath.append(BROTLI_FILE_EXTENSION);

                if (!ReadBinaryFile(srcFilePath, src_data, &src_size))
                    throw std::exception("File Not Found.");

                size_t output_sizet = BrotliEncoderMaxCompressedSize(src_size);
                uint8_t* output_data = new uint8_t[output_sizet];

                uint32_t rep = 0;
                while (rep != pParams.num_repeat)
                {
                    printf("Round %d of %d\n", rep + 1, pParams.num_repeat);
                    auto start = std::chrono::high_resolution_clock::now();
                    if (!BrotliEncoderCompress(pParams.brotli_quality,
                        pParams.brotli_lgwin,
                        BROTLI_DEFAULT_MODE,
                        src_size,
                        src_data,
                        &output_sizet,
                        output_data))
                    {
                        throw std::exception("Brotli Encoder Failed or Aborted.");
                    }
                    auto end = std::chrono::high_resolution_clock::now();

                    deltaTime += static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
                    output_size = (uint32_t)output_sizet;
                    ++rep;
                }

                printf("\nSaving compressed file %s\n", dstFilePath.c_str());
                if (!WriteBinaryFile(dstFilePath, output_data, output_size))
                    throw std::exception("File Not Saved.");
                isCompressed = true;
            }
        }
#endif
        deltaTime /= (double)pParams.num_repeat;
        if (deltaTime > 0.0)
            bandwidth = (static_cast<double>(src_size) / deltaTime) * (1000.0 / GIGABYTES);
        if ((output_size > 0) && isCompressed)
            compression_ratio = (static_cast<double>(src_size) / static_cast<double>(output_size));

        printf("%s\n", processMessage);
        printf("Input size        : %i bytes\n", src_size);
        printf("Output size       : %i bytes\n", output_size);
        printf("Processed in      : %.0f ms\n", deltaTime);
        printf("Bandwidth         : %.6f GiB/s\n", bandwidth);

        if (compression_ratio > 0.0f) 
            printf("Compression Ratio : %.2f\n", compression_ratio);

        if (output_data != nullptr)  delete[](output_data);
        if (src_data != nullptr) delete[](src_data);
    }
    catch (const std::exception& ex)
    {
        fprintf(stderr, "ERROR: %s\n", ex.what());
        return -1;
    }

}
