#include "DirectStorageReader.h"
#include "Meshlets.h"
#include "RenderingSystem.h"
#include "d3dUtil.h"
#include "model_loader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <dxgi1_6.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

using Microsoft::WRL::ComPtr;

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<uint8_t> ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void CheckMeshlets(const MeshData& mesh) {
    const auto meshlets = BuildMeshlets(mesh);
    Check(meshlets.triangles.size() * 3 == mesh.indices.size(), "Triangle count changed");
    Check(meshlets.submeshes.size() == mesh.submeshes.size(), "Submesh count changed");
    size_t vertexOffset = 0, triangleOffset = 0, blockIndex = 0;
    bool partialBlock = false;
    for (size_t submeshIndex = 0; submeshIndex < mesh.submeshes.size(); ++submeshIndex) {
        const auto& submesh = mesh.submeshes[submeshIndex];
        const auto range = meshlets.submeshes[submeshIndex];
        Check(range.first == blockIndex && range.count <= meshlets.blocks.size() - blockIndex,
              "Invalid block range");
        std::vector<std::array<uint32_t, 3>> sourceTriangles, outputTriangles;
        for (size_t i = submesh.startIndexLocation; i < size_t(submesh.startIndexLocation) + submesh.indexCount; i += 3) {
            sourceTriangles.push_back({
                static_cast<uint32_t>(int64_t(mesh.indices[i]) + submesh.baseVertexLocation),
                static_cast<uint32_t>(int64_t(mesh.indices[i + 1]) + submesh.baseVertexLocation),
                static_cast<uint32_t>(int64_t(mesh.indices[i + 2]) + submesh.baseVertexLocation)
            });
        }
        for (; blockIndex < size_t(range.first) + range.count; ++blockIndex) {
            const auto& block = meshlets.blocks[blockIndex];
            Check(block.vertexOffset == vertexOffset && block.triangleOffset == triangleOffset,
                  "Invalid GPU buffer offsets");
            Check(block.vertexCount > 0 && block.vertexCount <= MESHLET_MAX_VERTICES &&
                  block.vertexCount <= meshlets.vertices.size() - vertexOffset, "Invalid vertex count");
            Check(block.triangleCount > 0 && block.triangleCount <= MESHLET_MAX_TRIANGLES &&
                  block.triangleCount <= meshlets.triangles.size() - triangleOffset, "Invalid triangle count");
            for (uint32_t i = 0; i < block.vertexCount; ++i) {
                const auto index = meshlets.vertices[vertexOffset + i];
                Check(index < mesh.vertices.size(), "Global index out of bounds");
                const auto& p = mesh.vertices[index].Position;
                const auto& s = block.sphere;
                Check(std::hypot(p.x - s.x, p.y - s.y, p.z - s.z) <= s.w + 0.0001f,
                      "Meshlet sphere missed a vertex");
            }
            partialBlock |= block.vertexCount < MESHLET_MAX_VERTICES && block.triangleCount < MESHLET_MAX_TRIANGLES;
            for (uint32_t i = 0; i < block.triangleCount; ++i) {
                const uint32_t triangle = meshlets.triangles[triangleOffset++];
                std::array<uint32_t, 3> global{};
                for (unsigned corner = 0; corner < global.size(); ++corner) {
                    const auto local = (triangle >> (8 * corner)) & 255;
                    Check(local < block.vertexCount, "Local index out of bounds");
                    global[corner] = meshlets.vertices[vertexOffset + local];
                }
                outputTriangles.push_back(global);
            }
            vertexOffset += block.vertexCount;
        }
        std::sort(sourceTriangles.begin(), sourceTriangles.end());
        std::sort(outputTriangles.begin(), outputTriangles.end());
        Check(outputTriangles == sourceTriangles, "Triangle or winding changed");
    }
    Check(blockIndex == meshlets.blocks.size() && vertexOffset == meshlets.vertices.size() &&
          triangleOffset == meshlets.triangles.size(), "Unreferenced meshlet data");
    Check(partialBlock, "Model should exercise partial blocks");
    std::cout << mesh.vertices.size() << " vertices, " << triangleOffset << " triangles, "
              << blockIndex << " meshlets\n";
}

int CheckPipeline() {
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
    ComPtr<IDXGIFactory6> factory;
    ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)), "Create DXGI factory");
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0;; ++i) {
        const HRESULT hr = factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                              IID_PPV_ARGS(&adapter));
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        ThrowIfFailed(hr, "Enumerate DXGI adapter");
        ComPtr<ID3D12Device> device;
        if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) continue;
        D3D12_FEATURE_DATA_D3D12_OPTIONS7 features{};
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &features, sizeof(features))) ||
            features.MeshShaderTier < D3D12_MESH_SHADER_TIER_1) continue;
        RenderingSystem rendering;
        Check(rendering.Initialize(device.Get(), 64, 64, DXGI_FORMAT_R8G8B8A8_UNORM,
                                   DXGI_FORMAT_D24_UNORM_S8_UINT), "Initialize renderer");
        Check(rendering.GetMeshRootSignature() && rendering.GetMeshPSO(false) && rendering.GetMeshPSO(true),
              "Mesh PSOs unavailable");
        std::cout << "Mesh PSOs created on a Mesh Shader Tier 1 adapter\n";
        return 0;
    }
    std::cout << "Mesh Shader Tier 1 unavailable\n";
    return 77;
}
}

int main(int argc, char** argv) {
    try {
        Check(argc == 3, "Expected test mode and asset directory");
        const std::string mode = argv[1];
        const std::filesystem::path assets = argv[2];
        if (mode == "pso") return CheckPipeline();
        const auto model = assets / "treasure_chest_1k.fbx";
        if (mode == "storage") {
            DirectStorageReader reader;
            for (const auto& path : {model, assets / "treasure_chest_diff_1k.jpg",
                                     assets / "treasure_chest_nor_dx_1k.jpg"}) {
                const auto expected = ReadFile(path);
                Check(!expected.empty() && reader.Read(path) == expected, "DirectStorage file content mismatch");
            }
            std::cout << "DirectStorage loaded the FBX and both textures byte-for-byte\n";
        } else {
            const auto bytes = ReadFile(model);
            Check(!bytes.empty(), "Cannot read Treasure Chest FBX");
            const auto mesh = ModelLoader::LoadModelFromMemory(bytes.data(), bytes.size(), "fbx");
            Check(mesh.indices.size() / 3 >= 100000, "Expected a high-poly model");
            CheckMeshlets(mesh);
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
