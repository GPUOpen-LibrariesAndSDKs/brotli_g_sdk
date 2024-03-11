# Brotli-G SDK 1.1 Sample
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

link_directories(
    ${CMAKE_BINARY_DIR}/lib         # link in static libs 
    )

add_executable(brotlig_cli)

if (OPTION_SAMPLE_INCLUDE_BROTLI)
add_compile_definitions(USE_BROTLI_CODEC)
endif()

if (OPTION_SAMPLE_ENABLE_GPU)
add_compile_definitions(USE_GPU_DECOMPRESSION)
endif()

target_sources(brotlig_cli
    PRIVATE
			BrotligGPUDecoder.h
			BrotligGPUDecoder.cpp
            brotlig_cli.cpp
)

# Mimimize this to just a single include path
target_include_directories(brotlig_cli PUBLIC
    ./
    ${PROJECT_SOURCE_DIR}/external/brotli/c/include
    ${PROJECT_SOURCE_DIR}/inc
    ${PROJECT_SOURCE_DIR}/inc/common
    ${PROJECT_SOURCE_DIR}/inc/decoder
    ${PROJECT_SOURCE_DIR}/inc/encoder
	${PROJECT_SOURCE_DIR}/sample/external/dxc_2021_12_08/inc
	${PROJECT_SOURCE_DIR}/sample/external/DirectX-Headers/include/directx
)

target_link_directories( brotlig_cli PRIVATE 
"${PROJECT_SOURCE_DIR}/sample/external/dxc_2021_12_08/lib/x64" 
)

target_link_libraries(brotlig_cli
    PRIVATE
	dxgi.lib
	d3d12.lib
	d3dcompiler.lib
	dxguid.lib
	dxcompiler.lib
    ${DEPS}
)

set_target_properties(brotlig_cli
     PROPERTIES 
     OUTPUT_NAME "brotlig"
     VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$(Configuration)"
    )
	
add_custom_command(TARGET brotlig_cli POST_BUILD
COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/src/decoder/BrotliGCompute.hlsl" $<TARGET_FILE_DIR:brotlig_cli>
COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/sample/external/dxc_2021_12_08/bin/x64/dxil.dll" $<TARGET_FILE_DIR:brotlig_cli>
COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/sample/external/dxc_2021_12_08/bin/x64/dxcompiler.dll" $<TARGET_FILE_DIR:brotlig_cli>
)

