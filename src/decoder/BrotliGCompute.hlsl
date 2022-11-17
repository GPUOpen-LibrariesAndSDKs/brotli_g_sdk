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

#define const32_t static const uint
#define DIV_ROUND_UP(a, b)  (((a) + (b) - 1) / (b))

const32_t LOG_NUM_LANES								= 5;	                //	number of bits for workgroup index
const32_t NUM_LANES									= 1u << LOG_NUM_LANES;	//	workgroup is sized correspondingly
const32_t CMD_GRP_SIZE								= 1;

const32_t BROTLIG_WORK_PAGE_SIZE_UNIT				= 32 * 1024;
const32_t BROTLIG_WORK_STREAM_HEADER_SIZE			= 8;

const32_t BROTLIG_NUM_CATEGORIES					= 3;

const32_t NIBBLE_TOTAL_BITS                         = 4;
const32_t BYTE_TOTAL_BITS                           = 8;
const32_t DWORD_TOTAL_BITS							= 32;
const32_t DWORD_TOTAL_BYTES							= DWORD_TOTAL_BITS / BYTE_TOTAL_BITS;
const32_t DWORD_TOTAL_NIBBLES						= DWORD_TOTAL_BITS / NIBBLE_TOTAL_BITS;
const32_t SHORT_TOTAL_BITS							= 16;
const32_t SHORT_TOTAL_BYTES							= SHORT_TOTAL_BITS / BYTE_TOTAL_BITS;
const32_t SHORT_TOTAL_NIBBLES						= SHORT_TOTAL_BITS / NIBBLE_TOTAL_BITS;

const32_t BROTLIG_NUM_LITERAL_SYMBOLS				= 256;
const32_t BROTLIG_EOS_COMMAND_SYMBOL				= 704;
const32_t BROTLIG_NUM_COMMAND_SYMBOLS               = BROTLIG_EOS_COMMAND_SYMBOL + 24;
const32_t BROTLIG_NUM_COMMAND_SYMBOLS_WITH_SENTINEL	= BROTLIG_NUM_COMMAND_SYMBOLS;
const32_t BROTLIG_NUM_DISTANCE_SYMBOLS				= 544;
const32_t BROTLIG_NUM_MAX_SYMBOLS					= BROTLIG_NUM_COMMAND_SYMBOLS_WITH_SENTINEL;
//const32_t BROTLIG_NUM_MAX_SYMBOLS					= max (
//            BROTLIG_NUM_COMMAND_SYMBOLS_WITH_SENTINEL, max (
//            BROTLIG_NUM_LITERAL_SYMBOLS,
//            BROTLIG_NUM_DISTANCE_SYMBOLS));

const32_t BROTLIG_WINDOW_GAP						= 16;
const32_t BROTLIG_REPEAT_PREVIOUS_CODE_LENGTH		= 16;
const32_t BROTLIG_REPEAT_ZERO_CODE_LENGTH			= 17;
const32_t BROTLIG_MAX_HUFFMAN_CODE_BITSIZE_LOG		= 4;
const32_t BROTLIG_MAX_HUFFMAN_CODE_BITSIZE			= 1u << BROTLIG_MAX_HUFFMAN_CODE_BITSIZE_LOG;
const32_t BROTLIG_MAX_HUFFMAN_CODE_LENGTH           = BROTLIG_MAX_HUFFMAN_CODE_BITSIZE - 1;

const32_t BROTLIG_LENGTH_ENCODER_SIZE				= 18;
const32_t BROTLIG_LENGTH_ENCODER_MAX_KEY_LENGTH		= 7;
const32_t BROTLIG_LENGTH_ENCODER_MAX_EXTRA_LENGTH	= 3;

const32_t DECODER_LENGTH_SHIFT                      = SHORT_TOTAL_BITS -
                                                      BROTLIG_MAX_HUFFMAN_CODE_BITSIZE_LOG;

#ifdef USE_METACOMMAND_INTERFACE
#define _RootSig                                                \
    "RootFlags(0), "                                            \
    "DescriptorTable( CBV( b0, space = 0, numDescriptors=1) )," \
    "DescriptorTable( SRV( t0, space = 0, numDescriptors=2) )," \
    "DescriptorTable( UAV( u0, space = 0, numDescriptors=1) )," \
    "DescriptorTable( UAV( u1, space = 0, numDescriptors=1) )," \
    "DescriptorTable( UAV( u2, space = 0, numDescriptors=1) ),"
#else
#define _RootSig                                                \
    "RootFlags(0), "                                            \
    "DescriptorTable( CBV( b0, space = 0, numDescriptors=1) )," \
    "DescriptorTable( SRV( t0, space = 0, numDescriptors=2) )," \
    "DescriptorTable( UAV( u0, space = 0, numDescriptors=1) )," \
    "DescriptorTable( UAV( u1, space = 0, numDescriptors=1) ),"
#endif

struct CmdLut
{
	uint insert_len_extra;
	uint copy_len_extra;
	int dist_code;
	uint ctx;
	uint insert_len_offset;
	uint copy_len_offset;
};

ByteAddressBuffer input : register(t0);     // Input buffer
RWByteAddressBuffer meta : register(u0);    // Metadata buffer
RWByteAddressBuffer output : register(u1);  // Output buffer

#ifdef USE_METACOMMAND_INTERFACE
RWByteAddressBuffer scratch : register(u2); // Scratch buffer
#endif

uint MaskLsbs (uint n) { return (1u << n) - 1; }
min16uint MaskLsbs16 (min16uint n) { return (min16uint (1) << n) - 1; }
uint MaskLsbs (uint a, uint n) { return a & MaskLsbs (n); }
min16uint MaskLsbs (min16uint a, uint n) { return a & MaskLsbs16 (min16uint (n)); }
//  dword addressing and masking
uint MakeSizeTAddr (uint addr, uint bitlength = 8, uint sizeT = DWORD_TOTAL_BITS)	{
    return addr / (sizeT / bitlength);
}
uint MakeAlignAddr (uint addr, uint bitlength = 8, uint sizeT = DWORD_TOTAL_BITS)	{
    return addr & -(sizeT / bitlength);
}
uint MakeAlignShift (uint addr, uint bitlength = 8, uint sizeT = DWORD_TOTAL_BITS) {
    return (addr & ((sizeT / bitlength) - 1)) * bitlength;
}
uint MakeReadAlignData (uint addr, uint data, uint bitlength = 8, uint sizeT = DWORD_TOTAL_BITS)
{
    return (data >> MakeAlignShift (addr, bitlength, sizeT)) & MaskLsbs (bitlength);
}
uint MakeWriteAlignData (uint addr, uint data, uint bitlength = 8, uint sizeT = DWORD_TOTAL_BITS)
{
    return (data & MaskLsbs (bitlength)) << MakeAlignShift (addr, bitlength, sizeT);
}
uint bfe (uint data, uint pos, uint n, uint base = 0)
{
    return ((data >> pos) & MaskLsbs (n)) + base;
}
struct BitField {
    uint d;
    uint bit (uint from, uint to = 0) // inclusive, from <= to < 31
    {
        uint s = min (from, 31);
        uint e = max (s, to);
        uint m = uint (-1) >> (31 - e + s);
        return (d >> s) & m;
    };
};
uint GetBitSize (uint num) { return (firstbithigh (num - 1) + 1) % DWORD_TOTAL_BITS; };
uint DivRoundUp (uint num, uint div) { return (num + div - 1) / div; };
uint2 __div3correct (uint num) { num += (num & 3) == 3; return uint2 (num >> 2, num & 3); };
min16uint2 __div3correct (min16uint num) { num += (num & 3) == 3; return min16uint2 (num >> 2, num & 3); };
min16uint2 Div3bit8 (min16uint num) { return __div3correct ((num * 0x55 + (num >> 2)) >> 6); }
                            // please note that 8bit * 0x155 > 16bit hence uint
