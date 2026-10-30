#include "Terrain.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <limits>
#include <stdexcept>

using namespace DirectX;

namespace {
float RandomHeight(uint32_t x, uint32_t z) {
    uint32_t hash = x * 374761393u + z * 668265263u + 42u;
    hash = (hash ^ (hash >> 13)) * 1274126177u;
    hash ^= hash >> 16;
    return float(hash & 0x00ffffffu) / 16777215.0f;
}

float Noise(float x, float z) {
    const auto ix = static_cast<uint32_t>(std::floor(x));
    const auto iz = static_cast<uint32_t>(std::floor(z));
    float u = x - std::floor(x), v = z - std::floor(z);
    u = u * u * (3 - 2 * u);
    v = v * v * (3 - 2 * v);
    const float a = RandomHeight(ix, iz), b = RandomHeight(ix + 1, iz);
    const float c = RandomHeight(ix, iz + 1), d = RandomHeight(ix + 1, iz + 1);
    return (a + (b - a) * u) * (1 - v) + (c + (d - c) * u) * v;
}
}

void Terrain::Generate(const std::string& directory) {
    constexpr int resolution = 1025, tileSize = 257;
    std::vector<float> heights(resolution * resolution);
    for (int z = 0; z < resolution; ++z) {
        for (int x = 0; x < resolution; ++x) {
            float height = 0, amplitude = 1, frequency = 1;
            for (int octave = 0; octave < 4; ++octave) {
                height += amplitude * Noise((x / 300.0f + 2.4f) * frequency, (z / 300.0f + 0.7f) * frequency);
                amplitude *= 0.5f;
                frequency *= 2;
            }
            heights[z * resolution + x] = height;
        }
    }
    const auto range = std::minmax_element(heights.begin(), heights.end());
    const float low = *range.first, high = *range.second;
    if (!std::isfinite(low) || !std::isfinite(high) || high <= low)
        throw std::runtime_error("Invalid generated terrain heights");
    std::filesystem::create_directories(directory);
    auto write = [](const std::string& path, const char* data, size_t bytes) {
        std::ofstream file(path, std::ios::binary);
        if (!file.write(data, bytes) || !file.flush())
            throw std::runtime_error("Cannot save terrain file: " + path);
    };
    for (int tz = 0; tz < 4; ++tz) {
        for (int tx = 0; tx < 4; ++tx) {
            std::vector<uint16_t> tile(tileSize * tileSize);
            std::string pgm = "P5\n257 257\n65535\n";
            for (int z = 0; z < tileSize; ++z) {
                for (int x = 0; x < tileSize; ++x) {
                    const float h = heights[(tz * 256 + z) * resolution + tx * 256 + x];
                    const auto value = static_cast<uint16_t>(std::lround(std::clamp((h - low) / (high - low), 0.0f, 1.0f) * 65535));
                    tile[z * tileSize + x] = value;
                    pgm.push_back(static_cast<char>(value >> 8));
                    pgm.push_back(static_cast<char>(value & 255));
                }
            }
            const auto name = directory + "/height_x" + std::to_string(tx) + "_z" + std::to_string(tz);
            write(name + ".r16", reinterpret_cast<const char*>(tile.data()), tile.size() * sizeof(uint16_t));
            write(name + ".pgm", pgm.data(), pgm.size());
        }
    }
    const std::string config = "4 257 512 0 65\n";
    write(directory + "/terrain.cfg", config.data(), config.size());
    const std::string metadata = "{\n  \"generator\": \"C++ value noise fBm\",\n  \"seed\": 42,\n  \"octaves\": 4,\n  \"size\": 512,\n  \"tiles\": 4,\n  \"tileResolution\": 257,\n  \"minHeight\": 0,\n  \"maxHeight\": 65\n}\n";
    write(directory + "/terrain.json", metadata.data(), metadata.size());
}

float Terrain::Height(int x, int z) const {
    x = std::clamp(x, 0, mResolution - 1);
    z = std::clamp(z, 0, mResolution - 1);
    return mHeights[z * mResolution + x];
}

