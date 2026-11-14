    #include "Meshlets.h"

#include <algorithm>
#include <array>
#include <stdexcept>

static_assert(MESHLET_MAX_VERTICES <= 256 && MESHLET_MAX_TRIANGLES <= 256);
static_assert(sizeof(Meshlet) == 16 && sizeof(Vertex) == 56);

Meshlets BuildMeshlets(const MeshData& mesh) {
    if (mesh.indices.size() > UINT32_MAX || mesh.vertices.size() > UINT32_MAX)
        throw std::runtime_error("Meshlet input exceeds the 32-bit index range");

    Meshlets result;
    constexpr uint8_t unused = 255;
    static_assert(MESHLET_MAX_VERTICES <= unused);
    std::vector<uint8_t> localIndices(mesh.vertices.size(), unused);
    size_t nextIndex = 0;

    for (const auto& submesh : mesh.submeshes) {
        if (submesh.startIndexLocation != nextIndex || submesh.indexCount % 3 != 0 ||
            submesh.indexCount > mesh.indices.size() - nextIndex)
            throw std::runtime_error("Invalid submesh triangle range");

        const auto first = static_cast<uint32_t>(result.blocks.size());
        Meshlet block{};
        const auto finishBlock = [&] {
            if (!block.triangleCount) return;
            for (uint32_t i = 0; i < block.vertexCount; ++i)
                localIndices[result.vertices[block.vertexOffset + i]] = unused;
            result.blocks.push_back(block);
            block = {static_cast<uint32_t>(result.vertices.size()), 0,
                     static_cast<uint32_t>(result.triangles.size()), 0};
        };
        block.vertexOffset = static_cast<uint32_t>(result.vertices.size());
        block.triangleOffset = static_cast<uint32_t>(result.triangles.size());

        const size_t end = nextIndex + submesh.indexCount;
        for (; nextIndex < end; nextIndex += 3) {
            std::array<uint32_t, 3> global{};
            uint32_t newVertices = 0;
            for (size_t corner = 0; corner < global.size(); ++corner) {
                const auto index = int64_t(mesh.indices[nextIndex + corner]) + submesh.baseVertexLocation;
                if (index < 0 || static_cast<uint64_t>(index) >= mesh.vertices.size())
                    throw std::runtime_error("Meshlet triangle references an invalid vertex");
                global[corner] = static_cast<uint32_t>(index);
                if (localIndices[global[corner]] == unused &&
                    std::find(global.begin(), global.begin() + corner, global[corner]) == global.begin() + corner)
                    ++newVertices;
            }

            // shortcut: keep source triangle order, add adjacency clustering if vertex reuse becomes a bottleneck.
            if (block.vertexCount + newVertices > MESHLET_MAX_VERTICES ||
                block.triangleCount == MESHLET_MAX_TRIANGLES)
                finishBlock();

            uint32_t packed = 0;
            for (size_t corner = 0; corner < global.size(); ++corner) {
                auto& index = localIndices[global[corner]];
                if (index == unused) {
                    index = static_cast<uint8_t>(block.vertexCount++);
                    result.vertices.push_back(global[corner]);
                }
                packed |= uint32_t(index) << (8 * corner);
            }
            result.triangles.push_back(packed);
            ++block.triangleCount;
        }
        finishBlock();
        result.submeshes.push_back({first, static_cast<uint32_t>(result.blocks.size()) - first});
    }

    if (nextIndex != mesh.indices.size() || result.blocks.empty())
        throw std::runtime_error("Meshlets did not cover the model");
    return result;
}
