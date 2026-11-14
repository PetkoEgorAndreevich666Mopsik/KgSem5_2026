#define MESH_PIPELINE
#include "main_shader.hlsl"
#include "../h/MeshletLimits.h"

struct Meshlet {
    uint VertexOffset, VertexCount;
    uint TriangleOffset, TriangleCount;
};

StructuredBuffer<VSInput> gVertices : register(t3);
StructuredBuffer<Meshlet> gMeshlets : register(t4);
StructuredBuffer<uint> gMeshletVertices : register(t5);
StructuredBuffer<uint> gMeshletTriangles : register(t6);
cbuffer MeshDispatch : register(b2) {
    uint gFirstMeshlet;
};

[numthreads(MESHLET_MAX_VERTICES, 1, 1)]
[outputtopology("triangle")]
void MS_Geometry(uint3 group : SV_GroupID, uint lane : SV_GroupIndex,
                 out vertices PixelIn vertices[MESHLET_MAX_VERTICES],
                 out indices uint3 triangles[MESHLET_MAX_TRIANGLES]) {
    uint meshletId = gFirstMeshlet + group.x;
    Meshlet meshlet = gMeshlets[meshletId];
    SetMeshOutputCounts(meshlet.VertexCount, meshlet.TriangleCount);

    if (lane < meshlet.VertexCount) {
        uint vertexId = gMeshletVertices[meshlet.VertexOffset + lane];
        PixelIn vertex = VS_Geometry(gVertices[vertexId]);
        vertex.MeshletId = meshletId;
        vertices[lane] = vertex;
    }
    for (uint i = lane; i < meshlet.TriangleCount; i += MESHLET_MAX_VERTICES) {
        uint packed = gMeshletTriangles[meshlet.TriangleOffset + i];
        triangles[i] = uint3(packed & 255u, (packed >> 8) & 255u, (packed >> 16) & 255u);
    }
}
