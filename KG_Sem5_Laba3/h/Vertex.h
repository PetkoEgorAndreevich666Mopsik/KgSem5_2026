#pragma once

#include <DirectXMath.h>

using namespace DirectX;

struct Vertex {
    XMFLOAT3 Position = {0.0f, 0.0f, 0.0f};
    XMFLOAT3 Normal = {0.0f, 1.0f, 0.0f};
    XMFLOAT3 Tangent = {1.0f, 0.0f, 0.0f};
    XMFLOAT3 Bitangent = {0.0f, 0.0f, 1.0f};
    XMFLOAT2 TexC = {0.0f, 0.0f};
};