uint2 Div3bit14 (uint num) { return __div3correct ((num * 0x15555) >> 16); }

min16uint BitTable (uint index, uint fieldlength, uint table) {
    return min16uint (table >> (index * fieldlength)) & min16uint (MaskLsbs (fieldlength));
}
min16uint BitTable (uint index, uint fieldlength, uint64_t table) {
    return min16uint (table >> (index * fieldlength)) & min16uint (MaskLsbs (fieldlength));
}

inline uint ByteLoad (uint addr)
{
    uint shft = (addr & (DWORD_TOTAL_BYTES - 1)) << 3;
    addr	  =  addr & -DWORD_TOTAL_BYTES;

    return (output.Load (addr) >> shft) & MaskLsbs (8);
}

inline void ByteStore (uint addr, uint data)
{
    uint shft = (addr & (DWORD_TOTAL_BYTES - 1)) << 3;
    addr	  =  addr & -DWORD_TOTAL_BYTES;
    uint dummy;

    output.InterlockedOr (addr, (data & MaskLsbs (BYTE_TOTAL_BITS)) << shft, dummy);
}

int ActiveIndexPrevious (const bool cond, uint laneIx)
{
    return firstbithigh (WaveActiveBallot (cond).x & MaskLsbs (laneIx));
}

struct DecoderState {


    uint readPointer;
    min16uint validBits;
    min16uint prefetched;
	uint64_t hold;
    const32_t stride = 1;
    uint LANE;
    
    void FetchNextDword (bool en)
    {
        if (en && validBits <= DWORD_TOTAL_BITS)
        {
            uint readBuffer = input.Load (readPointer);
            hold |= uint64_t (readBuffer) << validBits;
            validBits   += DWORD_TOTAL_BITS;
            readPointer += DWORD_TOTAL_BYTES * stride;
        }
	}
	void Init (uint i) {
        LANE = WaveGetLaneIndex ();
        uint2 tmp = input.Load2 (i);
        validBits = DWORD_TOTAL_BITS * 2;
        hold = (uint64_t (tmp.y) << DWORD_TOTAL_BITS) | tmp.x;
        readPointer = i + DWORD_TOTAL_BYTES * 2;
        // FetchNextDword (true);
    }
	void DropBitsNoFetch (uint size, bool en) {
        size  = en ? size : 0;
		hold	 >>= size;
        validBits -= min16uint (size);
    }
	void DropBits (uint size, bool en) {
        size  = en ? size : 0;
		hold	 >>= size;
        validBits -= min16uint (size);
        FetchNextDword (en);
	}
	uint GetBits (uint size = 32) {
        return uint (hold) & (size == 32 ? -1 : MaskLsbs (size));
    }
	uint GetAndDropBits (uint size, bool en) {
        uint retval = en ? GetBits (size) : 0;
        DropBits (size, en);
        return retval;
    }
};

uint WaveHistogram (uint tag, bool present, uint laneIx)
{
    uint result = 0;
    uint mask = WaveActiveBallot (present).x;
    while (mask)
    {
        uint l = firstbitlow (mask);
        uint t = WaveReadLaneAt (tag, l);
        uint n = countbits (WaveActiveBallot (t == tag).x & mask);

        result = laneIx == t ? n : result;

        uint m = WaveActiveBallot (t == tag).x;
        mask &= ~m;
    }
    return result;
}
uint WaveHistogram (uint tag, uint num, bool present, uint laneIx)
{
    uint result = 0;
    uint mask = WaveActiveBallot (num && present).x;
    while (mask)
    {
        uint l = firstbithigh (mask);
        uint t = WaveReadLaneAt (tag, l);
        bool b = t == tag;
        uint n = WaveActiveSum (b ? num : 0);

        result = laneIx == t ? n : result;

        uint m = WaveActiveBallot (b).x;
        mask &= ~m;
    }
    return result;
}
uint WaveWriteLane (uint lane, uint value, bool present, uint laneIx, uint init = 0)
{
    uint result = init;
    uint mask = WaveActiveBallot (present).x;
    while (mask)
    {
        uint l = firstbithigh (mask);
        uint t = WaveReadLaneAt (lane, l);
        uint n = WaveReadLaneAt (value, l);
        bool b = t == lane;

        result = laneIx == t ? n : result;

        uint m = WaveActiveBallot (b).x;
        mask &= ~m;
    }
    return result;
}

const32_t SYMBOL_LENGTHS_DWORDS = DIV_ROUND_UP (
                BROTLIG_NUM_COMMAND_SYMBOLS_WITH_SENTINEL, DWORD_TOTAL_NIBBLES);
const32_t SYMBOL_LENGTHS_SHORTS = DIV_ROUND_UP (
                BROTLIG_NUM_COMMAND_SYMBOLS_WITH_SENTINEL, SHORT_TOTAL_NIBBLES);

const32_t DICTIONARY_COMPACT_LITERAL_SIZE  = DIV_ROUND_UP (BROTLIG_NUM_LITERAL_SYMBOLS , 4);
const32_t DICTIONARY_COMPACT_DISTANCE_SIZE = DIV_ROUND_UP (BROTLIG_NUM_DISTANCE_SYMBOLS, 3);
const32_t DICTIONARY_COMPACT_COMMAND_SIZE  = DIV_ROUND_UP (BROTLIG_NUM_COMMAND_SYMBOLS , 3);
const32_t DICTIONARY_COMPACT_TOTAL_SIZE = DICTIONARY_COMPACT_LITERAL_SIZE  +
                                          DICTIONARY_COMPACT_DISTANCE_SIZE +
                                          DICTIONARY_COMPACT_COMMAND_SIZE;
