#pragma once

#include "MeshletLimits.h"
#include "mesh_data.h"

#include <cstdint>

struct Meshlet {
    uint32_t vertexOffset, vertexCount;
    uint32_t triangleOffset, triangleCount;
};

struct MeshletRange {
    uint32_t first, count;
};

struct Meshlets {
    std::vector<Meshlet> blocks;
    std::vector<uint32_t> vertices;
    std::vector<uint32_t> triangles;
    std::vector<MeshletRange> submeshes;
};

Meshlets BuildMeshlets(const MeshData& mesh);
