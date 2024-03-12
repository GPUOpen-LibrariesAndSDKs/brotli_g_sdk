# Brotli-G SDK 1.1
# 
# Copyright(c) 2022 - 2024 Advanced Micro Devices, Inc. All rights reserved.
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files(the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions :
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

 cmake_minimum_required(VERSION 3.11)
 
 set(CMAKE_SUPPRESS_REGENERATION OFF) # allow cmakelist.txt in project view 

 project(brotlig VERSION 1.1.0)
   
 set( PROJECT_VERSION_MAJOR 1 )
 set( PROJECT_VERSION_MINOR 1 )
 set( PROJECT_VERSION_PATCH 0 )
 
 set(CMAKE_CXX_STANDARD 17) 

 option(OPTION_BUILD_SAMPLE  "Build BrotliG sample application" OFF)
 option(OPTION_SAMPLE_INCLUDE_BROTLI  "Include Brotli in sample application" OFF)
 option(OPTION_SAMPLE_ENABLE_GPU  "Enable GPU decompression in sample application" OFF)
 option(OPTION_SAMPLE_ENABLE_AGS  "Enable AGS in GPU decompression sample" OFF)
 option(OPTION_BUILD_TEST	"Build BrotliG test application" OFF)

 # generate the output binary in the /bin directory
 set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

 # Configure externals
 set_property(GLOBAL PROPERTY USE_FOLDERS ON)
 include(external/cmake/CMakeLists.txt)
 include_directories("external/brotli/c/include")
 include_directories("external")
 list(APPEND DEPS brotli)
  
 file(GLOB COMMON  src/common/*.cpp src/DataStream.cpp inc/common/*.h inc/DataStream.h)
 file(GLOB ENCODER src/encoder/*.cpp src/BrotligEncoder.cpp inc/encoder/*.h inc/BrotligEncoder.h)
 file(GLOB DECODER src/decoder/*.cpp src/BrotligDecoder.cpp inc/decoder/*.h inc/BrotligDecoder.h)


 # Brotli-G Lib
 add_library(brotlig STATIC)

 target_compile_features(brotlig PUBLIC cxx_std_17)

 include_directories("inc")
 include_directories("inc/common")
 include_directories("inc/decoder")
 include_directories("inc/encoder")

 source_group("Common"   FILES ${COMMON}   )
 source_group("Decoder"  FILES ${DECODER}  )
 source_group("Encoder"  FILES ${ENCODER}  )


 target_sources(brotlig PRIVATE
		inc/BrotliG.h
		${COMMON}
		${ENCODER}   
		${DECODER}
		)

 target_include_directories(brotlig INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
	)
	
 target_include_directories(brotlig INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}/external
	)
	
 target_include_directories(brotlig INTERFACE
	${CMAKE_CURRENT_SOURCE_DIR}/external/brotli/c/include
	)
	
 target_link_libraries( brotlig ${DEPS} )
 
 list(APPEND DEPS brotlig)
 
  # Brotli-G Sample Application
  if (OPTION_BUILD_SAMPLE) 
	add_subdirectory(sample)
  endif()