groupshared uint gDictionary [DICTIONARY_COMPACT_TOTAL_SIZE];
uint GetRange (uint type)
{
    return type == 0 ? BROTLIG_NUM_LITERAL_SYMBOLS :
           type == 1 ? BROTLIG_NUM_COMMAND_SYMBOLS :
                       BROTLIG_NUM_DISTANCE_SYMBOLS;
}
uint GetBase (uint type)
{
    return type == 0 ? 0 : type == 1 ? DICTIONARY_COMPACT_LITERAL_SIZE :
        DICTIONARY_COMPACT_LITERAL_SIZE + DICTIONARY_COMPACT_COMMAND_SIZE;
}
void ClearDictionary (uint type, uint laneIx)
{
    for (uint ptr = laneIx; ptr < GetRange (type) / 4; ptr += NUM_LANES)
        gDictionary [GetBase (type) + ptr] = 0;
}
bool CheckDictionaryAddress (uint type, uint addr, uint data, out uint ix, out uint sh, out uint dt)
{
    uint __range = GetRange (type);
    uint __field = type == 0 ? 8 : 10;
    ix = sh = dt = 0;

    uint2 a;
    if (type)
        a = Div3bit14 (addr);
    else
        a = uint2 (addr / DWORD_TOTAL_BYTES, addr % DWORD_TOTAL_BYTES);
    ix = a.x;
    sh = a.y * __field;
    dt = (data & MaskLsbs (__field)) << sh;
    return false;
}
void SetSymbol (uint type, uint addr, min16uint data)
{
    uint base = GetBase (type);
    uint ix, sh, dt;

    if (CheckDictionaryAddress (type, addr, data, ix, sh, dt))
        return;
    
    InterlockedOr (gDictionary [base + ix], dt);
    
    GroupMemoryBarrierWithGroupSync ();
};
min16uint GetSymbol (uint type, uint addr)
{
    uint base = GetBase (type);
    uint ix, sh, dt;

    if (CheckDictionaryAddress (type, addr, 0, ix, sh, dt))
        return 0;
    
    dt = gDictionary [base + ix];
    return min16uint (dt >> sh) & MaskLsbs16 (type ? 10 : 8);
}

#define SEPARATE_SYMBOL_TABLE
// 144 * 8 packed 4-bit lengths for symbols
groupshared uint gSymbolLengths [SYMBOL_LENGTHS_DWORDS];
void SymbolLengthsReset (uint laneIx)
{
    for (uint i = laneIx; i < SYMBOL_LENGTHS_DWORDS; i += NUM_LANES)
        gSymbolLengths [i] = 0;
}

void HuffmalLengthTableInsert (uint data, uint rle, uint addr)
{
    for (uint i = 0; i < rle; i++)
    {
        uint a = MakeSizeTAddr (addr + i, BROTLIG_MAX_HUFFMAN_CODE_BITSIZE_LOG);
        uint d = MakeWriteAlignData (addr + i, data, BROTLIG_MAX_HUFFMAN_CODE_BITSIZE_LOG);

        InterlockedOr (gSymbolLengths [a], d);
    }
}

struct SymbolLengths
{
    uint __data [DIV_ROUND_UP (SYMBOL_LENGTHS_DWORDS, NUM_LANES)];
    
    void Copy (uint row, uint laneIx) {
        uint addr = row * NUM_LANES + laneIx;
        uint data = addr < SYMBOL_LENGTHS_DWORDS ? gSymbolLengths [addr] : 0;
        __data [row] = data;
    };
    uint SafeGet (uint addr) {
        uint da = MakeSizeTAddr (addr, BROTLIG_MAX_HUFFMAN_CODE_BITSIZE_LOG);
        uint p1 = WaveReadLaneAt (__data [0], da % NUM_LANES);
        uint p2 = WaveReadLaneAt (__data [1], da % NUM_LANES);
        uint p3 = WaveReadLaneAt (__data [2], da % NUM_LANES);
        uint dd = da / NUM_LANES == 0 ? p1 : da / NUM_LANES == 1 ? p2 : p3;
        return MakeReadAlignData (addr, dd, BROTLIG_MAX_HUFFMAN_CODE_BITSIZE_LOG);
    };
    uint LaneGet (uint addr) {
        uint da = MakeSizeTAddr (addr, BROTLIG_MAX_HUFFMAN_CODE_BITSIZE_LOG);
        uint ra = da / NUM_LANES;
        uint dd = ra == 0 ? __data [0] : ra == 1 ? __data [1] : __data [2];
        uint pp = WaveReadLaneAt (dd, da % NUM_LANES);
        return MakeReadAlignData (addr, pp, BROTLIG_MAX_HUFFMAN_CODE_BITSIZE_LOG);
    };
    bool IsSingleLane (const uint tSize) {
        return tSize <= NUM_LANES * DWORD_TOTAL_BITS / BROTLIG_MAX_HUFFMAN_CODE_BITSIZE_LOG;
    };
};

uint HuffmanDefineRegions (out uint offset, uint number, uint bitlength)
{
    const32_t GUARD_BITS = 1;

    //  generate offsets into symbol table for each length for sorting
    offset = WavePrefixSum (number);
		
    uint increment = number << (SHORT_TOTAL_BITS - bitlength - GUARD_BITS);
    return WavePrefixSum (increment);
}
uint CompactHuffmanSearchEntry (uint c, uint o, uint l)
{
    uint mo = MaskLsbs (DECODER_LENGTH_SHIFT);
    uint ml = MaskLsbs (SHORT_TOTAL_BITS - DECODER_LENGTH_SHIFT);
    uint shiftedLength = (l & ml) << DECODER_LENGTH_SHIFT;
    return (c << SHORT_TOTAL_BITS) | (o & mo) | shiftedLength;
}
uint CreateCompactedHuffmanSearchTable (uint number, out uint table, uint laneIx)
{
    uint offset;
    uint key = HuffmanDefineRegions (offset, number, laneIx);

    bool isvalue = number != 0 && laneIx < BROTLIG_MAX_HUFFMAN_CODE_BITSIZE;
        
    key = WaveReadLaneAt (key, laneIx + 1);
        
    uint val = CompactHuffmanSearchEntry (key, offset, laneIx);
    
    uint mask = WaveActiveBallot (isvalue).x;
    for (uint ix = 0; mask; mask &= mask - 1, ix++)
    {
        uint lane = firstbitlow (mask);
        uint value = WaveReadLaneAt (val, lane);
            
        table = laneIx == ix ? value : table;
    }

    return offset + number;
}

struct HuffmanDecoder
{
    const32_t __field = 6;
    const32_t carrymask = ((((((((((1u) << __field) |
                                    1u) << __field) |
                                    1u) << __field) |
                                    1u) << __field) |
                                    1u) << __field) >> 1;

	uint Init(uint number, uint laneIx) {
		
        uint src = 0xfffff000;
        uint offset = CreateCompactedHuffmanSearchTable (number, src, laneIx);

#define DO_NOT_PREPARE_QUICK_SEARCH_DATA
        return offset;
    }

	void Reset (uint laneIx)
	{
    }
};

struct InlineDecoder
{
    uint __table;
    
