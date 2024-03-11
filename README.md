# BrotliG-SDK
This project contains the specifications and reference implementations for the Brotli-G compression format: 
 
[Brotli-G Bitstream Spec](docs/Brotli_G_Bitstream_Format.pdf)

The use of this technical documentation is goverened by the [License](LICENSE.txt).

# Source

* `src/decoder/BrotliGCompute.hlsl` - start here to read the HLSL source of the GPU decompressor
* `src/decoder/PageDecoder.cpp`     - start here to read the source of the CPU decompressor
* `src/BrotligDecoder.cpp`          - this contains the code driving the CPU decompressor
* `src/encoder/PageEncoder.cpp`     - start here to read the source of the compressor
* `src/BrotligEncoder.cpp`          - entry point for the compressor


## Getting Started

The repo uses CMake build system. Install CMake >= 3.11 if not installed.

## Building

Run "Build.bat" from a Visual Studio Developer Command Prompt to generate build files. Open the build files in compiler of choice and compile. Outputs are static libraries `build\external\brotli\Release\brotli.lib` and `build\Release\brotlig.lib`.

## Usage

To use in a visual studio project, include `inc` `external` and `external\brotli\c\include` as include directories and link `build\external\brotli\Release\brotli.lib` and `build\Release\brotlig.lib` as external libraries.
 
Relevant header files:
* `inc/BrotligEncoder.h` - function declarations for BrotliG compressor 
* `inc/BrotligDecoder.h` - function declarations for BrotliG CPU decompressor
* `inc/BrotliG.h`        - BroltiG API header file for both compressor and CPU decompressor
* `inc/DataStream.h`     - data structures for BrotliG datastream

Code examples:

```
// Compression
void Compress(size_t srcSize, uint8_t* src, size_t& dstSize, uint8_t*& dst, BrotligDataconditionParams dcParams)
{
    dstSize = BrotliG::MaxCompressedSize(srcSize);
    dst = new uint8_t[dstSize];
	
    size_t actualSize = 0;
	
    BrotliG::Encode(
			srcSize, 		// input size (bytes) 
			src, 			// input data
			&actualSize,		// actual compressed size (bytes) 
			dst, 			// compressed output
			65536,			// page size (bytes)
			dcParams,		// data pre-conditioning parameters
			nullptr			// handle to an application defined progress function
		);
	
    dstSize = actualSize;
}
```
```
// CPU Decompression
void DecompressCPU(size_t srcSize, uint8_t* src, size_t& dstSize, uint8_t*& dst)
{
   dstSize = BrotliG::DecompressedSize(src);
   dst = new uint8_t[dstSize];
   
   size_t actualSize = 0;
	
   BrotliG::DecodeCPU(
			srcSize, 		// compressed size (bytes) 
			src, 			// compressed data
			&actualSize,		// decompressed size (bytes) 
			dst, 			// decompressed output
			nullptr			// handle to an application defined progress function
		);
		
   dstSize = actualSize;
}
```

Example root signature for BrotliGCompute.hlsl:

```
CD3DX12_ROOT_PARAMETER1 rootParameters[RootParametersCount];
rootParameters[RootSRVInput].InitAsShaderResourceView(0);	// Compressed data buffer (SRV input)
rootParameters[RootUAVControl].InitAsUnorderedAccessView(0);	// Compressed data control buffer (UAV input)
rootParameters[RootUAVOutput].InitAsUnorderedAccessView(1);	// Decompressed data buffer (UAV output)
```
## Sample

Source code of a sample demostrating the usage of BrotliG APIs is provided in the `sample` directory.`Build.bat` builds the sample by default. 

Sample build output `bin\Release\brotlig.exe`. 

Using the sample:
* BrotliG cpu compression:    `brotlig.exe <filepath>`
* BrotliG cpu deccompression: `brotlig.exe <filepath>.brotlig`
* BrotliG gpu decompression:  `brotlig.exe -gpu <filepath>.brotlig`
* Help:                       `brotlig.exe` 
