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
 
 message(STATUS "++++++++++++++++++ external CMAKE subfolder" )
 
 #BrotliG Dependencies
 
 project (brotlig)
 
 if (NOT DEFINED BROTLI_PROJECT_DIR)
    get_filename_component(BROTLI_PROJECT_DIR ${CMAKE_CURRENT_LIST_DIR}/../../ ABSOLUTE)
endif ()

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/customtargets)

# Write a generic c++ source file to compile
file(
    WRITE ${CMAKE_CURRENT_BINARY_DIR}/external.cpp
    "// Dummy source file
#include <stdio.h>

int main()
{
    printf(\"Brotli-G Dependencies\\n\");

    return 0;
}
"
)