    void Reset () { __table = 0; };
    void Init (uint t, uint sel, uint laneIx)
    {
        if (sel)
        {
            t = WaveReadLaneAt (t, laneIx ^ (NUM_LANES / 2));
            __table = laneIx >= NUM_LANES / 2 ? t : __table;
        } else
            __table = laneIx < NUM_LANES / 2 ? t : __table;
    };
    min16uint Decode (uint sel, uint type, uint key, out uint length)
    {
        if (sel)
            sel = NUM_LANES / 2;
        min16uint ref = min16uint ((reversebits (key) >> 1) >> 16);
        min16uint offset = 0, code = 0;
        length = BROTLIG_MAX_HUFFMAN_CODE_BITSIZE;
        bool done = false;

        [unroll(BROTLIG_MAX_HUFFMAN_CODE_BITSIZE)]
        for (uint i = 0; i < BROTLIG_MAX_HUFFMAN_CODE_BITSIZE; i++)
        {
            uint d = WaveReadLaneAt (__table, sel + i);
            min16uint newcode = min16uint (d >> SHORT_TOTAL_BITS);
            offset = done ? offset : min16uint (d & MaskLsbs (SHORT_TOTAL_BITS));
            done = ref < newcode;
            code = done ? code : newcode;
            if (WaveActiveAllTrue (done))
                break;
        }
        uint l = (uint (offset) >> DECODER_LENGTH_SHIFT) &
                            MaskLsbs (BROTLIG_MAX_HUFFMAN_CODE_BITSIZE_LOG);
        length = uint (l);
        offset = (offset & min16uint (MaskLsbs (DECODER_LENGTH_SHIFT))) +
                            ((ref - code) >> min16uint (SHORT_TOTAL_BITS - 1 - l));
        return GetSymbol (type, offset);
    };
};

static InlineDecoder SearchTables [2];

struct BaseSymbolTable
{
    HuffmanDecoder dec;

	const32_t sMaxSymbols = BROTLIG_NUM_MAX_SYMBOLS;

    uint HuffmanInflateTable (uint type, uint symbol, uint length, uint offset, uint laneIx)
	{
        uint toDo = WaveActiveBallot (length != 0).x;
        uint retv = 0;
        while (toDo)
        {
            uint lane = firstbitlow (toDo);
            uint leng = WaveReadLaneAt (length, lane);
            bool same = leng == length;
            uint mask = WaveActiveBallot (same).x;
            uint offs = countbits (mask & MaskLsbs (laneIx));

            if (same)
            {
                offset += offs;
                // retv = mask;
            }
            retv = leng == laneIx ? countbits (mask) : retv;

            toDo &= ~mask;
        }
        if (length)
            SetSymbol (type, offset, min16uint (symbol));

        return retv;
    }

    void BuildHuffmanTable (uint type, uint off, uint tSize, uint laneIx)
	{
        uint symbol, length, offset, same;

        off = WaveReadLaneAt (off, laneIx - 1);
        
        SymbolLengths lenTable;
        lenTable.Copy (0, laneIx);
        lenTable.Copy (1, laneIx);
        lenTable.Copy (2, laneIx);
        
        ClearDictionary (type, laneIx);

        symbol = laneIx;
        uint row = 0x3020;
        uint i = 0;
        for (symbol = laneIx; WaveActiveAnyTrue (symbol < tSize); symbol += NUM_LANES)
		{
            length = lenTable.LaneGet (symbol);
            length = symbol < tSize ? length : 0;

            offset = WaveReadLaneAt (off, length);

            off   += HuffmanInflateTable (type, symbol, length, offset, laneIx);
        }
	}

    min16uint Decode (min16uint type, uint key, out min16uint length, uint laneIx)
    {
        return SearchTables [type / 2].Decode (type & 1, type, key, length);
    }
};

groupshared
BaseSymbolTable gLookup;

uint FixedCodeLenCounts(uint laneIx, uint nsym, uint tree_select)
{
	const32_t length_counts[4][5] = {
									    {0, 2, 0, 0, 0},
									    {0, 1, 2, 0, 0},
									    {0, 0, 4, 0, 0},
									    {0, 1, 1, 2, 0}
	};

	uint table_index = nsym < 4 ? nsym - 2 : tree_select ? 3 : 2;
	return laneIx < nsym ? length_counts[table_index][laneIx] : 0;
}

uint ReadSymbolCodeLengths (uint type, inout DecoderState bs, uint numCodes, uint tSize)
{
    uint laneIx = bs.LANE;
	
    gLookup.dec.Reset (laneIx);
    
    //  retrieve the bitlenghts
    uint lengths = bs.GetAndDropBits (5, laneIx < numCodes);
    uint swizzle = laneIx == 0 ? 4 :
				   laneIx == 5 ? 5 :
				   laneIx == 6 ? 7 :
				   laneIx == 16 ? 8 :
				   laneIx == 17 ? 6 :
				   laneIx >= 7 ? laneIx + 2 : laneIx - 1;
    lengths = WaveReadLaneAt (lengths, swizzle);
    lengths &= laneIx < BROTLIG_LENGTH_ENCODER_SIZE ? 0xf : 0;
	
	//  build histogram
    uint hist = WaveHistogram (lengths, lengths && laneIx < BROTLIG_LENGTH_ENCODER_SIZE, laneIx);

    //  build key lookup tables
    InlineDecoder dec = { 0xfffff000 };
    
    hist = CreateCompactedHuffmanSearchTable (hist, dec.__table, laneIx);
    hist = WaveReadLaneAt (hist, lengths - 1);
    gLookup.HuffmanInflateTable (type, laneIx, lengths, hist, laneIx);

    SymbolLengthsReset (laneIx);

    uint rle    = 0;
    uint saved  = -1;
    uint offset = 0;

    for (uint ptr = 0; WaveActiveAnyTrue (ptr < tSize);
            ptr = WaveReadLaneAt (ptr + rle, NUM_LANES - 1))
    {
        uint keylen;
        uint key = bs.GetBits (BROTLIG_LENGTH_ENCODER_MAX_KEY_LENGTH + BROTLIG_LENGTH_ENCODER_MAX_EXTRA_LENGTH);

        uint symbol = dec.Decode (0, type, key, keylen);

        key >>= keylen;

        uint lastIx = ActiveIndexPrevious (symbol != BROTLIG_REPEAT_PREVIOUS_CODE_LENGTH, laneIx);
        uint repeat = lastIx == -1 ? saved : WaveReadLaneAt (symbol, lastIx);
        
        switch (symbol)
        {
            case BROTLIG_REPEAT_PREVIOUS_CODE_LENGTH:
                symbol = repeat; // >= BROTLIG_REPEAT_PREVIOUS_CODE_LENGTH ? 0 : repeat;
                rle = 3 + (key & 3);
                keylen += 2;
                break;
            case BROTLIG_REPEAT_ZERO_CODE_LENGTH:
                symbol = 0;
                rle = 3 + (key & 7);
                keylen += 3;
                break;
            default:
                rle = 1;
        }
        symbol = symbol < BROTLIG_REPEAT_PREVIOUS_CODE_LENGTH ? symbol : 0;
        
        ptr += WavePrefixSum (rle);
        
        bool ptrValid = ptr < tSize;
        
        uint _rle = 0;
        if (0 < symbol && ptrValid)
        {
            _rle = min (tSize - ptr, rle);
            HuffmalLengthTableInsert (symbol, _rle, ptr);
        }
        offset += WaveHistogram (symbol, _rle, true, laneIx);

        saved = WaveReadLaneAt (symbol, NUM_LANES - 1);

        bs.DropBits (keylen, ptr < tSize);
    }
    return offset;
}