void Terrain::Load(const std::string& directory, MeshData& mesh) {
    int tiles = 0, resolution = 0;
    float low = 0, high = 0;
    std::ifstream config(directory + "/terrain.cfg");
    if (!(config >> tiles >> resolution >> mSize >> low >> high) || tiles != 4 || resolution != 257 ||
        !std::isfinite(mSize) || !std::isfinite(low) || !std::isfinite(high) || mSize <= 0 || high <= low)
        throw std::runtime_error("Invalid terrain configuration");
    mResolution = tiles * (resolution - 1) + 1;
    mHeights.assign(mResolution * mResolution, std::numeric_limits<float>::quiet_NaN());
    std::vector<uint16_t> data(resolution * resolution);
    for (int tz = 0; tz < tiles; ++tz) {
        for (int tx = 0; tx < tiles; ++tx) {
            std::ifstream file(directory + "/height_x" + std::to_string(tx) + "_z" + std::to_string(tz) + ".r16", std::ios::binary);
            if (!file.read(reinterpret_cast<char*>(data.data()), data.size() * sizeof(uint16_t)) || file.peek() != EOF)
                throw std::runtime_error("Missing or invalid terrain tile");
            for (int z = 0; z < resolution; ++z) {
                for (int x = 0; x < resolution; ++x) {
                    float h = low + data[z * resolution + x] / 65535.0f * (high - low);
                    auto& value = mHeights[(tz * (resolution - 1) + z) * mResolution + tx * (resolution - 1) + x];
                    if (std::isfinite(value) && std::abs(value - h) > 0.0001f)
                        throw std::runtime_error("Terrain tile edges do not match");
                    value = h;
                }
            }
        }
    }
    mOriginalHeights = mHeights;
    mSkirtBottom = *std::min_element(mHeights.begin(), mHeights.end()) - 10.0f;
    const auto range = std::minmax_element(mHeights.begin(), mHeights.end());
    mLodHeight = (*range.first + *range.second) * 0.5f;
    BuildMesh(mesh);
}

void Terrain::BuildMesh(MeshData& mesh) {
    mesh = {};
    mNodes.clear();
    mNodes.reserve(341);
    Build(mesh, 0, 0, mResolution - 1, 0);
    mSplit.assign(mNodes.size(), false);
}

XMFLOAT3 Terrain::Normal(int x, int z, const std::vector<float>& heights) const {
    auto sample = [&](int sx, int sz) { return heights[std::clamp(sz, 0, mResolution - 1) * mResolution + std::clamp(sx, 0, mResolution - 1)]; };
    XMFLOAT3 normal;
    XMStoreFloat3(&normal, XMVector3Normalize(XMVectorSet(sample(x - 1, z) - sample(x + 1, z),
        2 * mSize / (mResolution - 1), sample(x, z - 1) - sample(x, z + 1), 0)));
    return normal;
}

bool Terrain::Raycast(const XMFLOAT3& origin, const XMFLOAT3& direction, XMFLOAT3& hit) const {
    if (mNodes.empty() || !std::isfinite(origin.x) || !std::isfinite(origin.y) || !std::isfinite(origin.z) ||
        !std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z)) return false;
    const auto start = XMLoadFloat3(&origin);
    const auto rawDirection = XMLoadFloat3(&direction);
    if (XMVectorGetX(XMVector3LengthSq(rawDirection)) < 0.000001f) return false;
    const auto ray = XMVector3Normalize(rawDirection);
    float closest = FLT_MAX;
    const float spacing = mSize / (mResolution - 1);
    auto point = [&](int x, int z) { return XMVectorSet(x * spacing - mSize / 2, Height(x, z), z * spacing - mSize / 2, 1); };
    auto visit = [&](auto&& self, int index) -> void {
        const auto& node = mNodes[index];
        float distance;
        if (!node.surfaceBounds.Intersects(start, ray, distance) || distance > closest) return;
        if (node.children[0] >= 0) {
            for (int child : node.children) self(self, child);
            return;
        }
        const int step = node.cells / 32;
        for (int z = node.z; z < node.z + node.cells; z += step) {
            for (int x = node.x; x < node.x + node.cells; x += step) {
                const auto a = point(x, z), b = point(x + step, z), c = point(x, z + step), d = point(x + step, z + step);
                if (TriangleTests::Intersects(start, ray, a, c, b, distance)) closest = std::min(closest, distance);
                if (TriangleTests::Intersects(start, ray, b, c, d, distance)) closest = std::min(closest, distance);
            }
        }
    };
    visit(visit, 0);
    if (closest == FLT_MAX) return false;
    XMStoreFloat3(&hit, start + ray * closest);
    return true;
}

