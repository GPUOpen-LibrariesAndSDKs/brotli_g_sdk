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
#include <wrl.h>
#include <dxgi1_6.h>
#include <dxcapi.h>
#include <d3dx12.h>

#include "DataStream.h"

const wchar_t* shaderfile = L"BrotliGCompute.hlsl";
const wchar_t* shaderfileOther = L"../../src/decoder/BrotliGCompute.hlsl";

using Microsoft::WRL::ComPtr;

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        char error_str[64] = {};
        sprintf_s(error_str, "Failed with error 0x%08X.", static_cast<UINT>(hr));
        throw std::exception(error_str);
    }
}

// Assign a name to the object to aid with debugging.
#if defined(_DEBUG) || defined(DBG)
inline void SetName(ID3D12Object* pObject, LPCWSTR name)
{
    pObject->SetName(name);
}
#else
inline void SetName(ID3D12Object*, LPCWSTR)
{
}
#endif

static LPCWSTR GetShaderModelName(D3D_SHADER_MODEL model)
{
    switch (model)
    {
    case D3D_SHADER_MODEL_6_0: return L"cs_6_0";
    case D3D_SHADER_MODEL_6_1: return L"cs_6_1";
    case D3D_SHADER_MODEL_6_2: return L"cs_6_2";
    case D3D_SHADER_MODEL_6_3: return L"cs_6_3";
    case D3D_SHADER_MODEL_6_4: return L"cs_6_4";
    case D3D_SHADER_MODEL_6_5: return L"cs_6_5";
    default:
        throw std::exception("Shader model not supported.");
    }
}

static char* GetShaderModelString(D3D_SHADER_MODEL model)
{
    switch (model)
    {
    case D3D_SHADER_MODEL_6_0: return "6.0";
    case D3D_SHADER_MODEL_6_1: return "6.1";
    case D3D_SHADER_MODEL_6_2: return "6.2";
    case D3D_SHADER_MODEL_6_3: return "6.3";
    case D3D_SHADER_MODEL_6_4: return "6.4";
    case D3D_SHADER_MODEL_6_5: return "6.5";
    default:
        throw std::exception("Invalid shader model.");
    }
}

// Compiles hlsl shader file
static void CompileShaderFromFile(
    ID3D12Device* device,
    const wchar_t* shadeFile,
    const wchar_t* shaderFileAlternate,
    const wchar_t* entryPoint,
    LPCWSTR* args,
    uint32_t numArgs,
    DxcDefine* defines,
    uint32_t numDefines,
    D3D_SHADER_MODEL shaderModel,
    IDxcBlob** compiledShader)
{
    ComPtr<IDxcLibrary> library;
    ThrowIfFailed(DxcCreateInstance(CLSID_DxcLibrary, IID_PPV_ARGS(&library)));

    UINT32 codePage = CP_UTF8;
    ComPtr<IDxcBlobEncoding> source;
    if (FAILED(library->CreateBlobFromFile(shadeFile, &codePage, &source)))
    {
        if (FAILED(library->CreateBlobFromFile(shaderFileAlternate, &codePage, &source)))
        {
            char error_str[1024];
            char cShaderFile[21];
            char cAltShaderFile[39];
            size_t charsConverted = 0;
            wcstombs_s(&charsConverted, cShaderFile, shadeFile, 20);
            wcstombs_s(&charsConverted, cAltShaderFile, shaderFileAlternate, 38);
            sprintf_s(error_str, "%s not found. %s also not found.", cShaderFile, cAltShaderFile);
            throw std::exception(error_str);
        }
    }

    ComPtr<IDxcCompiler2> compiler;
    ThrowIfFailed(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler)));

    ComPtr<IDxcIncludeHandler> incHandler;
    ThrowIfFailed(library->CreateIncludeHandler(&incHandler));

    // fetch the supported shader model to configure the compiler
    D3D12_FEATURE_DATA_SHADER_MODEL model{ shaderModel };
    ThrowIfFailed(device->CheckFeatureSupport(
        D3D12_FEATURE_SHADER_MODEL,
        &model,
        sizeof(model)
    ));

    ComPtr<IDxcOperationResult> opResult;
    if (FAILED(compiler->Compile(
        source.Get(),
        shadeFile,
        entryPoint,
        GetShaderModelName(model.HighestShaderModel),
        args,
        numArgs,
        defines,
        numDefines,
        incHandler.Get(),
        &opResult
    )))
    {
        ComPtr<IDxcBlobEncoding> error;
        if (SUCCEEDED(opResult->GetErrorBuffer(&error)) && error)
        {
            char error_str[1024];
            sprintf_s(error_str, "Shader failed to compile with error : % s", (const char*)error->GetBufferPointer());
            throw std::exception(error_str);
        }
        else
        {
            throw std::exception("Shader failed to compile.");
        }
    }

    ComPtr<IDxcBlob> result;
    ThrowIfFailed(opResult->GetResult(&result));
    *compiledShader = result.Detach();
}

