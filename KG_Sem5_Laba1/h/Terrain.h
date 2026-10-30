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
        unsigned int vertexStart = 0;
        unsigned int vertexCount = 0;
    };
    static void Generate(const std::string& directory);
    void Load(const std::string& directory, MeshData& mesh);
    void BuildMesh(MeshData& mesh);
    bool Raycast(const DirectX::XMFLOAT3& origin, const DirectX::XMFLOAT3& direction, DirectX::XMFLOAT3& hit) const;
    std::vector<unsigned int> ApplyBrush(const DirectX::XMFLOAT3& center, float radius, float amount, MeshData& mesh);
    void SetBrushRegion(const DirectX::XMFLOAT3& center, float radius);
    std::vector<unsigned int> Select(const DirectX::BoundingFrustum& frustum,
        const DirectX::XMFLOAT3& eye, bool culling, bool lod) const;
    const std::vector<Node>& Nodes() const { return mNodes; }
    unsigned int Culled() const { return mCulled; }
private:
    float Height(int x, int z) const;
    DirectX::XMFLOAT3 Normal(int x, int z, const std::vector<float>& heights) const;
    int Build(MeshData& mesh, int x, int z, int cells, unsigned int level);
    int mResolution = 0;
    float mSize = 0;
    float mSkirtBottom = 0;
    float mLodHeight = 0;
    DirectX::XMFLOAT3 mBrushCenter = {0, 0, 0};
    float mBrushRadius = 0;
    std::vector<float> mHeights;
    std::vector<float> mOriginalHeights;
    std::vector<Node> mNodes;
    mutable unsigned int mCulled = 0;
    mutable std::vector<bool> mSplit;
};
