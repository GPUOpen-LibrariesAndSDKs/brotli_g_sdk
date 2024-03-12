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

#include "common/BrotligUtils.h"

#include "BrotligHuffmanTable.h"

using namespace BrotliG;

static const uint16_t FixedCodes[4][4] = {
        { 0, 1, 0, 0 },
        { 0, 2, 3, 0 },
        { 0, 1, 2, 3 },
        { 0, 2, 6, 7 }
};

static const uint16_t FixedCodelengths[4][4] = {
        { 1, 1, 0, 0 },
        { 1, 2, 2, 0 },
        { 2, 2, 2, 2 },
        { 1, 2, 3, 3 }
};

static const uint16_t DyanmicCodeLenReadOrder[BROTLI_CODE_LENGTH_CODES] = {
            1, 2, 3, 4, 0, 5, 17, 6, 16, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

void GenerateHuffmanTable(uint16_t lens[], size_t size, uint16_t counts[], uint16_t next_code[], uint16_t numcodelengths, uint16_t symbols[], uint16_t codelens[])
{    
    // Find the numerical values of the smallest code for each code length
    uint16_t i = 0;
    counts[0] = 0;
    for (i = 1; i < numcodelengths; ++i) next_code[i] = (next_code[i - 1] + counts[i - 1]) << 1;

    // Fill the table
    uint16_t codelen = 0, symcount = 0, lencount = 0, code = 0, startIndex = 0, leftbits = 0, maxcodelength = numcodelengths - 1;
    uint16_t* symPtr = nullptr;
    uint16_t* lenPtr = nullptr;
    for (i = 0; i < size; ++i)
    {
        codelen = lens[i];
        if (codelen == 0) continue;

        leftbits = maxcodelength - codelen;
        code = next_code[codelen]++;
        startIndex = code << leftbits;
        symcount = lencount = uint16_t(1) << leftbits;

        symPtr = symbols + startIndex;
        while (symcount--) *symPtr++ = i;

        lenPtr = codelens + startIndex;
        while (lencount--) *lenPtr++ = codelen;
    }
}

void BrotliG::LoadHuffmanTable(
    BrotligDeswizzler& reader,
    size_t alphabet_size,
    uint16_t symbols[],
    uint16_t codelens[])
{
    uint32_t max_bits = Log2Floor(static_cast<uint32_t>(alphabet_size - 1));
    uint32_t ttype = reader.ReadAndConsume(2);

    switch (ttype)
    {
    case 0:
    {
        reader.Consume(4);
        uint16_t symbol = (uint16_t)reader.ReadAndConsume(max_bits);

        uint32_t symcount = BROTLIG_HUFFMAN_TABLE_SIZE;
        while (symcount--) *symbols++ = symbol;

        uint32_t lencount = BROTLIG_HUFFMAN_TABLE_SIZE;
        while (lencount--) *codelens++ = 0;

        reader.BSReset();
        break;
    }
    case 1:
    {
        uint32_t num_symbols = reader.ReadAndConsume(2) + 1;
        uint32_t tree_select = reader.ReadAndConsume(1);
        reader.Consume(1);

        size_t table_idx = num_symbols < 4 ? num_symbols - 2 : tree_select ? 3 : 2;
        uint16_t symbol = 0, codelen = 0, symcount = 0, lencount = 0;
        for (size_t i = 0; i < num_symbols; ++i)
        {
            codelen = FixedCodelengths[table_idx][i];
            symbol = (uint16_t)reader.ReadAndConsume(max_bits);
            symcount = lencount = uint16_t(1) << (BROTLIG_HUFFMAN_MAX_CODE_LENGTH - codelen);

            while (symcount--) *symbols++ = symbol;
            while (lencount--) *codelens++ = codelen;

            reader.BSSwitch();
        }

        reader.BSReset();
        break;
    }
    case 2:
    {
        uint32_t num_len_symbols = reader.ReadAndConsume(4) + 4;

        uint16_t len_sym_data[BROTLI_CODE_LENGTH_CODES];
        uint16_t len_blCounts[BROTLIG_HUFFMAN_NUM_CODE_LENGTH_CODE_LENGTH] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };
        uint16_t len_next_code[BROTLIG_HUFFMAN_NUM_CODE_LENGTH_CODE_LENGTH] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };

        uint16_t codelen = 0;
        uint32_t i = 0;
        for (i = 0; i < num_len_symbols; ++i)
        {
            codelen = reader.ReadAndConsume(5);
            len_sym_data[DyanmicCodeLenReadOrder[i]] = codelen;
            ++len_blCounts[codelen];
            reader.BSSwitch();
        }

        uint16_t len_symbols[BROTLIG_HUFFMAN_CODE_LENGTH_TABLE_SIZE];
        uint16_t len_codelens[BROTLIG_HUFFMAN_CODE_LENGTH_TABLE_SIZE];
        GenerateHuffmanTable(len_sym_data, num_len_symbols, len_blCounts, len_next_code, BROTLIG_HUFFMAN_NUM_CODE_LENGTH_CODE_LENGTH, len_symbols, len_codelens);

        reader.BSReset();

        uint16_t len_symbol = 0, prev_len_symbol = BROTLI_INITIAL_REPEATED_CODE_LENGTH;
        uint16_t len_code = 0, rev_len_code = 0;
        uint32_t num_reps = 0;

        uint16_t data[BROLTIG_NUM_COMMAND_SYMBOLS_EFFECTIVE];
        uint16_t blCounts[BROTLIG_HUFFMAN_NUM_CODE_LENGTH] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };
        uint16_t next_code[BROTLIG_HUFFMAN_NUM_CODE_LENGTH] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };

        uint16_t* dataPtr = data;
        size_t symbols_left = alphabet_size;
        while(symbols_left)
        {
            len_code = static_cast<uint16_t>(reader.ReadNoConsume9());
            rev_len_code = sBrotligReverseBits9[len_code];
            reader.Consume(len_codelens[rev_len_code]);
            len_symbol = len_symbols[rev_len_code];

            if (len_symbol == BROTLI_REPEAT_PREVIOUS_CODE_LENGTH)
            {
                num_reps = reader.ReadAndConsume(2) + 3;
                blCounts[prev_len_symbol] += num_reps;
                assert(num_reps <= symbols_left);
                symbols_left -= num_reps;
                while (num_reps--) *dataPtr++ = prev_len_symbol;
            }
            else if (len_symbol == BROTLI_REPEAT_ZERO_CODE_LENGTH)
            {
                num_reps = reader.ReadAndConsume(3) + 3;
                blCounts[0] += num_reps;
                assert(num_reps <= symbols_left);
                symbols_left -= num_reps;
                while (num_reps--) *dataPtr++ = 0;
            }
            else
            {
                prev_len_symbol = len_symbol;
                ++blCounts[len_symbol];
                --symbols_left;
                *dataPtr++ = len_symbol;
            }

            reader.BSSwitch();
        }

        GenerateHuffmanTable(data, alphabet_size, blCounts, next_code, BROTLIG_HUFFMAN_NUM_CODE_LENGTH, symbols, codelens);
        
        reader.BSReset();
        break;
    }
    default:
        throw std::exception("Error loading huffman table. Incorrect tree type.");
    }
}