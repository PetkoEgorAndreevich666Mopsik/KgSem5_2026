#include "model_loader.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <filesystem>
#include <stdexcept>

using namespace DirectX;

namespace {
XMMATRIX AiToXm(const aiMatrix4x4& m) {
    return XMMATRIX(
        m.a1, m.a2, m.a3, m.a4,
        m.b1, m.b2, m.b3, m.b4,
        m.c1, m.c2, m.c3, m.c4,
        m.d1, m.d2, m.d3, m.d4);
}

std::string StemFromAssimpTexPath(const aiString& path) {
    std::filesystem::path p(path.C_Str());
    return p.stem().string();
}

void ParseNode(const aiScene* scene,
               const aiNode* node,
               const XMMATRIX& parent,
               MeshData& dst) {
    // DirectXMath uses row-vectors (v * M), so local transform must be applied before parent.
    const XMMATRIX nodeTransform = AiToXm(node->mTransformation) * parent;
    const XMMATRIX normalMat = XMMatrixTranspose(XMMatrixInverse(nullptr, nodeTransform));

    for (unsigned int n = 0; n < node->mNumMeshes; ++n) {
        const aiMesh* mesh = scene->mMeshes[node->mMeshes[n]];

        const unsigned int baseVertex = static_cast<unsigned int>(dst.vertices.size());
        const unsigned int startIndex = static_cast<unsigned int>(dst.indices.size());

        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            Vertex v;

            XMVECTOR p = XMVectorSet(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z, 1.0f);
            p = XMVector3TransformCoord(p, nodeTransform);
            XMStoreFloat3(&v.Position, p);

            if (mesh->HasNormals()) {
                XMVECTOR normal = XMVectorSet(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z, 0.0f);
                normal = XMVector3Normalize(XMVector3TransformNormal(normal, normalMat));
                XMStoreFloat3(&v.Normal, normal);
            }

            if (mesh->HasTangentsAndBitangents()) {
                XMVECTOR tangent = XMVectorSet(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z, 0.0f);
                tangent = XMVector3Normalize(XMVector3TransformNormal(tangent, normalMat));
                XMStoreFloat3(&v.Tangent, tangent);

                XMVECTOR bitangent = XMVectorSet(mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z, 0.0f);
                bitangent = XMVector3Normalize(XMVector3TransformNormal(bitangent, normalMat));
                XMStoreFloat3(&v.Bitangent, bitangent);
            }

            if (mesh->HasTextureCoords(0)) {
                v.TexC.x = mesh->mTextureCoords[0][i].x;
                v.TexC.y = 1.0f - mesh->mTextureCoords[0][i].y;
            }

            dst.vertices.push_back(v);
        }

        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            for (unsigned int k = 0; k < face.mNumIndices; ++k) {
                dst.indices.push_back(baseVertex + face.mIndices[k]);
            }
        }

        Submesh sub;
        sub.indexCount = mesh->mNumFaces * 3;
        sub.startIndexLocation = startIndex;
        sub.baseVertexLocation = 0;

        if (mesh->mMaterialIndex < scene->mNumMaterials) {
            aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
            aiString tex;

            if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &tex) == AI_SUCCESS) {
                sub.material.diffuseTextureName = StemFromAssimpTexPath(tex);
            }
            if (mat->GetTexture(aiTextureType_NORMALS, 0, &tex) == AI_SUCCESS ||
                mat->GetTexture(aiTextureType_HEIGHT, 0, &tex) == AI_SUCCESS) {
                sub.material.normalTextureName = StemFromAssimpTexPath(tex);
            }
            if (mat->GetTexture(aiTextureType_DISPLACEMENT, 0, &tex) == AI_SUCCESS ||
                mat->GetTexture(aiTextureType_HEIGHT, 0, &tex) == AI_SUCCESS) {
                const auto name = StemFromAssimpTexPath(tex);
                if (name.find("height") != std::string::npos || name.find("HEIGHT") != std::string::npos) {
                    sub.material.displacementTextureName = name;
                }
            }

            mat->Get(AI_MATKEY_METALLIC_FACTOR, sub.material.metallic);
            float shininess = 32.0f;
            if (mat->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS) {
                sub.material.shininess = shininess;
            }
        }

        dst.submeshes.push_back(sub);
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        ParseNode(scene, node->mChildren[i], nodeTransform, dst);
    }
}
} // namespace

MeshData ModelLoader::LoadModel(const std::string& filePath, const XMMATRIX& world) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        filePath,
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices);

    if (!scene || !scene->mRootNode) {
        throw std::runtime_error(importer.GetErrorString());
    }

    MeshData result;
    ParseNode(scene, scene->mRootNode, world, result);
    return result;
}
