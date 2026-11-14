#pragma once

#include "Vertex.h"

#include <string>
#include <vector>

struct SubmeshMaterial {
    std::string diffuseTextureName;
    std::string normalTextureName;
    std::string displacementTextureName;
    float shininess = 32.0f;
    float metallic = 0.0f;

    unsigned int diffuseSrvHeapIndex = 0;
    unsigned int normalSrvHeapIndex = 0;
    unsigned int displacementSrvHeapIndex = 0;
};

struct Submesh {
    unsigned int indexCount = 0;
    unsigned int startIndexLocation = 0;
    int baseVertexLocation = 0;
    SubmeshMaterial material;
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Submesh> submeshes;
};
