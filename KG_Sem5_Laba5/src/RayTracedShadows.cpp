#include "RayTracedShadows.h"
#include <algorithm>
#include <cstring>
#include "d3dUtil.h"
#include "d3dx12.h"

using Microsoft::WRL::ComPtr;

bool RayTracedShadows::Initialize(ID3D12Device *device) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options = {};
    D3D12_FEATURE_DATA_SHADER_MODEL model = {D3D_SHADER_MODEL_6_5};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options))) ||
        options.RaytracingTier < D3D12_RAYTRACING_TIER_1_1 ||
        FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model))) ||
        model.HighestShaderModel < D3D_SHADER_MODEL_6_5)
        return false;
    ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&mDevice)), "Query DXR device failed");
    CD3DX12_DESCRIPTOR_RANGE input, output;
    input.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);
    output.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
    CD3DX12_ROOT_PARAMETER params[4];
    params[0].InitAsDescriptorTable(1, &input);
    params[1].InitAsShaderResourceView(3);
    params[2].InitAsDescriptorTable(1, &output);
    params[3].InitAsConstantBufferView(0);
    CD3DX12_ROOT_SIGNATURE_DESC desc(4, params);
    ComPtr<ID3DBlob> code, errors;
    ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &code, &errors),
                  "Serialize DXR root failed");
    ThrowIfFailed(device->CreateRootSignature(0, code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&mRoot)),
                  "Create DXR root failed");
    ThrowIfFailed(D3DReadFileToBlob(L"rt_shadows.cso", &code), "Read RT shader failed; build the shaders target");
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline = {};
    pipeline.pRootSignature = mRoot.Get();
    pipeline.CS = {code->GetBufferPointer(), code->GetBufferSize()};
    ThrowIfFailed(device->CreateComputePipelineState(&pipeline, IID_PPV_ARGS(&mPipeline)),
                  "Create DXR pipeline failed");
    return true;
}

ComPtr<ID3D12Resource> RayTracedShadows::Buffer(UINT64 bytes, D3D12_RESOURCE_STATES state, D3D12_HEAP_TYPE heap) {
    ComPtr<ID3D12Resource> result;
    const auto properties = CD3DX12_HEAP_PROPERTIES(heap);
    const auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytes, heap == D3D12_HEAP_TYPE_DEFAULT
                                                                   ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                                                                   : D3D12_RESOURCE_FLAG_NONE);
    ThrowIfFailed(mDevice->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr,
                                                   IID_PPV_ARGS(&result)),
                  "Create DXR buffer failed");
    return result;
}

void RayTracedShadows::Build(ID3D12GraphicsCommandList *command, ID3D12Resource *vertices, ID3D12Resource *indices,
                             const MeshData &mesh, const std::vector<unsigned int> &patches, bool terrain) {
    if (!Supported())
        return;
    ComPtr<ID3D12GraphicsCommandList4> cmd;
    ThrowIfFailed(command->QueryInterface(IID_PPV_ARGS(&cmd)), "Query DXR command list failed");
    mBlas.clear();
    mPatches = patches;
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometries;
    UINT64 scratchBytes = 0;
    for (auto patch: patches) {
        const auto &submesh = mesh.submeshes.at(patch);
        D3D12_RAYTRACING_GEOMETRY_DESC geometry = {};
        geometry.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometry.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        auto &triangles = geometry.Triangles;
        triangles.VertexBuffer.StartAddress =
                vertices->GetGPUVirtualAddress() + UINT64(submesh.baseVertexLocation) * sizeof(Vertex);
        triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
        triangles.VertexCount = static_cast<UINT>(mesh.vertices.size()) - submesh.baseVertexLocation;
        triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        triangles.IndexBuffer =
                indices->GetGPUVirtualAddress() + UINT64(submesh.startIndexLocation) * sizeof(unsigned int);
        triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        triangles.IndexCount = terrain ? 32u * 32u * 6u : submesh.indexCount;
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS input = {};
        input.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        input.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        input.NumDescs = 1;
        input.pGeometryDescs = &geometry;
        input.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO size = {};
        mDevice->GetRaytracingAccelerationStructurePrebuildInfo(&input, &size);
        scratchBytes = std::max(scratchBytes, size.ScratchDataSizeInBytes);
        geometries.push_back(geometry);
        mBlas.push_back(Buffer(size.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE));
    }
    mTopInputs = {};
    mTopInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    mTopInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    mTopInputs.NumDescs = static_cast<UINT>(patches.size());
    mTopInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
                       D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topSize = {};
    mDevice->GetRaytracingAccelerationStructurePrebuildInfo(&mTopInputs, &topSize);
    scratchBytes = std::max({scratchBytes, topSize.ScratchDataSizeInBytes, topSize.UpdateScratchDataSizeInBytes});
    mScratch = Buffer(scratchBytes, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    mTlas = Buffer(topSize.ResultDataMaxSizeInBytes, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);
    mInstances = Buffer(patches.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC), D3D12_RESOURCE_STATE_GENERIC_READ,
                        D3D12_HEAP_TYPE_UPLOAD);
    mTopBuilt = false;
    for (size_t i = 0; i < geometries.size(); ++i) {
        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {};
        build.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        build.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        build.Inputs.NumDescs = 1;
        build.Inputs.pGeometryDescs = &geometries[i];
        build.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        build.ScratchAccelerationStructureData = mScratch->GetGPUVirtualAddress();
        build.DestAccelerationStructureData = mBlas[i]->GetGPUVirtualAddress();
        cmd->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
        const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
        command->ResourceBarrier(1, &barrier);
    }
}