std::vector<unsigned int> Terrain::ApplyBrush(const XMFLOAT3& center, float radius, float amount, MeshData& mesh) {
    std::vector<unsigned int> dirty;
    if (mNodes.empty() || !std::isfinite(center.x) || !std::isfinite(center.z) || !std::isfinite(radius) ||
        !std::isfinite(amount) || radius <= 0 || amount == 0 || std::abs(amount) > 100 || mesh.submeshes.size() != mNodes.size()) return dirty;
    const float spacing = mSize / (mResolution - 1);
    if (center.x + radius < -mSize / 2 || center.x - radius > mSize / 2 || center.z + radius < -mSize / 2 || center.z - radius > mSize / 2) return dirty;
    const int x0 = int(std::floor(std::clamp((center.x - radius + mSize / 2) / spacing, 0.0f, float(mResolution - 1))));
    const int x1 = int(std::ceil(std::clamp((center.x + radius + mSize / 2) / spacing, 0.0f, float(mResolution - 1))));
    const int z0 = int(std::floor(std::clamp((center.z - radius + mSize / 2) / spacing, 0.0f, float(mResolution - 1))));
    const int z1 = int(std::ceil(std::clamp((center.z + radius + mSize / 2) / spacing, 0.0f, float(mResolution - 1))));
    float low = FLT_MAX, high = -FLT_MAX;
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const float dx = x * spacing - mSize / 2 - center.x, dz = z * spacing - mSize / 2 - center.z;
            const float weight = std::max(0.0f, 1 - (dx * dx + dz * dz) / (radius * radius));
            auto& value = mHeights[z * mResolution + x];
            const float next = std::clamp(value + amount * weight * weight, -mSize / 2, mSize / 2);
            if (value == next) continue;
            value = next;
            low = std::min(low, value);
            high = std::max(high, value);
        }
    }
    if (low == FLT_MAX) return dirty;
    const bool lowerBase = low < mSkirtBottom + 2;
    if (lowerBase) mSkirtBottom = low - 10;
    for (unsigned int i = 0; i < mNodes.size(); ++i) {
        auto& node = mNodes[i];
        const auto& box = node.surfaceBounds;
        const float dx = std::max(0.0f, std::abs(center.x - box.Center.x) - box.Extents.x);
        const float dz = std::max(0.0f, std::abs(center.z - box.Center.z) - box.Extents.z);
        if (!lowerBase && dx * dx + dz * dz > (radius + spacing * 2) * (radius + spacing * 2)) continue;
        for (unsigned int v = 0; v < node.vertexCount; ++v) {
            auto& vertex = mesh.vertices[node.vertexStart + v];
            const int x = int(std::lround(vertex.TexC.x * (mResolution - 1)));
            const int z = int(std::lround(vertex.TexC.y * (mResolution - 1)));
            const bool bottom = v >= 33 * 33 && (v - 33 * 33) % 2 == 1;
            vertex.Position.y = bottom ? mSkirtBottom : Height(x, z);
            if (vertex.Tangent.y > -0.5f) vertex.Normal = Normal(x, z, mHeights);
        }
        const float minimum = std::min(box.Center.y - box.Extents.y, low);
        const float maximum = std::max(box.Center.y + box.Extents.y, high);
        node.surfaceBounds.Center.y = (minimum + maximum) / 2;
        node.surfaceBounds.Extents.y = (maximum - minimum) / 2;
        node.bounds.Center.y = (mSkirtBottom + maximum) / 2;
        node.bounds.Extents.y = (maximum - mSkirtBottom) / 2;
        dirty.push_back(i);
    }
    return dirty;
}