void CreateBuffer(
    ID3D12Device* device,
    D3D12_HEAP_TYPE heapType,
    D3D12_HEAP_FLAGS heapFlag,
    UINT64 bufferSize,
    D3D12_RESOURCE_FLAGS resFlag,
    D3D12_RESOURCE_STATES resState,
    ID3D12Resource** resource,
    LPCWSTR name)
{
    ComPtr<ID3D12Resource> newResource;
    *resource = nullptr;

    D3D12_HEAP_PROPERTIES heapProp = CD3DX12_HEAP_PROPERTIES(heapType);
    D3D12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(bufferSize, resFlag);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProp,
        heapFlag,
        &resDesc,
        resState,
        nullptr,
        IID_PPV_ARGS(&newResource)
    ));
    SetName(newResource.Get(), name);

    *resource = newResource.Detach();
}

void BarrierCopy(
    ID3D12GraphicsCommandList* commandList,
    ID3D12Resource* src,
    UINT64 srcOffset,
    D3D12_RESOURCE_STATES srcBeforeState,
    D3D12_RESOURCE_STATES srcCopyState,
    D3D12_RESOURCE_STATES srcAfterState,
    ID3D12Resource* dest,
    UINT64 destOffset,
    D3D12_RESOURCE_STATES destBeforeState,
    D3D12_RESOURCE_STATES destCopyState,
    D3D12_RESOURCE_STATES destAfterState,
    UINT64 numBytes)
{
    std::vector<D3D12_RESOURCE_BARRIER> barriers;

    if (srcBeforeState != srcCopyState)
    {
        barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            src,
            srcBeforeState,
            srcCopyState
        ));
    }

    if (destBeforeState != destCopyState)
    {
        barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            dest,
            destBeforeState,
            destCopyState
        ));
    }

    if (barriers.size() > 0)
    {
        commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        barriers.clear();
    }

    commandList->CopyBufferRegion(dest, destOffset, src, srcOffset, numBytes);

    if (srcCopyState != srcAfterState)
    {
        barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            src,
            srcCopyState,
            srcAfterState
        ));
    }

    if (destCopyState != destAfterState)
    {
        barriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
            dest,
            destCopyState,
            destAfterState
        ));
    }

    if (barriers.size() > 0)
    {
        commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        barriers.clear();
    }
}