struct SymbolTable : BaseSymbolTable
{
    void loadtrivial (uint type, inout DecoderState bs, uint tSize, uint nsym)
	{
        uint laneIx = bs.LANE;
		
        uint max_bits = firstbithigh (tSize - 1) + 1;
        uint sym = bs.GetAndDropBits (max_bits, laneIx == 0);

        uint code = laneIx == 0 ? 1u << (BROTLIG_MAX_HUFFMAN_CODE_BITSIZE - 2) :
                                  1u << (BROTLIG_MAX_HUFFMAN_CODE_BITSIZE - 1);
        
        uint _t = CompactHuffmanSearchEntry (code, 0, 1);

        if (type < 2)
            SearchTables [0].Init (_t, type & 1, laneIx);
        else
            SearchTables [1].Init (_t, type & 1, laneIx);
        
        SetSymbol (type, laneIx, min16uint (sym));
    }

    void loadsimple (uint type, inout DecoderState bs, uint tSize, uint nsym, uint tree_select)
	{
        uint laneIx = bs.LANE;
		/*
        nsym ts lengths   offsets   [wptrs]   [codes]  [-codelen-]
        2   x   1 1       0 0 0 0   0 1 2 3   f f f f  [
        3   x   1 2 2     0 1 1 1   0 1 2 3   4 f f f
        4   0   2 2 2 2   0 0 0 0   0 1 2 3   
        4   1   1 2 3 3   0 1 2 2   0 1 2 3   
        */
		uint count = FixedCodeLenCounts(laneIx, nsym, tree_select);

        uint _t = 0xfffff000;
        CreateCompactedHuffmanSearchTable (count, _t, laneIx);
        if (type < 2)
            SearchTables [0].Init (_t, type & 1, laneIx);
        else
            SearchTables [1].Init (_t, type & 1, laneIx);
        
        uint max_bits = firstbithigh (tSize - 1) + 1;
        uint sym = bs.GetAndDropBits (max_bits, laneIx < nsym);

        SetSymbol (type, laneIx, min16uint (sym));
    }

    void loadcomplex (uint type, inout DecoderState bs, uint tSize, uint numCodes)
	{
        uint laneIx = bs.LANE;
		
        uint count = ReadSymbolCodeLengths (type, bs, numCodes, tSize);
        
        uint _t = 0xfffff000;
        uint off = CreateCompactedHuffmanSearchTable (count, _t, laneIx);
        if (type < 2)
            SearchTables [0].Init (_t, type & 1, laneIx);
        else
            SearchTables [1].Init (_t, type & 1, laneIx);
        
        BuildHuffmanTable (type, off, tSize, laneIx);
        
        uint lastPtr = WaveReadLaneAt (WavePrefixSum (count), 16);

        uint space   = sMaxSymbols - lastPtr - 1;
	}

    void ReadHuffmanCode (uint type, inout DecoderState bs, uint tSize)
    {
        uint laneIx = bs.LANE;
        
        uint data = bs.GetBits ();

        BitField treeheader;
        treeheader.d = WaveReadLaneFirst (data);

        bs.DropBits (6, laneIx == 0);

        uint cSel = treeheader.bit (0, 1);
        uint nSym = treeheader.bit (2, 3);
        uint tSel = treeheader.bit (4, 4);
        uint nCom = treeheader.bit (2, 5) + 4;

        switch (treeheader.bit (0, 1))
        {
        case 0:
                loadtrivial (type, bs, tSize, nSym);
                break;
        case 1:
                loadsimple  (type, bs, tSize, nSym + 1, tSel);
                break;
        case 2:
                loadcomplex (type, bs, tSize, nCom);
                break;
        }
    }
};

struct DecoderParams
{
	min16uint max_backward_distance;
    min16uint uncompLen;
	min16uint npostfix;
    min16uint n_direct;
    bool isLast;
    bool isEmpty;
    bool isUncompressed;
};

groupshared SymbolTable luts[BROTLIG_NUM_CATEGORIES];

struct BrotligCmd
{
	min16uint icp_code;
	uint insert_len;
	uint copy_len;
	uint copy_dist;
};

struct LengthCode24
{
    uint extraHIbaseLO;
    void Init (uint bias, uint bitDelta, uint extDelta, uint laneIx)
    {
        min16uint base, extra;
        uint bitCnt  = countbits (bitDelta & MaskLsbs (laneIx));
        uint extCnt  = countbits (extDelta & MaskLsbs (laneIx));
        extra = min16uint (laneIx < 24 - 1 ? bitCnt + extCnt : 24);
        uint range   = uint (min16uint (1) << extra);
        base  = min16uint (bias + WavePrefixSum (range));
        extraHIbaseLO = uint (base) | (uint (extra) << 16);
    }
};
static LengthCode24 insertLengthCode, copyLengthCode;

struct Decoder
{
    inline min16uint decodelit (min16uint bits, out min16uint len, uint laneIx)
	{
		return luts [0].Decode (0, bits, len, laneIx);
    }

    inline min16uint decodeicp (min16uint bits, out min16uint len, uint laneIx)
	{
        return luts [1].Decode (1, bits, len, laneIx);
    }

    inline min16uint decodedis (min16uint bits, out min16uint len, uint laneIx)
	{
        return luts [2].Decode (2, bits, len, laneIx);
    }
    
    CmdLut decodelut (min16uint icp_code, bool en)
    {
        CmdLut result = { 0, 0, 0, 0, 0, 0 };
        
        en = en && (icp_code != min16uint (BROTLIG_EOS_COMMAND_SYMBOL));
        
        bool ec = en && icp_code < min16uint (BROTLIG_EOS_COMMAND_SYMBOL);
        uint ic = icp_code - min16uint (BROTLIG_EOS_COMMAND_SYMBOL);

        uint copyCode = 0, insertCode = 0;
        if (en)
        {
            min16uint copyLsbs   = min16uint (icp_code >> 0) & MaskLsbs16 (3);
            min16uint insertLsbs = min16uint (icp_code >> 3) & MaskLsbs16 (3);
        
            icp_code >>= 6;
            
            //       icp_code_msbs 10 9 8 7 6 5 4 3 2 1 0
            //       -------------+----------------------
            //  insert = 0x298500   2 2 1 2 0 1 1 0 0 0 0
            //  copy   = 0x262444   2 1 2 0 2 1 0 1 0 1 0
        
            copyCode   = BitTable (icp_code, 2, 0x262444U);
            insertCode = BitTable (icp_code, 2, 0x298500U);
            
            copyCode   = (copyCode   << 3) | copyLsbs;
            insertCode = (insertCode << 3) | insertLsbs;
            
            insertCode = ec ? insertCode : ic;
        }
        
        uint copy   = WaveReadLaneAt (copyLengthCode.extraHIbaseLO  , copyCode);
        uint insert = WaveReadLaneAt (insertLengthCode.extraHIbaseLO, insertCode);
        
        result.copy_len_extra    = ec ? (copy   >> 16) & 0xffff : 0;
        result.copy_len_offset   = ec ? (copy   >>  0) & 0xffff : 0;
        result.insert_len_extra  = en ? (insert >> 16) & 0xffff : 0;
        result.insert_len_offset = en ? (insert >>  0) & 0xffff : 0;
        
        return result;
    }

