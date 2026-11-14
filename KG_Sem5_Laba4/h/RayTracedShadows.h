#pragma once
#include <d3d12.h>
#include <vector>
#include <wrl.h>
#include "mesh_data.h"

class RayTracedShadows {
public:
    bool Initialize(ID3D12Device *device);
    bool Supported() const { return mDevice != nullptr; }
    void Build(ID3D12GraphicsCommandList *command, ID3D12Resource *vertices, ID3D12Resource *indices,
               const MeshData &mesh, const std::vector<unsigned int> &patches, bool terrain);
    void UpdateInstances(ID3D12GraphicsCommandList *command, const std::vector<DirectX::XMFLOAT4X4> &worlds);
    void Resize(unsigned int width, unsigned int height);
    void CreateViews(D3D12_CPU_DESCRIPTOR_HANDLE srv, D3D12_CPU_DESCRIPTOR_HANDLE uav);
    void Trace(ID3D12GraphicsCommandList *command, D3D12_GPU_DESCRIPTOR_HANDLE gbuffer,
               D3D12_GPU_DESCRIPTOR_HANDLE output, D3D12_GPU_VIRTUAL_ADDRESS pass);
    const std::vector<unsigned int> &Patches() const { return mPatches; }

private:
    Microsoft::WRL::ComPtr<ID3D12Resource> Buffer(UINT64 bytes, D3D12_RESOURCE_STATES state,
                                                  D3D12_HEAP_TYPE heap = D3D12_HEAP_TYPE_DEFAULT);
    Microsoft::WRL::ComPtr<ID3D12Device5> mDevice;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mPipeline;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> mBlas;
    std::vector<unsigned int> mPatches;
    Microsoft::WRL::ComPtr<ID3D12Resource> mScratch, mTlas, mInstances, mMask;
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS mTopInputs = {};
    unsigned int mWidth = 1, mHeight = 1;
    bool mTopBuilt = false;
};