void RayTracedShadows::UpdateInstances(ID3D12GraphicsCommandList *command,
                                       const std::vector<DirectX::XMFLOAT4X4> &worlds) {
    if (!Supported())
        return;
    if (worlds.size() != mBlas.size())
        throw std::runtime_error("RT instance count mismatch");
    D3D12_RAYTRACING_INSTANCE_DESC *instances = nullptr;
    const D3D12_RANGE noRead = {0, 0};
    ThrowIfFailed(mInstances->Map(0, &noRead, reinterpret_cast<void **>(&instances)), "Map RT instances failed");
    for (size_t i = 0; i < worlds.size(); ++i) {
        instances[i] = {};
        DirectX::XMFLOAT4X4 transposed;
        DirectX::XMStoreFloat4x4(&transposed, DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&worlds[i])));
        std::memcpy(instances[i].Transform, &transposed, sizeof(instances[i].Transform));
        instances[i].InstanceID = static_cast<UINT>(i);
        instances[i].InstanceMask = 0xff;
        instances[i].Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
        instances[i].AccelerationStructure = mBlas[i]->GetGPUVirtualAddress();
    }
    mInstances->Unmap(0, nullptr);
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC build = {};
    build.Inputs = mTopInputs;
    build.Inputs.InstanceDescs = mInstances->GetGPUVirtualAddress();
    if (mTopBuilt) {
        build.Inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
        build.SourceAccelerationStructureData = mTlas->GetGPUVirtualAddress();
    }
    build.DestAccelerationStructureData = mTlas->GetGPUVirtualAddress();
    build.ScratchAccelerationStructureData = mScratch->GetGPUVirtualAddress();
    ComPtr<ID3D12GraphicsCommandList4> cmd;
    ThrowIfFailed(command->QueryInterface(IID_PPV_ARGS(&cmd)), "Query DXR command list failed");
    cmd->BuildRaytracingAccelerationStructure(&build, 0, nullptr);
    const auto barrier = CD3DX12_RESOURCE_BARRIER::UAV(nullptr);
    command->ResourceBarrier(1, &barrier);
    mTopBuilt = true;
}

void RayTracedShadows::Resize(unsigned int width, unsigned int height) {
    mWidth = width;
    mHeight = height;
    const auto properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, 1, 1, 1, 0,
                                                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    ID3D12Device *device = mDevice.Get();
    ThrowIfFailed(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc,
                                                  D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, nullptr,
                                                  IID_PPV_ARGS(&mMask)),
                  "Create RT mask failed");
}

void RayTracedShadows::CreateViews(D3D12_CPU_DESCRIPTOR_HANDLE srv, D3D12_CPU_DESCRIPTOR_HANDLE uav) {
    D3D12_SHADER_RESOURCE_VIEW_DESC view = {};
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    view.Texture2D.MipLevels = 1;
    mDevice->CreateShaderResourceView(mMask.Get(), &view, srv);
    D3D12_UNORDERED_ACCESS_VIEW_DESC output = {};
    output.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    output.Format = view.Format;
    mDevice->CreateUnorderedAccessView(mMask.Get(), nullptr, &output, uav);
}

void RayTracedShadows::Trace(ID3D12GraphicsCommandList *command, D3D12_GPU_DESCRIPTOR_HANDLE gbuffer,
                             D3D12_GPU_DESCRIPTOR_HANDLE output, D3D12_GPU_VIRTUAL_ADDRESS pass) {
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mMask.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
                                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    command->ResourceBarrier(1, &barrier);
    command->SetComputeRootSignature(mRoot.Get());
    command->SetPipelineState(mPipeline.Get());
    command->SetComputeRootDescriptorTable(0, gbuffer);
    command->SetComputeRootShaderResourceView(1, mTlas->GetGPUVirtualAddress());
    command->SetComputeRootDescriptorTable(2, output);
    command->SetComputeRootConstantBufferView(3, pass);
    command->Dispatch((mWidth + 7) / 8, (mHeight + 7) / 8, 1);
    barrier = CD3DX12_RESOURCE_BARRIER::Transition(mMask.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                   D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    command->ResourceBarrier(1, &barrier);
}
