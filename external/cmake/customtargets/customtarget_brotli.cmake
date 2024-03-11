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

# external CMakeLists.txt

 cmake_minimum_required(VERSION 3.11)

 get_filename_component(BROTLI_DIR ${BROTLI_PROJECT_DIR}/external/brotli ABSOLUTE)

 project(brotli C)
 
 file(GLOB COMMON ${BROTLI_DIR}/c/common/*.c ${BROTLI_DIR}/c/common/*.h ${BROTLI_DIR}/c/include/brotli/port.h ${BROTLI_DIR}/c/include/brotli/types.h) 
 file(GLOB ENCODER ${BROTLI_DIR}/c/enc/*.c ${BROTLI_DIR}/c/enc/*.h ${BROTLI_DIR}/c/include/brotli/encode.h)
 file(GLOB DECODER ${BROTLI_DIR}/c/dec/*.c ${BROTLI_DIR}/c/dec/*.h ${BROTLI_DIR}/c/include/brotli/decoder.h)
 
 add_library(brotli STATIC)
 
 target_compile_features(brotli PUBLIC)
 
 include_directories("${BROTLI_DIR}/c/include")
 include_directories("${BROTLI_DIR}/c/common")
 include_directories("${BROTLI_DIR}/c/enc")
 include_directories("${BROTLI_DIR}/c/dec")
 
 source_group("Common"   FILES ${COMMON}   )
 source_group("Decoder"  FILES ${DECODER}  )
 source_group("Encoder"  FILES ${ENCODER}  )

 target_sources(brotli PRIVATE
		${COMMON}
		${ENCODER}   
		${DECODER}
		)
		
 list(APPEND INCLUDE_PATHS "${BROTLI_DIR}/c/include")
 list(APPEND INCLUDE_PATHS "${BROTLI_DIR}/c/common")
 list(APPEND INCLUDE_PATHS "${BROTLI_DIR}/c/include")
 list(APPEND INCLUDE_PATHS "${BROTLI_DIR}/c/include") 
		
 target_include_directories(brotli INTERFACE
  "$<BUILD_INTERFACE:${includePath}>"
 )