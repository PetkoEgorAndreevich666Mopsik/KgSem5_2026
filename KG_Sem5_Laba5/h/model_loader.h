#pragma once

#include "mesh_data.h"

#include <DirectXMath.h>
#include <string>

class ModelLoader {
public:
    static MeshData LoadModel(const std::string& filePath, const DirectX::XMMATRIX& world = DirectX::XMMatrixIdentity());
    static MeshData LoadModelFromMemory(const void* data, size_t size, const char* format);
};
