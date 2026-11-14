#pragma once

#include "GBuffer.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <memory>

class RenderingSystem {
public:
    bool Initialize(ID3D12Device* device,
                    unsigned int width,
                    unsigned int height,
                    DXGI_FORMAT backBufferFormat,
                    DXGI_FORMAT depthStencilFormat);

    void OnResize(ID3D12Device* device, unsigned int width, unsigned int height);

    ID3D12RootSignature* GetGeometryRootSignature() const { return mGeometryRootSignature.Get(); }
    ID3D12RootSignature* GetLightingRootSignature() const { return mLightingRootSignature.Get(); }
    ID3D12RootSignature* GetShadowRootSignature() const { return mShadowRootSignature.Get(); }
    ID3D12RootSignature* GetParticleRootSignature() const { return mParticleRootSignature.Get(); }
    ID3D12RootSignature* GetPostRootSignature() const { return mPostRootSignature.Get(); }

    ID3D12PipelineState* GetGeometryPSO() const { return mGeometryPSO.Get(); }
    ID3D12PipelineState* GetGeometryWirePSO() const { return mGeometryWirePSO.Get(); }
    ID3D12PipelineState* GetTessellationPSO() const { return mTessellationPSO.Get(); }
    ID3D12PipelineState* GetTessellationWirePSO() const { return mTessellationWirePSO.Get(); }
    ID3D12PipelineState* GetLightingPSO() const { return mLightingPSO.Get(); }
    ID3D12PipelineState* GetShadowPSO() const { return mShadowPSO.Get(); }
    ID3D12PipelineState* GetParticleComputePSO() const { return mParticleComputePSO.Get(); }
    ID3D12PipelineState* GetParticlePSO() const { return mParticleGraphicsPSO.Get(); }
    ID3D12PipelineState* GetPostPSO() const { return mPostPSO.Get(); }

    GBuffer* GetGBuffer() const { return mGBuffer.get(); }

private:
    void BuildRootSignatures(ID3D12Device* device);
    void BuildPSOs(ID3D12Device* device);

private:
    unsigned int mWidth = 1;
    unsigned int mHeight = 1;

    DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> mGeometryRootSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mLightingRootSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mShadowRootSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mParticleRootSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> mPostRootSignature;

    Microsoft::WRL::ComPtr<ID3D12PipelineState> mGeometryPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mGeometryWirePSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mTessellationPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mTessellationWirePSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mLightingPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mShadowPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mParticleComputePSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mParticleGraphicsPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> mPostPSO;

    std::unique_ptr<GBuffer> mGBuffer;
};
