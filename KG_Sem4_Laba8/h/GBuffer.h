#pragma once

#include <array>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

class GBuffer {
public:
    enum TextureType : unsigned int {
        Albedo = 0,
        Normal = 1,
        Depth = 2,
        Count = 3
    };

    bool Initialize(ID3D12Device* device, unsigned int width, unsigned int height);
    void OnResize(ID3D12Device* device, unsigned int width, unsigned int height);

    void CreateSrvs(ID3D12Device* device,
                    D3D12_CPU_DESCRIPTOR_HANDLE cpuStart,
                    D3D12_GPU_DESCRIPTOR_HANDLE gpuStart,
                    unsigned int descriptorSize);

    void Clear(ID3D12GraphicsCommandList* cmdList) const;

    ID3D12Resource* GetTexture(TextureType type) const { return mTextures[type].Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetRtv(TextureType type) const { return mRtvHandles[type]; }
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpu(TextureType type) const { return mSrvGpuHandles[type]; }

    DXGI_FORMAT GetFormat(TextureType type) const;

private:
    void BuildResources(ID3D12Device* device);
    void BuildRtvs(ID3D12Device* device);

private:
    unsigned int mWidth = 1;
    unsigned int mHeight = 1;

    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, Count> mTextures;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;

    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, Count> mRtvHandles{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, Count> mSrvCpuHandles{};
    std::array<D3D12_GPU_DESCRIPTOR_HANDLE, Count> mSrvGpuHandles{};

    unsigned int mRtvDescriptorSize = 0;
};