BROTLIG_ERROR DecodeGPU(
    bool useWarpDevice,
    uint32_t input_size, 
    const uint8_t* input, 
    uint32_t* output_size, 
    uint8_t* output, 
    double& time)
{
    const BrotliG::StreamHeader* sHeader = reinterpret_cast<const BrotliG::StreamHeader*>(input);
    bool isPreconditioned = sHeader->IsPreconditioned();

    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12GraphicsCommandList> commandList;

    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    uint64_t fenceValue = 0;

    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> pipelineStateObject;

    ComPtr<ID3D12QueryHeap> queryHeap;
    ComPtr<ID3D12Resource> queryReadbackBuffer;

    ComPtr<ID3D12Resource> uploadBuffer;
    ComPtr<ID3D12Resource> inputBuffer;
    ComPtr<ID3D12Resource> outputBuffer;
    ComPtr<ID3D12Resource> metaBuffer;
    ComPtr<ID3D12Resource> readbackBuffer;

    enum RootParameters : uint32_t
    {
        RootSRVInput = 0,
        RootUAVMeta,
        RootUAVOutput,
        RootParametersCount
    };

    /**Create Device**/
    {
        UINT dxgiFactoryFlags = 0;

        // Intialize the D3D12 debug layer if in debug mode
#if defined(_DEBUG)

        ComPtr<ID3D12Debug1> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
#endif

        ComPtr<IDXGIFactory4> factory;
        ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

        if (useWarpDevice)
        {
            // Use sorftware adapter
            ComPtr<IDXGIAdapter> warpAdapter;
            ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

            ThrowIfFailed(D3D12CreateDevice(
                warpAdapter.Get(),
                D3D_FEATURE_LEVEL_12_0,
                IID_PPV_ARGS(&device)
            ));
            SetName(device.Get(), L"device");

            DXGI_ADAPTER_DESC desc;
            warpAdapter->GetDesc(&desc);

            wprintf(L"Using: %s\n", desc.Description);
        }
        else
        {
            // Use hardware adapter
            ComPtr<IDXGIAdapter1> hardwareAdapter;

            ComPtr<IDXGIFactory6> dxgifactory6;
            if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&dxgifactory6))))
            {
                ThrowIfFailed(dxgifactory6->EnumAdapterByGpuPreference(
                    0,
                    DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                    IID_PPV_ARGS(&hardwareAdapter)
                ));
            }
            else
            {
                for (UINT index = 0; factory->EnumAdapters1(index, &hardwareAdapter) != DXGI_ERROR_NOT_FOUND; ++index)
                {
                    DXGI_ADAPTER_DESC1 desc;
                    hardwareAdapter->GetDesc1(&desc);

                    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                    {
                        continue;
                    }

                    if (SUCCEEDED(D3D12CreateDevice(
                        hardwareAdapter.Get(),
                        static_cast<D3D_FEATURE_LEVEL>(BROTLIG_GPUD_MIN_D3D_FEATURE_LEVEL),
                        _uuidof(ID3D12Device), nullptr)))
                    {
                        wprintf(L"Using: %s\n", desc.Description);
                        break;
                    }
                }
            }

            ThrowIfFailed(D3D12CreateDevice(
                hardwareAdapter.Get(),
                static_cast<D3D_FEATURE_LEVEL>(BROTLIG_GPUD_MIN_D3D_FEATURE_LEVEL),
                IID_PPV_ARGS(&device)
            ));
            SetName(device.Get(), L"device");
        }

        D3D12_FEATURE_DATA_SHADER_MODEL model{ static_cast<D3D_SHADER_MODEL>(BROTLIG_GPUD_MAX_D3D_SHADER_MODEL) };
        ThrowIfFailed(device->CheckFeatureSupport(
            D3D12_FEATURE_SHADER_MODEL,
            &model,
            sizeof(model)
        ));

        if (model.HighestShaderModel < static_cast<D3D_SHADER_MODEL>(BROTLIG_GPUD_MIN_D3D_SHADER_MODEL))
        {
            device.Reset();
            char error_msg[45];
            sprintf_s(error_msg, "Device does not support shader model >= %s", GetShaderModelString(static_cast<D3D_SHADER_MODEL>(BROTLIG_GPUD_MIN_D3D_SHADER_MODEL)));
            throw std::exception(error_msg);
        }
    }

    /**Create Command List**/
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        queueDesc.NodeMask = 0;

        ThrowIfFailed(device->CreateCommandQueue(
            &queueDesc,
            IID_PPV_ARGS(&commandQueue)
        ));
        SetName(commandQueue.Get(), L"commandqueue");

        ThrowIfFailed(device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_COMPUTE,
            IID_PPV_ARGS(&commandAllocator)
        ));
        SetName(commandAllocator.Get(), L"commandallocator");

        ThrowIfFailed(device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_COMPUTE,
            commandAllocator.Get(),
            nullptr,
            IID_PPV_ARGS(&commandList)
        ));
        SetName(commandList.Get(), L"commandlist");

        ThrowIfFailed(device->CreateFence(
            0,
            D3D12_FENCE_FLAG_SHARED,
            IID_PPV_ARGS(&fence)
        ));
        SetName(fence.Get(), L"fence");

        fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    }

    /**Setup Pipeline State**/
    {
        // Root signature
        {
            CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeRootSignatureDesc;

            CD3DX12_ROOT_PARAMETER1 rootParameters[RootParametersCount];
            rootParameters[RootSRVInput].InitAsShaderResourceView(0);
            rootParameters[RootUAVMeta].InitAsUnorderedAccessView(0);
            rootParameters[RootUAVOutput].InitAsUnorderedAccessView(1);

            computeRootSignatureDesc.Init_1_1(
                _countof(rootParameters),
                rootParameters,
                0,
                nullptr
            );

            ComPtr<ID3DBlob> result;
            ComPtr<ID3DBlob> error;
            ThrowIfFailed(D3DX12SerializeVersionedRootSignature(
                &computeRootSignatureDesc,
                D3D_ROOT_SIGNATURE_VERSION_1_1,
                &result,
                &error
            ));

            if (error)
            {
                char error_str[1024];
                sprintf_s(error_str, "Root signature creation failed with : % s", (const char*)error->GetBufferPointer());
                throw std::exception(error_str);
            }

            ThrowIfFailed(device->CreateRootSignature(
                0,
                result->GetBufferPointer(),
                result->GetBufferSize(),
                IID_PPV_ARGS(&rootSignature)
            ));
            SetName(rootSignature.Get(), L"rootsignature");
        }

        // Shader compilation
        ComPtr<IDxcBlob> compiledShader;
        {
            LPCWSTR compileArgs[1] = {
                L""
            };

            CompileShaderFromFile(
                device.Get(),
                shaderfile,
                shaderfileOther,
                L"CSMain",
                compileArgs,
                _countof(compileArgs),
                nullptr,
                0,
                static_cast<D3D_SHADER_MODEL>(BROTLIG_GPUD_MAX_D3D_SHADER_MODEL),
                &compiledShader
            );
        }

        // Pipeline state object
        {
            D3D12_COMPUTE_PIPELINE_STATE_DESC pipelineDesc = {};
            pipelineDesc.CS.pShaderBytecode = compiledShader->GetBufferPointer();
            pipelineDesc.CS.BytecodeLength = compiledShader->GetBufferSize();
            pipelineDesc.pRootSignature = rootSignature.Get();

            ThrowIfFailed(device->CreateComputePipelineState(
                &pipelineDesc,
                IID_PPV_ARGS(&pipelineStateObject)
            ));
            SetName(pipelineStateObject.Get(), L"pipelinestateobject");
        }
    }

    // Create Buffers
    {
        size_t metadataSize = 4 * sizeof(uint32_t);

        // Upload Buffer
        CreateBuffer(
            device.Get(),
            D3D12_HEAP_TYPE_UPLOAD,
            D3D12_HEAP_FLAG_NONE,
            BROTLIG_GPUD_DEFAULT_MAX_TEMP_BUFFER_SIZE + metadataSize,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            &uploadBuffer,
            L"uploadbuffer"
        );

        // Input Buffer
        CreateBuffer(
            device.Get(),
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_HEAP_FLAG_NONE,
            BROTLIG_GPUD_DEFAULT_MAX_TEMP_BUFFER_SIZE,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COMMON,
            &inputBuffer,
            L"inputbuffer"
        );

        // Metadata Buffer
        CreateBuffer(
            device.Get(),
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_HEAP_FLAG_NONE,
            metadataSize,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COMMON,
            &metaBuffer,
            L"metadatabuffer"
        );

        // Output Buffer
        CreateBuffer(
            device.Get(),
            D3D12_HEAP_TYPE_DEFAULT,
            D3D12_HEAP_FLAG_NONE,
            BROTLIG_GPUD_DEFAULT_MAX_TEMP_BUFFER_SIZE,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COMMON,
            &outputBuffer,
            L"outputbuffer"
        );

        // Readback Buffer
        CreateBuffer(
            device.Get(),
            D3D12_HEAP_TYPE_READBACK,
            D3D12_HEAP_FLAG_NONE,
            BROTLIG_GPUD_DEFAULT_MAX_TEMP_BUFFER_SIZE,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_COPY_DEST,
            &readbackBuffer,
            L"readbackbuffer"
        );
    }

    // Initialize Queries
    {
        D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
        queryHeapDesc.Count = BROTLIG_GPUD_DEFAULT_MAX_QUERIES;
        queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        queryHeapDesc.NodeMask = 0;

        ThrowIfFailed(device->CreateQueryHeap(
            &queryHeapDesc,
            IID_PPV_ARGS(&queryHeap)
        ));
        SetName(queryHeap.Get(), L"queryheap");

        CreateBuffer(
            device.Get(),
            D3D12_HEAP_TYPE_READBACK,
            D3D12_HEAP_FLAG_NONE,
            sizeof(uint64_t)* BROTLIG_GPUD_DEFAULT_MAX_QUERIES,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_COPY_DEST,
            &queryReadbackBuffer,
            L"queryreadbackbuffer"
        );
    }

    // Get the upload ptr
    uint8_t* uploadPtr = nullptr;
    uploadBuffer->Map(0, nullptr, (void**)&uploadPtr);

    // Prepare and upload the metadata
    {
        uint32_t* pMetadata = reinterpret_cast<uint32_t*>(uploadPtr + BROTLIG_GPUD_DEFAULT_MAX_TEMP_BUFFER_SIZE);
        *pMetadata++ = 1;
        *pMetadata++ = 0;
        *pMetadata++ = 0;
        *pMetadata++ = 0;

        BarrierCopy(
            commandList.Get(),
            uploadBuffer.Get(),
            BROTLIG_GPUD_DEFAULT_MAX_TEMP_BUFFER_SIZE,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            metaBuffer.Get(),
            0,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_COMMON,
            sizeof(uint32_t) * 4
        );
    }

    // Upload the compressed input
    {
        memcpy(uploadPtr, input, input_size);

        BarrierCopy(
            commandList.Get(),
            uploadBuffer.Get(),
            0,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            inputBuffer.Get(),
            0,
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_COMMON,
            input_size
        );
    }

    // Prepare the hold and the output buffer for decompression
    {
        D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            outputBuffer.Get(),
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS
        );

        commandList->ResourceBarrier(1, &barrier);
    }

    // Decompress
    {
        commandList->SetPipelineState(pipelineStateObject.Get());
        commandList->SetComputeRootSignature(rootSignature.Get());

        commandList->SetComputeRootShaderResourceView(RootSRVInput, inputBuffer->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(RootUAVMeta, metaBuffer->GetGPUVirtualAddress());
        commandList->SetComputeRootUnorderedAccessView(RootUAVOutput, outputBuffer->GetGPUVirtualAddress());

        commandList->EndQuery(queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        commandList->Dispatch(BROTLIG_GPUD_DEFAULT_NUM_GROUPS, 1, 1);
        commandList->EndQuery(queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);

        // Resolve the query data
        commandList->ResolveQueryData(
            queryHeap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            0,
            2,
            queryReadbackBuffer.Get(),
            0
        );
    }

    // Read back output
    {
        BarrierCopy(
            commandList.Get(),
            outputBuffer.Get(),
            0,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            readbackBuffer.Get(),
            0,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_COPY_DEST,
            *output_size
        );
    }

    // Kick off execution
    {
        ThrowIfFailed(commandList->Close());
        ID3D12CommandList* pCommandLists[] = { commandList.Get() };
        commandQueue->ExecuteCommandLists(1, pCommandLists);

        fenceValue++;
        ThrowIfFailed(commandQueue->Signal(fence.Get(), fenceValue));
        ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, fenceEvent));
        WaitForSingleObject(fenceEvent, INFINITE);

        ThrowIfFailed(commandAllocator->Reset());
        ThrowIfFailed(commandList->Reset(commandAllocator.Get(), pipelineStateObject.Get()));
    }

    // Copy the output
    {
        uint8_t* pData = nullptr;
        readbackBuffer->Map(0, nullptr, (void**)&pData);
        memcpy(output, pData, *output_size);
        readbackBuffer->Unmap(0, nullptr);
    }

    // Compute decompression time
    {
        uint64_t* timestampPtr = nullptr;
        queryReadbackBuffer->Map(0, nullptr, (void**)&timestampPtr);

        uint64_t frequency;
        commandQueue->GetTimestampFrequency(&frequency);
        time += (1e6 / (double)frequency * (double)(timestampPtr[1] - timestampPtr[0]));

        queryReadbackBuffer->Unmap(0, nullptr);
    }

    uploadBuffer->Unmap(0, nullptr);

    if (fenceEvent)
        CloseHandle(fenceEvent);

    time /= 1e3;

    return BROTLIG_OK;
}