    BrotligCmd decodecommand (inout DecoderState bs, uint laneIx, bool en)
	{
		BrotligCmd cmd;

        CmdLut clut = { 0, 0, 0, 0, 0, 0 };
        min16uint len = 0;
        
        cmd.icp_code = en ? min16uint (bs.GetBits (BROTLIG_MAX_HUFFMAN_CODE_BITSIZE)) : 0;
        cmd.icp_code = decodeicp (cmd.icp_code, len, laneIx);
        cmd.icp_code = en ? cmd.icp_code : 0;
        
        uint cutoff_mask = WaveActiveBallot (cmd.icp_code == min16uint (BROTLIG_EOS_COMMAND_SYMBOL)).x;
        uint cutoff_ix   = firstbitlow (cutoff_mask);
        bool cutoff      = laneIx > cutoff_ix;
        cmd.icp_code     = cutoff ? min16uint (BROTLIG_EOS_COMMAND_SYMBOL) : cmd.icp_code;
        len              = cutoff ? 0 : len;
        
        bs.DropBits (len, en);
        
        clut = decodelut (cmd.icp_code, en);
        
        cmd.insert_len  = bs.GetAndDropBits (clut.insert_len_extra, en);
        cmd.copy_len    = bs.GetAndDropBits (clut.copy_len_extra  , en);
        cmd.insert_len += clut.insert_len_offset;
        cmd.copy_len   += clut.copy_len_offset;

		return cmd;
	}

    min16uint decodeliteral (inout DecoderState bs, bool en, uint laneIx)
	{
        min16uint lsymbol = en ? min16uint (bs.GetBits (BROTLIG_MAX_HUFFMAN_CODE_BITSIZE)) : 0;

        min16uint len = 0;
		// decode literal
        lsymbol = decodelit (lsymbol, len, laneIx);
        bs.DropBits (len, en);

		return lsymbol;
	}

    min16uint decodedistance (inout DecoderState bs, uint laneIx, bool p)
	{
		min16uint dsymbol = 0;
		min16uint len = 0;
        min16uint code = p ? min16uint (bs.GetBits (BROTLIG_MAX_HUFFMAN_CODE_BITSIZE)) : 0;
        dsymbol = decodedis (code, len, laneIx);
        bs.DropBits (len, p);

		return p ? dsymbol : 0;
	}
};

static uint64_t distringbuffer;

void InitDistRingBuffer(uint laneIx)
{
    // distringbuffer = (((((16ULL << 16) | 15ULL) << 16) | 11) << 16) | 4;
    // 0x0010000f000b0004ULL;
    distringbuffer = (((((16ULL << 16) | 15ULL) << 16) | 11) << 16) | 4;
}

void ReadHuffmanCode (inout DecoderState bs)
{
    luts [1].ReadHuffmanCode (1, bs, BROTLIG_NUM_COMMAND_SYMBOLS_WITH_SENTINEL);
    luts [2].ReadHuffmanCode (2, bs, BROTLIG_NUM_DISTANCE_SYMBOLS);
    luts [0].ReadHuffmanCode (0, bs, BROTLIG_NUM_LITERAL_SYMBOLS);
}

min16uint ResolveDistances (uint sym, inout DecoderState bs, DecoderParams dparams, uint laneIx, bool p)
{
    bool immed  = p && sym >= 16;
    bool stack  = sym <  16;
    bool fetch  = immed && (dparams.n_direct == 0 || sym >= 16 + dparams.n_direct);
    min16uint ix     = BitTable (sym, 2, 0x555000e4U);
    min16uint offset = BitTable (sym, 4, 0x7162537162534444ULL);
    min16uint dist   = immed && !fetch ? sym - 15 : p ? min16uint (distringbuffer) : 0;

    bool update = sym != 0;
    if (fetch)
    {
        min16uint param = sym - dparams.n_direct - 16;
        min16uint hcode = param >> dparams.npostfix;
        min16uint lcode = MaskLsbs (param, dparams.npostfix);

        min16uint ndistbits = 1 + (hcode >> 1);

        min16uint extra = min16uint (bs.GetAndDropBits (ndistbits, true));

			
        min16uint offset = ((2 + (hcode & 1)) << ndistbits) - 4;
        dist = ((offset + extra) << dparams.npostfix) + lcode + dparams.n_direct + 1;
    }
    uint umask = WaveActiveBallot (update).x;
    uint smask = WaveActiveBallot (stack ).x;
    
    uint mask  = WaveActiveBallot (p & update).x;
    while (mask)
    {
        uint lane = firstbitlow (mask);
        
        uint lmsk = 1u << lane;
        mask &= ~lmsk;
        
        min16uint idx = WaveReadLaneAt (ix    , lane);
        min16uint off = WaveReadLaneAt (offset, lane);
        min16uint tmp = WaveReadLaneAt (dist  , lane);
        
        bool takefromstack = (lane == laneIx && stack) |
            (lane < laneIx && !update);
        
        min16uint reg = min16uint (distringbuffer >> (idx * 16));
        
        reg += off - 4;
        reg  = smask & lmsk  ? reg : tmp;
        
        dist = takefromstack ? reg : dist;
        
        min16uint2 s = umask & lmsk ? min16uint2 (reg, 16) : 0;

        distringbuffer = (distringbuffer << s.y) | s.x;
    }
	return dist;
}

uint dic2bit12 (min16uint a) { return a; /* uint ((a & 0xff) | ((a & 0xf000) >> 4)); */ }
uint shiftbytes (uint a, uint b, uint sh)
{
    return uint (((uint64_t (b) << 32) | uint64_t (a)) >> (sh << BYTE_TOTAL_BITS));
}
uint shiftbytes (uint2 a, uint sh) { return shiftbytes (a.x, a.y, sh); }

groupshared uint scoreBoard [LOG_NUM_LANES];
uint SpreadValue (uint v [LOG_NUM_LANES], uint mask, uint sum, bool en, uint laneIx)
{
    uint result = 0;
    int  i;

    if (laneIx < LOG_NUM_LANES)
        scoreBoard [laneIx] = 0;
    [unroll]
    for (i = 0; i < LOG_NUM_LANES; i++)
        if (en)
            InterlockedOr (scoreBoard [i], (v [i] & mask) << sum);
    [unroll]
    for (i = LOG_NUM_LANES - 1; i >= 0; i--)
        result |= ((scoreBoard [i] >> laneIx) & 1) << i;

    return result;
}

