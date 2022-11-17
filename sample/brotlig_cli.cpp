// Brotli-G SDK 1.0 Sample
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

#ifdef USE_BROTLI_CODEC
#define BROTLI_FILE_EXTENSION ".brotli"
#define DEFAULT_BROTLI_QUALITY BROTLI_MAX_QUALITY
#define DEFAULT_BROTLI_LGWIN 24
#define DEFAULT_BROTLI_DECODE_OUTPUT_SIZE 256 * 1024 * 1024
#endif

typedef struct BROTLIG_OPTIONS_T {
#ifdef USE_GPU_DECOMPRESSION
    bool use_gpu;                                   // use the GPU to decompress the source file
    bool use_warp;                                  // use the warp adapter for GPU decompression
#endif
    bool verbose;                                   // print process status on console output, this will slow down performance
#ifdef USE_BROTLI_CODEC
    bool use_brotli;                                // use Brotli code to process the source file, else use BrotliG code
    uint32_t brotli_quality;                        // Brotli compression quality
    int32_t brotli_lgwin;                           // Brotli compression window size
    uint32_t brotli_decode_output_size;             // Brotli decompression output size
#endif

    BROTLIG_OPTIONS_T()
    {
#ifdef USE_GPU_DECOMPRESSION
        use_gpu = FALSE;
        use_warp = FALSE;
#endif
        verbose = FALSE;
#ifdef USE_BROTLI_CODEC
        use_brotli = FALSE;
        brotli_quality = DEFAULT_BROTLI_QUALITY;
        brotli_lgwin = DEFAULT_BROTLI_LGWIN;
        brotli_decode_output_size = DEFAULT_BROTLI_DECODE_OUTPUT_SIZE;
#endif
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
    puts(
        "Usage: brotlig [Options] filename [outfilename]\n"
        "Options:\n"
        " -verbose: Print progress on screen\n"
#ifdef USE_GPU_DECOMPRESSION
        " -gpu    : Use GPU to decompress srcfile\n"
        " -warp   : Use warp adapter for GPU decompression\n"
#endif
#ifdef USE_BROTLI_CODEC
        " -brotli : Use Brotli to compress into a filename.brotli file\n"
        " -brotli-quality <value> : Set Brotli quality (Min: 0, Max: 11, Default: 11)\n"
        " -brotli-windowsize <value> : Set Brotli window size (Min: 10, Max: 24, Default: 24)\n"
        " -brotli-decode-output-size <value> : Set Brotli output buffer byte size\n"
#endif
    );
}

void ParseCommandLine(int argCount, char* args[], std::string& srcFilePath, std::string& dstFilePath, BROTLIG_OPTIONS& params)
{
    int i = 0;

    // Options
    for (; i < argCount && args[i][0] == L'-'; ++i)
    {
        if (strcmp(args[i], "-verbose") == 0)
        {
            params.verbose = true;
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
bool processFeedback(float processCompleted)
{
    if (keypressed())
        return true;
    printStatus("\rProcessing %3.0f %%", processCompleted);
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
        char* processMessage = "";

#ifdef USE_BROTLI_CODEC
        if (pParams.use_brotli == false)
#endif // USE_BROTLI_CODEC
        {
            //--------------------------
            // BrotliG  decompressor
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
                    // BrotliG  GPU decompressor
                    //--------------------------

                    processMessage = "Brotli-G GPU decompressor";

                    if (DecodeGPU(pParams.use_warp, src_size, src_data, &output_size, output_data, deltaTime) != BROTLIG_OK)
                        throw std::exception("Brotli-G GPU Decoder Failed or Aborted.");
                }
                else
#endif
                {
                    //--------------------------
                    // BrotliG  CPU decompressor
                    //--------------------------

                    processMessage = "Brotli-G CPU decompressor";

                    auto start = std::chrono::high_resolution_clock::now();
                    if (BrotliG::DecodeCPU(src_size, src_data, &output_size, output_data, pParams.verbose ? processFeedback : nullptr) != BROTLIG_OK)
                        throw std::exception("Brotli-G CPU Decoder Failed or Aborted.");
                    auto end = std::chrono::high_resolution_clock::now();

                    deltaTime = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
                   
                }
                
                printf("Saving decompressed file %s\n", dstFilePath.c_str());
                if (!WriteBinaryFile(dstFilePath, output_data, output_size))
                    throw std::exception("File Not Saved.");
            }
            else {

                //-----------------------
                // BrotliG CPU compressor
                //-----------------------
                processMessage = "Brotli-G CPU compressor";

                dstFilePath = srcFilePath;
                dstFilePath.append(BROTLIG_FILE_EXTENSION);

                if (!ReadBinaryFile(srcFilePath, src_data, &src_size))
                    throw std::exception("File Not Found.");

                output_size = BrotliG::MaxCompressedSize(src_size);
                output_data = new uint8_t[output_size];

                auto start = std::chrono::high_resolution_clock::now();
                if (BrotliG::Encode(src_size, src_data, &output_size, output_data, pParams.verbose ? processFeedback:nullptr) != BROTLIG_OK)
                    throw std::exception("Brotli-G Encoder Failed or Aborted.");
                auto end = std::chrono::high_resolution_clock::now();

                deltaTime = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

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
                output_data = new uint8_t[output_size];

                auto start = std::chrono::high_resolution_clock::now();
                BrotliDecoderResult result = BrotliDecoderDecompress(src_size,src_data,&output_sizet,output_data);
                auto end = std::chrono::high_resolution_clock::now();

                deltaTime = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
                output_size = (uint32_t)output_sizet;


                if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT)
                    throw std::exception("Brotli Decoder Failed. Set larger output buffer size.");
                else if (result == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT
                    || result == BROTLI_DECODER_RESULT_ERROR)
                    throw std::exception("Brotil Decoder Failed. Input file may be corrupted.");

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

                auto start = std::chrono::high_resolution_clock::now();
                if (!BrotliEncoderCompress( pParams.brotli_quality,
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

                deltaTime = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
                output_size = (uint32_t)output_sizet;

                printf("\nSaving compressed file %s\n", dstFilePath.c_str());
                if (!WriteBinaryFile(dstFilePath, output_data, output_size))
                    throw std::exception("File Not Saved.");
                isCompressed = true;
            }
        }
#endif

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
