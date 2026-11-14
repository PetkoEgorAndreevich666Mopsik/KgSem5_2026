#include "RayTracedShadows.h"
#include "DirectXApp.h"
#include "d3dx12.h"
#include "d3dUtil.h"
#include <d3d12sdklayers.h>
#include <iostream>
#include <cstring>
#include <cmath>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    try {
        ComPtr<ID3D12Debug> debug;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
        ComPtr<IDXGIFactory6> factory;
        ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
        ComPtr<ID3D12Device> device;
        RayTracedShadows shadows;
        for (UINT i = 0; ; ++i) {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND) break;
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))) && shadows.Initialize(device.Get())) break;
        }
        if (!shadows.Supported()) { std::cout << "SKIP: DXR unavailable\n"; return 77; }
        ComPtr<ID3D12InfoQueue> info;
        device.As(&info);
        ComPtr<ID3D12CommandQueue> queue;
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        ThrowIfFailed(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
        ComPtr<ID3D12CommandAllocator> allocator;
        ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        ComPtr<ID3D12GraphicsCommandList> cmd;
        ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&cmd)));
        ComPtr<ID3D12Fence> fence;
        ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        UINT64 serial = 0;
        const auto submit = [&] {
            ThrowIfFailed(cmd->Close());
            ID3D12CommandList* lists[] = {cmd.Get()};
            queue->ExecuteCommandLists(1, lists);
            ThrowIfFailed(queue->Signal(fence.Get(), ++serial));
            HANDLE event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            Require(event != nullptr, "Create fence event failed");
            ThrowIfFailed(fence->SetEventOnCompletion(serial, event));
            const auto wait = WaitForSingleObject(event, 30000);
            CloseHandle(event);
            Require(wait == WAIT_OBJECT_0, "GPU fence timed out");
            ThrowIfFailed(device->GetDeviceRemovedReason(), "Device removed");
            ThrowIfFailed(allocator->Reset());
            ThrowIfFailed(cmd->Reset(allocator.Get(), nullptr));
        };
        const auto buffer = [&](UINT64 bytes, D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state, const void* data = nullptr) {
            ComPtr<ID3D12Resource> result;
            const auto properties = CD3DX12_HEAP_PROPERTIES(heap);
            const auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytes, state == D3D12_RESOURCE_STATE_UNORDERED_ACCESS ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE);
            ThrowIfFailed(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&result)));
            if (data) {
                void* mapped = nullptr;
                ThrowIfFailed(result->Map(0, nullptr, &mapped));
                std::memcpy(mapped, data, static_cast<size_t>(bytes));
                result->Unmap(0, nullptr);
            }
            return result;
        };
        MeshData mesh;
        for (auto p : {XMFLOAT3(-1,2,-1), XMFLOAT3(1,2,-1), XMFLOAT3(1,2,1), XMFLOAT3(-1,2,1)}) {
            Vertex vertex; vertex.Position = p; mesh.vertices.push_back(vertex);
        }
        mesh.indices = {0,1,2,0,2,3};
        Submesh patch; patch.indexCount = 6; mesh.submeshes.push_back(patch);
        auto vertices = buffer(mesh.vertices.size()*sizeof(Vertex), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, mesh.vertices.data());
        auto indices = buffer(mesh.indices.size()*sizeof(UINT), D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, mesh.indices.data());
        shadows.Build(cmd.Get(), vertices.Get(), indices.Get(), mesh, {0}, false);
        constexpr UINT size = 128;
        shadows.Resize(size, size);
        ComPtr<ID3D12DescriptorHeap> heap, rtvs;
        D3D12_DESCRIPTOR_HEAP_DESC hd = {};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = 5; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)));
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors = 2; hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ThrowIfFailed(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&rtvs)));
        const auto stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        const auto cpu = [&](int i) { return CD3DX12_CPU_DESCRIPTOR_HANDLE(heap->GetCPUDescriptorHandleForHeapStart(), i, stride); };
        const auto gpu = [&](int i) { return CD3DX12_GPU_DESCRIPTOR_HANDLE(heap->GetGPUDescriptorHandleForHeapStart(), i, stride); };
        std::vector<ComPtr<ID3D12Resource>> textures;
        for (UINT i = 0; i < 2; ++i) {
            const auto properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32B32A32_FLOAT, size, size, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
            ComPtr<ID3D12Resource> texture;
            ThrowIfFailed(device->CreateCommittedResource(&properties, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(&texture)));
            auto rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvs->GetCPUDescriptorHandleForHeapStart(), i, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
            device->CreateRenderTargetView(texture.Get(), nullptr, rtv);
            const float normal[] = {0.5f,1,0.5f,1}, depth[] = {0.5f,0,0,0};
            cmd->ClearRenderTargetView(rtv, i == 0 ? normal : depth, 0, nullptr);
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(texture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            cmd->ResourceBarrier(1, &barrier);
            device->CreateShaderResourceView(texture.Get(), nullptr, cpu(i+1));
            if (i == 0) device->CreateShaderResourceView(texture.Get(), nullptr, cpu(0));
            textures.push_back(texture);
        }
        shadows.CreateViews(cpu(3), cpu(4));
        CD3DX12_DESCRIPTOR_RANGE range;
        range.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        CD3DX12_ROOT_PARAMETER params[2];
        params[0].InitAsDescriptorTable(1, &range); params[1].InitAsUnorderedAccessView(0);
        CD3DX12_ROOT_SIGNATURE_DESC rd(2, params);
        ComPtr<ID3DBlob> code, errors;
        ThrowIfFailed(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &code, &errors));
        ComPtr<ID3D12RootSignature> root;
        ThrowIfFailed(device->CreateRootSignature(0, code->GetBufferPointer(), code->GetBufferSize(), IID_PPV_ARGS(&root)));
        const char* gather = "Texture2D<float4> mask:register(t0); RWStructuredBuffer<float4> output:register(u0); [numthreads(8,8,1)] void CS(uint3 p:SV_DispatchThreadID){output[p.y*128+p.x]=mask[p.xy];}";
        ThrowIfFailed(D3DCompile(gather, std::strlen(gather), nullptr, nullptr, nullptr, "CS", "cs_5_1", 0, 0, &code, &errors));
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {}; pd.pRootSignature = root.Get(); pd.CS = {code->GetBufferPointer(),code->GetBufferSize()};
        ComPtr<ID3D12PipelineState> pipeline;
        ThrowIfFailed(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pipeline)));
        auto output = buffer(size*size*sizeof(XMFLOAT4), D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        auto readback = buffer(size*size*sizeof(XMFLOAT4), D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        UploadBuffer<PassConstants> constants(device.Get(), 1, true);
        PassConstants pass;
        XMStoreFloat4x4(&pass.InvViewProj, XMMatrixTranspose(XMMatrixSet(4,0,0,0, 0,0,4,0, 0,0,0,0, 0,0,0,1)));
        pass.ShadowLight = {0,1,0,0}; pass.ShadowOptions = {0.003f,0,0,0};
        const auto run = [&](int mode, float radius, float x, float y) {
            pass.ShadowSettings = {float(mode),radius,32,1}; constants.CopyData(0,pass);
            XMFLOAT4X4 world; XMStoreFloat4x4(&world, XMMatrixTranslation(x,y,0));
            shadows.UpdateInstances(cmd.Get(), {world});
            ID3D12DescriptorHeap* heaps[] = {heap.Get()}; cmd->SetDescriptorHeaps(1,heaps);
            shadows.Trace(cmd.Get(), gpu(0), gpu(4), constants.Resource()->GetGPUVirtualAddress());
            cmd->SetComputeRootSignature(root.Get()); cmd->SetPipelineState(pipeline.Get());
            cmd->SetComputeRootDescriptorTable(0,gpu(3)); cmd->SetComputeRootUnorderedAccessView(1,output->GetGPUVirtualAddress());
            cmd->Dispatch(size/8,size/8,1);
            D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(output.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_COPY_SOURCE);
            cmd->ResourceBarrier(1,&barrier); cmd->CopyResource(readback.Get(),output.Get());
            std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter); cmd->ResourceBarrier(1,&barrier);
            submit();
            XMFLOAT4* mapped = nullptr; ThrowIfFailed(readback->Map(0,nullptr,reinterpret_cast<void**>(&mapped)));
            std::vector<XMFLOAT4> result(mapped,mapped+size*size); readback->Unmap(0,nullptr);
            return result;
        };
        const auto partial = [](const auto& values) { size_t count=0; for (auto v:values) if(v.y>0 && v.y<1) ++count; return count; };
        auto hard = run(1,0,0,0);
        Require(hard[64*size+64].x == 0 && hard[0].x == 1, "Offscreen blocker visibility failed");
        Require(std::abs(hard[64*size+64].z - 1.994f)<0.01f, "Nearest blocker distance failed");
        auto zero = run(2,0,0,0);
        for (size_t i=0;i<zero.size();++i) Require(zero[i].y==hard[i].x,"Zero source differs from hard shadows");
        auto smallSource = run(2,0.03f,0,0), large = run(2,0.14f,0,0), distant = run(2,0.14f,0,2);
        Require(partial(large)>partial(smallSource), "Source radius does not widen penumbra");
        Require(partial(distant)>partial(large), "Blocker separation does not widen penumbra");
        auto moved = run(1,0,3,0);
        Require(moved[64*size+64].x==1 && moved[64*size+112].x==0, "TLAS transform update failed");
        auto none = run(0,0,0,0);
        for (auto value:none) Require(value.x==1 && value.y==1,"No shadows mode failed");
        for (auto& vertex : mesh.vertices) vertex.Position.y = 1;
        void* editedVertices = nullptr;
        ThrowIfFailed(vertices->Map(0, nullptr, &editedVertices));
        std::memcpy(editedVertices, mesh.vertices.data(), mesh.vertices.size() * sizeof(Vertex));
        vertices->Unmap(0, nullptr);
        shadows.Build(cmd.Get(), vertices.Get(), indices.Get(), mesh, {0}, false);
        auto edited = run(1, 0, 0, 0);
        Require(std::abs(edited[64 * size + 64].z - 0.994f) < 0.01f, "BLAS geometry rebuild failed");
        if (info) for (UINT64 i=0;i<info->GetNumStoredMessages();++i) {
            SIZE_T bytes=0; info->GetMessage(i,nullptr,&bytes); std::vector<char> storage(bytes);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data()); info->GetMessage(i,message,&bytes);
            if (message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR) throw std::runtime_error(message->pDescription);
        }
        ThrowIfFailed(cmd->Close());
        std::cout << "RT GPU checks passed: visibility, nearest hit, zero radius, source size, contact hardening, moving offscreen blocker, disabled mode, geometry rebuild\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