uint SpreadLiterals (inout uint sum, inout uint numLit, inout uint offset, out uint writeoff, uint laneIx)
{
    uint mask = numLit >= DWORD_TOTAL_BITS ? -1 : MaskLsbs (numLit);
    uint cond = numLit && sum < NUM_LANES;
    
    uint lanes [LOG_NUM_LANES];
    [unroll]
    for (uint l = laneIx, i = 0; i < LOG_NUM_LANES; i++, l >>= 1)
        lanes [i] = -(l & 1);
    uint srcIdx = SpreadValue (lanes, mask, sum, cond, laneIx);
    writeoff = WaveReadLaneAt (offset, srcIdx);

    uint count [] = { 0xaaaaaaaa, 0xcccccccc, 0xf0f0f0f0, 0xff00ff00, 0xffff0000 };
    uint relIdx = SpreadValue (count, mask, sum, cond, laneIx);
    
    uint lastOff  = min (NUM_LANES, numLit + sum);
    uint consumed = cond ? lastOff - sum : 0;
    uint lastIx   = firstbithigh (cond);
    uint written  = WaveReadLaneAt (lastOff, NUM_LANES - 1);
    
    numLit -= consumed;
    offset += consumed;
    sum    -= min (sum, NUM_LANES);

    writeoff += relIdx;

    return written;
}

static uint s_literalBuffer;
static uint s_literalsKept = 0;

#define BROTLIG_DEBUG_DECODE_DUMP_CMDS_DEBUG_PTR 0xfc8e

void WriteLiterals (inout DecoderState d, uniform const uint wptr, uint numLit, uint offset)
{
    const uint laneIx = d.LANE;
    Decoder dec;
    
    uint endOffset = (NUM_LANES - s_literalsKept) % NUM_LANES;
    uint startIx = WavePrefixSum (numLit) + endOffset;
    uint written, writeoff;

    while (WaveActiveAnyTrue (numLit != 0))
    {
        if (s_literalsKept == 0)
            s_literalBuffer = dec.decodeliteral (d, true, laneIx);

        written = SpreadLiterals (startIx, numLit, offset, writeoff, laneIx);

        bool mask = laneIx >= endOffset && laneIx < written;
        endOffset = written % NUM_LANES;
        s_literalsKept = WaveReadLaneFirst ((NUM_LANES - endOffset) % NUM_LANES);

        if (mask)
            ByteStore (wptr + writeoff, s_literalBuffer);
    }
}

uint Decompress (inout DecoderState d, uint wptr, DecoderParams dParams, uint outlimit)
{
    uint laneIx = d.LANE;
	
    Decoder dec;
	
	InitDistRingBuffer(laneIx);

	GroupMemoryBarrierWithGroupSync();
    
    uint neos = true;
    BrotligCmd cmd;
    
	cmd = dec.decodecommand(d, laneIx, neos);

	uint length = cmd.copy_len + cmd.insert_len;
	uint offset = 0;
	    
    neos = length > 0;
	    
    bool neob = WaveActiveAnyTrue (neos);
    
    uint literalsRead = 0;
    uint byte;
        
    while (WaveActiveAllTrue (wptr < outlimit))
    {
        offset = WavePrefixSum (length);

        uint numlits = cmd.insert_len;
        uint writeptr = wptr + offset;

        uint literalsTotal = WaveActiveSum (numlits);
        uint literalsFetch = max (literalsTotal, literalsRead) - literalsRead;
        literalsFetch = DIV_ROUND_UP (literalsFetch, NUM_LANES);
        literalsRead  = (literalsRead - literalsTotal) % NUM_LANES;
        
        bool decode_en = (cmd.icp_code >= 128) && neos &&
                         (cmd.icp_code < BROTLIG_EOS_COMMAND_SYMBOL);
        
        cmd.copy_dist = dec.decodedistance (d, laneIx, decode_en);
        cmd.copy_dist = ResolveDistances (cmd.copy_dist, d, dParams, laneIx, neos);
        
        WriteLiterals (d, wptr, numlits, offset);
        
        offset   += numlits;
        writeptr += numlits;
        
        wptr += WaveReadLaneAt (offset + uint (cmd.copy_len), NUM_LANES - 1);

        cmd.copy_len = min (cmd.copy_len, max (outlimit, writeptr) - writeptr);
        
        uint mask = WaveActiveBallot (neos).x;
        for (uint i = 0; i < NUM_LANES; i++)
        {
            if (!mask & (1u << i))
                continue;

            uint offset = WaveReadLaneAt (cmd.copy_dist, i);
            uint length = WaveReadLaneAt (cmd.copy_len, i);
            uint outptr = WaveReadLaneAt (writeptr, i);
            
            uint source = outptr - offset;
            uint target = outptr;

                for (uint j = laneIx; j < length; j += NUM_LANES)
                {
                    uint data = ByteLoad (source + j % offset);
                    ByteStore (j + target, data);
                }
        }

        if (WaveActiveAnyTrue (length == 0))
        {
            break;
        }
        cmd = dec.decodecommand (d, laneIx, neos);

        length = cmd.copy_len + cmd.insert_len;
        neos = length > 0;
        neob = WaveActiveAnyTrue (neos);
    }
    return wptr;
}

uint CopyRawData (uint src, uint dst, uint size, uint laneIx)
{
    uint rptr = src;
    uint wptr = dst + laneIx * DWORD_TOTAL_BYTES;
    for (uint i = laneIx * DWORD_TOTAL_BYTES; WaveActiveAnyTrue (i < size); i += NUM_LANES * DWORD_TOTAL_BYTES)
    {
        uint data = input.Load (rptr);
        uint byte [4];
        uint abcd = (laneIx / (NUM_LANES / DWORD_TOTAL_BYTES)) * BYTE_TOTAL_BITS;
        uint ln   = (laneIx * DWORD_TOTAL_BYTES) % NUM_LANES;
        byte [0] = (WaveReadLaneAt (data, ln + 0) >> abcd) & 0xff;
        byte [1] = (WaveReadLaneAt (data, ln + 1) >> abcd) & 0xff;
        byte [2] = (WaveReadLaneAt (data, ln + 2) >> abcd) & 0xff;
        byte [3] = (WaveReadLaneAt (data, ln + 3) >> abcd) & 0xff;
        data = (((((byte [3] << 8) | byte [2]) << 8) | byte [1]) << 8) | byte [0];
        
        if (wptr < dst + size)
            output.Store (wptr, data);
        rptr += DWORD_TOTAL_BYTES;
        wptr += NUM_LANES * DWORD_TOTAL_BYTES;
    }
	return wptr;
}

