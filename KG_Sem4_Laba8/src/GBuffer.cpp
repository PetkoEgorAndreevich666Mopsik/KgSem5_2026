#include "GBuffer.h"

#include "d3dUtil.h"
#include "../h/d3dx12.h"

using Microsoft::WRL::ComPtr;

namespace {
DXGI_FORMAT kGBufferFormats[GBuffer::Count] = {
    DXGI_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT_R16G16B16A16_FLOAT,
    DXGI_FORMAT_R32_FLOAT
};
}

bool GBuffer::Initialize(ID3D12Device* device, unsigned int width, unsigned int height) {
    mWidth = width;
    mHeight = height;
    mRtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = Count;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mRtvHeap)),
                  "Create GBuffer RTV heap failed");

    BuildResources(device);
    BuildRtvs(device);

    return true;
}

void GBuffer::OnResize(ID3D12Device* device, unsigned int width, unsigned int height) {
    if (mWidth == width && mHeight == height) {
        return;
    }

    mWidth = width;
    mHeight = height;

    for (auto& texture : mTextures) {
        texture.Reset();
    }

    BuildResources(device);
    BuildRtvs(device);
}

void GBuffer::CreateSrvs(ID3D12Device* device,
                         D3D12_CPU_DESCRIPTOR_HANDLE cpuStart,
                         D3D12_GPU_DESCRIPTOR_HANDLE gpuStart,
                         unsigned int descriptorSize) {
    for (unsigned int i = 0; i < Count; ++i) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = kGBufferFormats[i];
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        auto cpuHandle = cpuStart;
        auto gpuHandle = gpuStart;
        cpuHandle.ptr += static_cast<SIZE_T>(i) * descriptorSize;
        gpuHandle.ptr += static_cast<UINT64>(i) * descriptorSize;

        device->CreateShaderResourceView(mTextures[i].Get(), &srvDesc, cpuHandle);
        mSrvCpuHandles[i] = cpuHandle;
        mSrvGpuHandles[i] = gpuHandle;
    }
}

void GBuffer::Clear(ID3D12GraphicsCommandList* cmdList) const {
    constexpr float albedoClear[4] = {0.03f, 0.03f, 0.03f, 1.0f};
    constexpr float normalClear[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    constexpr float depthClear[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    cmdList->ClearRenderTargetView(mRtvHandles[Albedo], albedoClear, 0, nullptr);
    cmdList->ClearRenderTargetView(mRtvHandles[Normal], normalClear, 0, nullptr);
    cmdList->ClearRenderTargetView(mRtvHandles[Depth], depthClear, 0, nullptr);
}

DXGI_FORMAT GBuffer::GetFormat(TextureType type) const {
    return kGBufferFormats[type];
}

void GBuffer::BuildResources(ID3D12Device* device) {
    for (unsigned int i = 0; i < Count; ++i) {
        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Alignment = 0;
        texDesc.Width = mWidth;
        texDesc.Height = mHeight;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = kGBufferFormats[i];
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clear = {};
        clear.Format = kGBufferFormats[i];

        if (i == Albedo) {
            clear.Color[0] = 0.03f; clear.Color[1] = 0.03f; clear.Color[2] = 0.03f; clear.Color[3] = 1.0f;
        } else if (i == Normal) {
            clear.Color[0] = 0.0f; clear.Color[1] = 0.0f; clear.Color[2] = 1.0f; clear.Color[3] = 1.0f;
        } else {
            clear.Color[0] = 1.0f; clear.Color[1] = 1.0f; clear.Color[2] = 1.0f; clear.Color[3] = 1.0f;
        }

        ThrowIfFailed(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            &clear,
            IID_PPV_ARGS(&mTextures[i])),
            "Create GBuffer texture failed");
    }
}

void GBuffer::BuildRtvs(ID3D12Device* device) {
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (unsigned int i = 0; i < Count; ++i) {
        device->CreateRenderTargetView(mTextures[i].Get(), nullptr, rtvHandle);
        mRtvHandles[i] = rtvHandle;
        rtvHandle.Offset(1, mRtvDescriptorSize);
    }
}
