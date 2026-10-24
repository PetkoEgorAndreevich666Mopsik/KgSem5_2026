#pragma once
#include "mesh_data.h"
#include <DirectXCollision.h>
#include <array>
#include <string>

class Terrain {
public:
    struct Node {
        DirectX::BoundingBox bounds;
        DirectX::BoundingBox surfaceBounds;
        std::array<int, 4> children = {-1, -1, -1, -1};
        unsigned int submesh = 0;
        unsigned int level = 0;
        int x = 0;
        int z = 0;
        int cells = 0;
    };
    static void Generate(const std::string& directory);
    void Load(const std::string& directory, MeshData& mesh);
    std::vector<unsigned int> Select(const DirectX::BoundingFrustum& frustum,
        const DirectX::XMFLOAT3& eye, bool culling, bool lod) const;
    const std::vector<Node>& Nodes() const { return mNodes; }
    unsigned int Culled() const { return mCulled; }
private:
    float Height(int x, int z) const;
    int Build(MeshData& mesh, int x, int z, int cells, unsigned int level);
    int mResolution = 0;
    float mSize = 0;
    float mSkirtBottom = 0;
    std::vector<float> mHeights;
    std::vector<Node> mNodes;
    mutable unsigned int mCulled = 0;
    mutable std::vector<bool> mSplit;
};