bool UncompressedMemCopy (uint source, uint destination, uint inputSize, uint outputSize, uint laneIx)
{
    if (inputSize != outputSize)
        return false;
    
    uint rows = inputSize / DWORD_TOTAL_BYTES / NUM_LANES / 4;
    uint i;

    for (i = 0; i < rows; i++)
        output.Store4 (destination + (i * NUM_LANES + laneIx) * 4 * DWORD_TOTAL_BYTES,
            input.Load4 (source + (i * NUM_LANES + laneIx) * 4 * DWORD_TOTAL_BYTES));

    i = rows * NUM_LANES * 4 * DWORD_TOTAL_BYTES + laneIx * DWORD_TOTAL_BYTES;
    for (; i < inputSize; i += NUM_LANES * DWORD_TOTAL_BYTES)
        output.Store (destination + i, input.Load (source + i));

    return true;
}

void DeserializeCompact (inout DecoderState bs, uint iSize)
{
    uint laneIx = bs.LANE;
    uint temp   = bs.GetBits () << 6;
    
    uint avgBsSize     = DivRoundUp (iSize, NUM_LANES);
    uint baseSizeBits  = GetBitSize (avgBsSize + 1);
    
    uint realSizeBits  = GetBitSize (iSize);
    uint deltaLogBits  = GetBitSize (realSizeBits + 1);
    
    uint baseSize      = bs.GetAndDropBits (baseSizeBits, true);
    uint deltaSizeBits = bs.GetAndDropBits (deltaLogBits, true);
    
    baseSize      = WaveReadLaneFirst (baseSize);
    deltaSizeBits = WaveReadLaneFirst (deltaSizeBits);

    uint delta = 0;
    for (uint i = 0; i < NUM_LANES; i++)
    {
        uint bits = bs.GetAndDropBits (deltaSizeBits, true);
        delta = i == laneIx ? bits : delta;
    }

    delta += baseSize;

    uint offset = WavePrefixSum (delta);
    
    uint readPtr = bs.readPointer - (bs.validBits / NUM_LANES) * DWORD_TOTAL_BYTES;

    bs.Init (readPtr + (offset / DWORD_TOTAL_BYTES) * DWORD_TOTAL_BYTES);
    bs.DropBits ((offset % DWORD_TOTAL_BYTES) * BYTE_TOTAL_BITS, true);
}

void Process (uint source, uint destination, uint inputSize, uint outputSize, uint laneIx)
{
    if (UncompressedMemCopy (source, destination, inputSize, outputSize, laneIx))
        return;

    uint fill = 0;

    for (uint i = laneIx; i < (outputSize + 3) / 4; i += NUM_LANES)
        output.Store (destination + i * 4, fill);
    for (uint j = laneIx; j < DICTIONARY_COMPACT_TOTAL_SIZE; j += NUM_LANES)
        gDictionary [j] = 0;
    s_literalsKept = 0;
    
    copyLengthCode.Init   (2, 0x3eaa80, 0x000000, laneIx);
    insertLengthCode.Init (0, 0x3eaa80, 0x310020, laneIx);
    
    DecoderParams dparams = { 0, 0, 0, 0, 0, 0, 0 };
    
    DecoderState bs;
    bs.Init (source);
    
    dparams.npostfix = bs.GetAndDropBits (2, true);
    dparams.n_direct = bs.GetAndDropBits (4, true);
    bs.DropBits(2, true);
    
    dparams.n_direct <<= dparams.npostfix;
    
    dparams.npostfix = WaveReadLaneFirst (dparams.npostfix);
    dparams.n_direct = WaveReadLaneFirst (dparams.n_direct);
    
    DeserializeCompact (bs, inputSize);

    ReadHuffmanCode (bs);

    destination = Decompress (bs, destination, dparams, destination + outputSize);
}

struct WorkParams
{
    uint index, rptr, wptr, rsize, wsize;
};

//  stream/page descriptor as per benchmark app API
[numthreads(NUM_LANES, 1, 1)]
[RootSignature (_RootSig)]
void CSMain(uint laneIx : SV_GroupThreadID)
{
    WorkParams stream = { 0, 0, 0, 0, 0 };

	if (laneIx == 0)
        stream.index = meta.Load (0);
    stream.index = WaveReadLaneFirst (stream.index);

	[allow_uav_condition]
    while (stream.index > 0)
	{
        if (laneIx < 2)
        {
#if USE_METACOMMAND_INTERFACE
            stream.rptr = meta.Load(4 + (stream.index - 1) * 8 + laneIx * 4);
#else
            stream.rptr = meta.Load(4 + (stream.index - 1) * 12 + laneIx * 4);
#endif
        }
        stream.wptr = WaveReadLaneAt (stream.rptr, 1);
        stream.rptr = WaveReadLaneAt (stream.rptr, 0);
		
        uint lastPageRsize;
        if (laneIx < 3)
            lastPageRsize = input.Load (stream.rptr + laneIx * (BROTLIG_WORK_STREAM_HEADER_SIZE - 4));

        BitField readControlWord1, readControlWord2;
        readControlWord1.d = WaveReadLaneAt (lastPageRsize, 0);
        readControlWord2.d = WaveReadLaneAt (lastPageRsize, 1);
        lastPageRsize      = WaveReadLaneAt (lastPageRsize, 2);

        const uint numPages = readControlWord1.bit (16, 31);

		[allow_uav_condition]
		while (true)
		{
            WorkParams page = { -1, 0, 0, 0, 0 };

            const uint pageDesc = BROTLIG_WORK_STREAM_HEADER_SIZE + stream.rptr;
            const uint pageSize = BROTLIG_WORK_PAGE_SIZE_UNIT <<
                                                        readControlWord2.bit (0, 1);

			if (laneIx == 0)
            {
#if USE_METACOMMAND_INTERFACE
                scratch.InterlockedAdd((stream.index - 1) * 4, 1u, page.index);
#else
                meta.InterlockedAdd(12u + (stream.index - 1) * 12, 1u, page.index);
#endif
            }
            page.index = WaveReadLaneFirst (page.index);

            if (page.index >= numPages)
				break;

            uint readSizeIndex;
            if (laneIx < 2)
                readSizeIndex = input.Load (pageDesc + page.index * 4 + laneIx * 4);
            
            page.rptr  = page.index > 0 ? WaveReadLaneFirst (readSizeIndex) : 0;
            page.rsize = page.index == numPages - 1 ? lastPageRsize : WaveReadLaneAt (readSizeIndex, 1) - page.rptr;

            uint lastPageSize = readControlWord2.bit (2, 19);
            // lastPageSize = lastPageSize > 0 ? lastPageSize : pageSize;
			
            page.wptr  = stream.wptr + page.index * pageSize;
            page.wsize = page.index < numPages - 1 || !lastPageSize ? pageSize : lastPageSize;
            page.rptr += pageDesc + numPages * 4;
			
            Process (page.rptr, page.wptr, page.rsize, page.wsize, laneIx);
        }

		if (laneIx == 0)
		{
			int actualIndex;
            //  because other wave could already have it decremented
            meta.InterlockedCompareExchange (0, stream.index, stream.index - 1, actualIndex);

            stream.index = actualIndex == stream.index ? 
                stream.index - 1 : actualIndex;
        }
        stream.index = WaveReadLaneFirst (stream.index);
    }
}