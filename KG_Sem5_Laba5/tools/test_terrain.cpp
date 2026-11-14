#include "Terrain.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdlib>

using namespace DirectX;

int main(int argc, char** argv) {
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    if (argc != 2) return 2;
    Terrain terrain;
    MeshData mesh;
    bool rejected = false;
    try { terrain.Load("missing-terrain-directory", mesh); }
    catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    const auto generated = std::filesystem::temp_directory_path() / "KG_Sem5_Laba1_TerrainTests";
    Terrain::Generate(generated.string());
    auto read = [](const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(file), {});
    };
    const auto first = read(generated / "height_x0_z0.r16");
    assert(first.size() == 257 * 257 * 2);
    Terrain::Generate(generated.string());
    assert(read(generated / "height_x0_z0.r16") == first);
    terrain.Load(generated.string(), mesh);
    assert(!mesh.vertices.empty());

    terrain.Load(argv[1], mesh);
    std::ifstream terrainConfig(std::filesystem::path(argv[1]) / "terrain.cfg");
    int tileCount, tileResolution;
    float worldSize, minimumHeight, maximumHeight;
    terrainConfig >> tileCount >> tileResolution >> worldSize >> minimumHeight >> maximumHeight;
    assert(terrain.Nodes().size() == 341);
    assert(mesh.submeshes.size() == terrain.Nodes().size());
    for (auto index : mesh.indices) assert(index < mesh.vertices.size());
    for (const auto& vertex : mesh.vertices) {
        const float length = vertex.Normal.x * vertex.Normal.x + vertex.Normal.y * vertex.Normal.y + vertex.Normal.z * vertex.Normal.z;
        assert(std::isfinite(vertex.Position.y) && std::abs(length - 1) < 0.001f);
    }
    for (const auto& node : terrain.Nodes()) {
        const auto& patch = mesh.submeshes[node.submesh];
        float bottom = FLT_MAX;
        for (unsigned int i = 0; i < patch.indexCount; ++i) {
            const auto& p = mesh.vertices[mesh.indices[patch.startIndexLocation + i]].Position;
            bottom = std::min(bottom, p.y);
            assert(std::abs(p.x - node.bounds.Center.x) <= node.bounds.Extents.x + 0.001f);
            assert(std::abs(p.y - node.bounds.Center.y) <= node.bounds.Extents.y + 0.001f);
            assert(std::abs(p.z - node.bounds.Center.z) <= node.bounds.Extents.z + 0.001f);
        }
        assert(std::abs(bottom - (minimumHeight - 10.0f)) < 0.01f);
    }
    BoundingFrustum frustum;
    auto near = terrain.Select(frustum, {0, 65, 0}, false, true);
    auto far = terrain.Select(frustum, {0, 100000, 0}, false, true);
    auto full = terrain.Select(frustum, {0, 65, 0}, false, false);
    assert(full.size() == 256 && near.size() > far.size() && far.size() == 1);
    for (const auto& selection : {near, far, full}) {
        std::unordered_set<unsigned int> unique(selection.begin(), selection.end());
        assert(unique.size() == selection.size());
        auto covered = [&](auto&& self, int index) -> unsigned int {
            const auto& node = terrain.Nodes()[index];
            if (unique.count(node.submesh)) {
                auto noDescendant = [&](auto&& visit, int child) -> void {
                    assert(!unique.count(terrain.Nodes()[child].submesh));
                    for (int c : terrain.Nodes()[child].children) if (c >= 0) visit(visit, c);
                };
                for (int child : node.children) if (child >= 0) noDescendant(noDescendant, child);
                return 1u << (2 * (4 - node.level));
            }
            unsigned int leaves = 0;
            for (int child : node.children) if (child >= 0) leaves += self(self, child);
            return leaves;
        };
        assert(covered(covered, 0) == 256);
    }
    BoundingFrustum::CreateFromMatrix(frustum, XMMatrixPerspectiveFovLH(XM_PI / 4, 1.777f, 0.1f, 1500));
    BoundingFrustum away;
    frustum.Transform(away, XMMatrixTranslation(5000, 0, 5000));
    assert(terrain.Select(away, {5000, 0, 5000}, true, true).empty());
    assert(terrain.Culled() > 0);
    BoundingFrustum toward;
    const auto view = XMMatrixLookAtLH(XMVectorSet(0, 130, -300, 1), XMVectorSet(0, 25, 0, 1), XMVectorSet(0, 1, 0, 0));
    frustum.Transform(toward, XMMatrixInverse(nullptr, view));
    auto visible = terrain.Select(toward, {0, 130, -300}, true, true);
    auto unculled = terrain.Select(toward, {0, 130, -300}, false, true);
    assert(!visible.empty() && visible.size() <= unculled.size());
    for (int z = -240; z <= 240; z += 80) {
        for (int x = -240; x <= 240; x += 80) {
            auto selection = terrain.Select(frustum, {float(x), 65, float(z)}, false, true);
            std::array<int, 256> owners;
            owners.fill(-1);
            for (auto index : selection) {
                const auto& node = terrain.Nodes()[index];
                const int size = node.cells / 64, sx = node.x / 64, sz = node.z / 64;
                for (int row = sz; row < sz + size; ++row) {
                    for (int col = sx; col < sx + size; ++col) {
                        assert(owners[row * 16 + col] == -1);
                        owners[row * 16 + col] = int(node.level);
                    }
                }
            }
            for (int row = 0; row < 16; ++row) {
                for (int col = 0; col < 16; ++col) {
                    const int level = owners[row * 16 + col];
                    assert(level >= 0);
                    if (col < 15) assert(std::abs(level - owners[row * 16 + col + 1]) <= 1);
                    if (row < 15) assert(std::abs(level - owners[(row + 1) * 16 + col]) <= 1);
                }
            }
        }
    }
    const auto& root = terrain.Nodes()[0];
    auto atDistance = [&](float distance) {
        return terrain.Select(frustum, {root.surfaceBounds.Center.x,
            root.surfaceBounds.Center.y + distance, root.surfaceBounds.Center.z}, false, true).size();
    };
    assert(atDistance(1500) == 1);
    assert(atDistance(1000) > 1);
    assert(atDistance(1100) > 1);
    assert(atDistance(1300) == 1);
    assert(atDistance(1100) == 1);
    atDistance(100000);
    auto overhead = terrain.Select(frustum, {0, root.surfaceBounds.Center.y + 300, 0}, false, true);
    assert(overhead.size() == 16);
    for (auto patch : overhead) assert(terrain.Nodes()[patch].level == 2);
    terrain.SetBrushRegion({0, 0, 0}, 4);
    auto brushDetail = terrain.Select(frustum, {0, 100000, 0}, false, true);
    bool detailedBrush = false;
    for (auto patch : brushDetail) {
        const auto& node = terrain.Nodes()[patch];
        if (node.x <= 512 && node.x + node.cells >= 512 && node.z <= 512 && node.z + node.cells >= 512) {
            assert(node.level == 4);
            detailedBrush = true;
        }
    }
    assert(detailedBrush);
    terrain.SetBrushRegion({}, 0);
    assert(terrain.Select(frustum, {0, 100000, 0}, false, true).size() == 1);
    Terrain sculpted;
    MeshData sculptMesh;
    sculpted.Load(argv[1], sculptMesh);
    XMFLOAT3 center, outside, halfRadius, edge, after;
    assert(sculpted.Raycast({0, 200, 0}, {0, -1, 0}, center));
    assert(sculpted.Raycast({20, 200, 0}, {0, -1, 0}, outside));
    assert(sculpted.Raycast({6, 200, 0}, {0, -1, 0}, halfRadius));
    assert(sculpted.Raycast({12, 200, 0}, {0, -1, 0}, edge));
    assert(!sculpted.Raycast({0, 200, 0}, {0, 1, 0}, after));
    assert(!sculpted.Raycast({0, 200, 0}, {0, 0, 0}, after));
    assert(sculpted.ApplyBrush(center, -1, 2, sculptMesh).empty());
    assert(sculpted.ApplyBrush(center, 12, std::numeric_limits<float>::quiet_NaN(), sculptMesh).empty());
    auto dirty = sculpted.ApplyBrush(center, 12, 2, sculptMesh);
    assert(!dirty.empty() && dirty.size() < sculpted.Nodes().size());
    assert(sculpted.Raycast({0, 200, 0}, {0, -1, 0}, after) && std::abs(after.y - center.y - 2) < 0.001f);
    assert(sculpted.Raycast({6, 200, 0}, {0, -1, 0}, after) && after.y > halfRadius.y && after.y < halfRadius.y + 2);
    assert(sculpted.Raycast({12, 200, 0}, {0, -1, 0}, after) && std::abs(after.y - edge.y) < 0.001f);
    assert(sculpted.Raycast({20, 200, 0}, {0, -1, 0}, after) && std::abs(after.y - outside.y) < 0.001f);
    sculpted.ApplyBrush(center, 12, -2, sculptMesh);
    assert(sculpted.Raycast({0, 200, 0}, {0, -1, 0}, after) && std::abs(after.y - center.y) < 0.001f);
    sculpted.ApplyBrush(center, 12, -80, sculptMesh);
    assert(sculpted.Raycast({0, 200, 0}, {0, -1, 0}, after) && std::abs(after.y - center.y + 80) < 0.001f);
    const float pitHeight = after.y;
    for (const auto& node : sculpted.Nodes()) {
        assert(node.vertexStart + node.vertexCount <= sculptMesh.vertices.size());
        for (unsigned int i = 0; i < node.vertexCount; ++i) {
            const auto& vertex = sculptMesh.vertices[node.vertexStart + i];
            assert(std::isfinite(vertex.Normal.x) && std::isfinite(vertex.Normal.y) && std::isfinite(vertex.Normal.z));
            assert(std::abs(vertex.Position.y - node.bounds.Center.y) <= node.bounds.Extents.y + 0.001f);
            if (i >= 33 * 33 && (i - 33 * 33) % 2 == 1) assert(vertex.Position.y <= pitHeight - 9.9f);
            else if (vertex.Position.x == 0 && vertex.Position.z == 0) assert(std::abs(vertex.Position.y - pitHeight) < 0.001f);
        }
    }
    sculpted.BuildMesh(sculptMesh);
    assert(sculpted.Raycast({0, 200, 0}, {0, -1, 0}, after) && std::abs(after.y - pitHeight) < 0.001f);
    std::cout << "Terrain checks passed: 16 tiles, 341 nodes; near=" << near.size()
        << ", far=" << far.size() << ", full=" << full.size() << ", visible=" << visible.size() << '\n';
}
