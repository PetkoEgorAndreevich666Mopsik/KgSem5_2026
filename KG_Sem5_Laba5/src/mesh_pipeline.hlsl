#define MESH_PIPELINE
#include "main_shader.hlsl"
#include "../h/MeshletLimits.h"

struct Meshlet {
    uint VertexOffset, VertexCount;
    uint TriangleOffset, TriangleCount;
    float4 Sphere;
};

StructuredBuffer<VSInput> gVertices : register(t3);
StructuredBuffer<Meshlet> gMeshlets : register(t4);
StructuredBuffer<uint> gMeshletVertices : register(t5);
StructuredBuffer<uint> gMeshletTriangles : register(t6);
cbuffer MeshDispatch : register(b2) {
    uint gFirstMeshlet;
    uint gMeshletCount;
    uint gCullMeshlets;
};

struct MeshPayload {
    uint MeshletIds[AMPLIFICATION_GROUP_SIZE];
};

groupshared MeshPayload gPayload;
groupshared uint gVisibleCount;

bool IntersectsFrustum(float4 sphere) {
    // D3D clip inequalities are -w <= x,y <= w and 0 <= z <= w.
    float4x4 columns = transpose(mul(gWorld, gWorldViewProj));
    float4 planes[6] = {
        columns[3] + columns[0], columns[3] - columns[0],
        columns[3] + columns[1], columns[3] - columns[1],
        columns[2], columns[3] - columns[2]
    };
    [unroll]
    for (uint i = 0; i < 6; ++i) {
        float4 plane = planes[i];
        if (dot(float4(sphere.xyz, 1.0f), plane) < -(sphere.w + 0.0001f) * length(plane.xyz))
            return false;
    }
    return true;
}

[numthreads(AMPLIFICATION_GROUP_SIZE, 1, 1)]
void AS_CullMeshlets(uint3 thread : SV_DispatchThreadID, uint lane : SV_GroupIndex) {
    if (lane == 0) gVisibleCount = 0;
    GroupMemoryBarrierWithGroupSync();

    if (thread.x < gMeshletCount) {
        uint meshletId = gFirstMeshlet + thread.x;
        if (gCullMeshlets == 0 || IntersectsFrustum(gMeshlets[meshletId].Sphere)) {
            uint slot;
            InterlockedAdd(gVisibleCount, 1, slot);
            gPayload.MeshletIds[slot] = meshletId;
        }
    }
    GroupMemoryBarrierWithGroupSync();
    DispatchMesh(gVisibleCount, 1, 1, gPayload);
}

[numthreads(MESHLET_MAX_VERTICES, 1, 1)]
[outputtopology("triangle")]
void MS_Geometry(uint3 group : SV_GroupID, uint lane : SV_GroupIndex,
                 in payload MeshPayload payload,
                 out vertices PixelIn vertices[MESHLET_MAX_VERTICES],
                 out indices uint3 triangles[MESHLET_MAX_TRIANGLES]) {
    uint meshletId = payload.MeshletIds[group.x];
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