int Terrain::Build(MeshData& mesh, int x, int z, int cells, unsigned int level) {
    constexpr int grid = 32;
    const int index = static_cast<int>(mNodes.size());
    mNodes.push_back({});
    const int step = cells / grid;
    const float spacing = mSize / (mResolution - 1);
    float low = std::numeric_limits<float>::max(), high = -low;
    for (int dz = 0; dz <= cells; ++dz) {
        for (int dx = 0; dx <= cells; ++dx) {
            const float h = Height(x + dx, z + dz);
            low = std::min(low, h);
            high = std::max(high, h);

        }
    }
    const unsigned int base = static_cast<unsigned int>(mesh.vertices.size());
    Submesh submesh;
    submesh.startIndexLocation = static_cast<unsigned int>(mesh.indices.size());
    submesh.material.shininess = 2;
    for (int row = 0; row <= grid; ++row) {
        for (int col = 0; col <= grid; ++col) {
            const int sx = x + col * step, sz = z + row * step;
            Vertex vertex;
            vertex.Position = {(sx * spacing) - mSize / 2, Height(sx, sz), (sz * spacing) - mSize / 2};
            vertex.Normal = Normal(sx, sz, mHeights);
            vertex.Bitangent = Normal(sx, sz, mOriginalHeights);
            vertex.TexC = {float(sx) / (mResolution - 1), float(sz) / (mResolution - 1)};
            mesh.vertices.push_back(vertex);
        }
    }
    for (int row = 0; row < grid; ++row) {
        for (int col = 0; col < grid; ++col) {
            unsigned int a = base + row * (grid + 1) + col, b = a + 1, c = a + grid + 1, d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }
    std::vector<unsigned int> perimeter;
    for (int i = 0; i < grid; ++i) perimeter.push_back(base + i);
    for (int i = 0; i < grid; ++i) perimeter.push_back(base + i * (grid + 1) + grid);
    for (int i = grid; i > 0; --i) perimeter.push_back(base + grid * (grid + 1) + i);
    for (int i = grid; i > 0; --i) perimeter.push_back(base + i * (grid + 1));
    const unsigned int skirtBase = static_cast<unsigned int>(mesh.vertices.size());
    for (unsigned int i = 0; i < perimeter.size(); ++i) {
        Vertex top = mesh.vertices[perimeter[i]];
        const auto& previous = mesh.vertices[perimeter[(i + perimeter.size() - 1) % perimeter.size()]].Position;
        const auto& next = mesh.vertices[perimeter[(i + 1) % perimeter.size()]].Position;
        if (std::abs(top.Position.x) >= mSize / 2 - 0.001f || std::abs(top.Position.z) >= mSize / 2 - 0.001f) {
            XMStoreFloat3(&top.Normal, XMVector3Normalize(XMVectorSet(next.z - previous.z, 0, previous.x - next.x, 0)));
            top.Tangent = {0, -1, 0};
        }
        mesh.vertices.push_back(top);
        top.Position.y = mSkirtBottom;
        mesh.vertices.push_back(top);
    }
    for (unsigned int i = 0; i < perimeter.size(); ++i) {
        unsigned int next = (i + 1) % static_cast<unsigned int>(perimeter.size());
        const auto a = skirtBase + i * 2, b = skirtBase + next * 2;
        mesh.indices.insert(mesh.indices.end(), {a, b, a + 1, b, b + 1, a + 1});
    }
    submesh.indexCount = static_cast<unsigned int>(mesh.indices.size()) - submesh.startIndexLocation;
    auto& node = mNodes[index];
    node.submesh = static_cast<unsigned int>(mesh.submeshes.size());
    node.level = level;
    node.x = x;
    node.z = z;
    node.cells = cells;
    node.vertexStart = base;
    node.vertexCount = static_cast<unsigned int>(mesh.vertices.size()) - base;
    node.surfaceBounds = BoundingBox({(x + cells * 0.5f) * spacing - mSize / 2, (low + high) / 2,
        (z + cells * 0.5f) * spacing - mSize / 2}, {cells * spacing / 2, (high - low) / 2, cells * spacing / 2});
    node.bounds = node.surfaceBounds;
    node.bounds.Center.y = (mSkirtBottom + high) / 2;
    node.bounds.Extents.y = (high - mSkirtBottom) / 2;
    mesh.submeshes.push_back(submesh);
    if (level < 4) {
        for (int child = 0; child < 4; ++child) {
            const int childIndex = Build(mesh, x + (child & 1) * cells / 2, z + (child >> 1) * cells / 2, cells / 2, level + 1);
            mNodes[index].children[child] = childIndex;
            BoundingBox::CreateMerged(mNodes[index].bounds, mNodes[index].bounds, mNodes[childIndex].bounds);
        }
    }
    return index;
}

void Terrain::SetBrushRegion(const XMFLOAT3& center, float radius) {
    mBrushCenter = center;
    mBrushRadius = std::isfinite(center.x) && std::isfinite(center.z) && std::isfinite(radius) && radius > 0 ? radius : 0;
}

std::vector<unsigned int> Terrain::Select(const BoundingFrustum& frustum, const XMFLOAT3& eye,
    bool culling, bool lod) const {
    std::vector<unsigned int> selected;
    mCulled = 0;
    auto collapse = [&](auto&& self, int index) -> void {
        mSplit[index] = false;
        for (int child : mNodes[index].children) if (child >= 0) self(self, child);
    };
    auto choose = [&](auto&& self, int index) -> void {
        const auto& node = mNodes[index];
        const auto& b = node.surfaceBounds;
        const float dx = std::max(0.0f, std::abs(eye.x - b.Center.x) - b.Extents.x);
        const float dy = eye.y - mLodHeight;
        const float dz = std::max(0.0f, std::abs(eye.z - b.Center.z) - b.Extents.z);
        const float distance = std::max(1.0f, std::sqrt(dx * dx + dy * dy + dz * dz));
        const float splitDistance = node.cells * mSize / (mResolution - 1) * 2.0f;
        const bool distanceSplit = node.children[0] >= 0 && (!lod || distance < splitDistance * (mSplit[index] ? 1.2f : 1.0f));
        const float brushX = std::max(0.0f, std::abs(mBrushCenter.x - b.Center.x) - b.Extents.x);
        const float brushZ = std::max(0.0f, std::abs(mBrushCenter.z - b.Center.z) - b.Extents.z);
        const bool divide = distanceSplit || (node.children[0] >= 0 && mBrushRadius > 0 && brushX * brushX + brushZ * brushZ <= mBrushRadius * mBrushRadius);
        if (lod) mSplit[index] = distanceSplit;
        if (divide) {
            for (int child : node.children) self(self, child);
        } else {
            if (lod) collapse(collapse, index);
            selected.push_back(node.submesh);
        }
    };
    if (mNodes.empty()) return selected;
    choose(choose, 0);
    for (;;) {
        std::array<int, 256> owners;
        owners.fill(-1);
        for (auto index : selected) {
            const auto& node = mNodes[index];
            const int size = node.cells * 16 / (mResolution - 1);
            const int x = node.x * 16 / (mResolution - 1), z = node.z * 16 / (mResolution - 1);
            for (int row = z; row < z + size; ++row)
                for (int col = x; col < x + size; ++col) owners[row * 16 + col] = static_cast<int>(index);
        }
        std::vector<bool> refine(mNodes.size(), false);
        bool changed = false;
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                const int a = owners[z * 16 + x];
                for (int b : {x < 15 ? owners[z * 16 + x + 1] : a, z < 15 ? owners[(z + 1) * 16 + x] : a}) {
                    const int difference = int(mNodes[a].level) - int(mNodes[b].level);
                    if (std::abs(difference) > 1) {
                        refine[difference < 0 ? a : b] = true;
                        changed = true;
                    }
                }
            }
        }
        if (!changed) break;
        std::vector<unsigned int> balanced;
        for (auto index : selected) {
            if (refine[index]) {
                for (int child : mNodes[index].children) balanced.push_back(static_cast<unsigned int>(child));
            } else balanced.push_back(index);
        }
        selected.swap(balanced);
    }
    std::vector<bool> chosen(mNodes.size(), false);
    for (auto index : selected) chosen[index] = true;
    selected.clear();
    auto visible = [&](auto&& self, int index) -> void {
        const auto& node = mNodes[index];
        if (culling && frustum.Contains(node.bounds) == DISJOINT) {
            ++mCulled;
            return;
        }
        if (chosen[index]) selected.push_back(node.submesh);
        else for (int child : node.children) if (child >= 0) self(self, child);
    };
    visible(visible, 0);
    return selected;
}
