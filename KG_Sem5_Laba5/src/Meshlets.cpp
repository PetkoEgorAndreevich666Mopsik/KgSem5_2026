#include "Meshlets.h"

#include <DirectXCollision.h>
#include <DirectXMesh.h>
#include <array>
#include <cstring>
#include <stdexcept>
#include <utility>

static_assert(MESHLET_MAX_VERTICES <= 256 && MESHLET_MAX_TRIANGLES <= 256);
static_assert(sizeof(::Meshlet) == 32 && sizeof(Vertex) == 56);

Meshlets BuildMeshlets(const MeshData& mesh) {
    if (mesh.indices.empty() || mesh.indices.size() > UINT32_MAX || mesh.vertices.size() > UINT32_MAX)
        throw std::runtime_error("Invalid model size for meshlets");

    std::vector<DirectX::XMFLOAT3> positions;
    positions.reserve(mesh.vertices.size());
    for (const auto& vertex : mesh.vertices) positions.push_back(vertex.Position);

    std::vector<uint32_t> indices;
    indices.reserve(mesh.indices.size());
    std::vector<std::pair<size_t, size_t>> subsets;
    for (const auto& submesh : mesh.submeshes) {
        const size_t start = indices.size();
        if (submesh.startIndexLocation != start || submesh.indexCount % 3 != 0 ||
            submesh.indexCount > mesh.indices.size() - start)
            throw std::runtime_error("Invalid submesh triangle range");

        subsets.emplace_back(start / 3, submesh.indexCount / 3);
        for (size_t i = start; i < start + submesh.indexCount; ++i) {
            const auto index = int64_t(mesh.indices[i]) + submesh.baseVertexLocation;
            if (index < 0 || static_cast<uint64_t>(index) >= mesh.vertices.size())
                throw std::runtime_error("Meshlet triangle references an invalid vertex");
            indices.push_back(static_cast<uint32_t>(index));
        }
    }
    if (indices.size() != mesh.indices.size()) throw std::runtime_error("Submeshes did not cover the model");

    std::vector<DirectX::Meshlet> blocks;
    std::vector<uint8_t> vertexBytes;
    std::vector<DirectX::MeshletTriangle> triangles;
    std::vector<std::pair<size_t, size_t>> ranges(subsets.size());
    const HRESULT hr = DirectX::ComputeMeshlets(indices.data(), indices.size() / 3, positions.data(), positions.size(),
        subsets.data(), subsets.size(), nullptr, blocks, vertexBytes, triangles, ranges.data(),
        MESHLET_MAX_VERTICES, MESHLET_MAX_TRIANGLES);
    if (FAILED(hr)) throw std::runtime_error("DirectXMesh::ComputeMeshlets failed");
    if (blocks.empty() || triangles.size() != indices.size() / 3)
        throw std::runtime_error("DirectXMesh did not preserve all model triangles");

    Meshlets result;
    result.vertices.resize(vertexBytes.size() / sizeof(uint32_t));
    std::memcpy(result.vertices.data(), vertexBytes.data(), vertexBytes.size());

    result.blocks.reserve(blocks.size());
    for (const auto& block : blocks) {
        std::array<DirectX::XMFLOAT3, MESHLET_MAX_VERTICES> points;
        for (uint32_t i = 0; i < block.VertCount; ++i)
            points[i] = positions[result.vertices[block.VertOffset + i]];
        DirectX::BoundingSphere sphere;
        DirectX::BoundingSphere::CreateFromPoints(sphere, block.VertCount, points.data(), sizeof(points.front()));
        result.blocks.push_back({block.VertOffset, block.VertCount, block.PrimOffset, block.PrimCount,
                                 {sphere.Center.x, sphere.Center.y, sphere.Center.z, sphere.Radius}});
    }

    result.triangles.reserve(triangles.size());
    for (const auto& triangle : triangles)
        result.triangles.push_back(triangle.i0 | (triangle.i1 << 8u) | (triangle.i2 << 16u));

    result.submeshes.reserve(ranges.size());
    for (const auto& [first, count] : ranges)
        result.submeshes.push_back({static_cast<uint32_t>(first), static_cast<uint32_t>(count)});

    return result;
}
