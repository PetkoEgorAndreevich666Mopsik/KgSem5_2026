#pragma once

#include "d3dUtil.h"
#include "../h/d3dx12.h"

#include <wrl.h>
#include <cstring>
#include <d3d12.h>

using Microsoft::WRL::ComPtr;

template<typename T>
class UploadBuffer {
public:
    UploadBuffer(ID3D12Device* device, unsigned int elementCount, bool isConstantBuffer)
        : mIsConstantBuffer(isConstantBuffer) {
        mElementByteSize = sizeof(T);

        if (isConstantBuffer) {
            mElementByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(T));
        }

        ThrowIfFailed(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(mElementByteSize) * elementCount),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&mUploadBuffer)),
            "Create upload buffer failed");

        ThrowIfFailed(mUploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mMappedData)),
                      "Map upload buffer failed");
    }

    UploadBuffer(const UploadBuffer&) = delete;
    UploadBuffer& operator=(const UploadBuffer&) = delete;

    ~UploadBuffer() {
        if (mUploadBuffer) {
            mUploadBuffer->Unmap(0, nullptr);
        }
        mMappedData = nullptr;
    }

    ID3D12Resource* Resource() const {
        return mUploadBuffer.Get();
    }

    unsigned int GetElementSize() const {
        return mElementByteSize;
    }

    void CopyData(int elementIndex, const T& data) {
        memcpy(&mMappedData[elementIndex * mElementByteSize], &data, sizeof(T));
    }

private:
    ComPtr<ID3D12Resource> mUploadBuffer;
    unsigned char* mMappedData = nullptr;

    unsigned int mElementByteSize = 0;
    bool mIsConstantBuffer = false;
};
