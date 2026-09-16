#pragma once

#include "Light.h"
#include "UploadBuffer.h"
#include "mesh_data.h"
#include "RenderingSystem.h"

#include <array>
#include <DirectXCollision.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <string>
#include <unordered_map>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct ObjectConstants {
    XMFLOAT4X4 WorldViewProj = {};
    XMFLOAT4X4 World = {};
    XMFLOAT4X4 TextureTransform = {};
    float TotalTime = 0.0f;
    XMFLOAT3 TimePadding = {0.0f, 0.0f, 0.0f};
    XMFLOAT4 Params = {0.0f, 0.0f, 0.0f, 0.0f};
};

struct PassConstants {
    XMFLOAT4X4 InvViewProj = {};
    XMFLOAT3 EyePosW = {0.0f, 0.0f, 0.0f};
    float Padding = 0.0f;
    XMFLOAT4 AmbientColor = {0.08f, 0.08f, 0.1f, 1.0f};
};

class GameTimer;

class DirectXApp {
public:
    DirectXApp();
    ~DirectXApp();

    bool Initialize(HWND hwnd, unsigned int width, unsigned int height);

    void OnResize(unsigned int width, unsigned int height);
    void Update(const GameTimer& gt);
    void Draw(const GameTimer& gt);

    LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    bool InitDirect3D();
    void CreateCommandObjects();
    void CreateSwapChain();
    void CreateRtvAndDsvDescriptorHeaps();
    void CreateRenderTargetViews();
    void CreateDepthStencilBuffer();

    void BuildScene();
    void LoadModels();
    void BuildGeometryBuffers();
    void LoadTextures();
    void CreateFallbackTextures();
    void BindSubmeshTextures();

    void BuildConstantBuffers();
    void BuildMainSrvHeap();
    void BuildLights();
    void UpdateFallingLights(float dt);
    void CreateShadowResources();

    void UpdateCamera(float dt);
    void BuildSceneObjects();
    void BuildOctree();
    void QueryOctree(const BoundingFrustum& frustum, std::vector<unsigned int>& visible) const;
    bool IsObjectVisible(unsigned int objectIndex, const BoundingFrustum& frustum) const;

    void FlushCommandQueue();

    D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
    D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;
    ID3D12Resource* CurrentBackBuffer() const;

    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuSrvHandle(unsigned int heapIndex) const;
    void TransitionResource(ID3D12GraphicsCommandList* cmdList,
                            ID3D12Resource* resource,
                            D3D12_RESOURCE_STATES& currentState,
                            D3D12_RESOURCE_STATES targetState);

private:
    struct TextureResource {
        std::wstring path;
        ComPtr<ID3D12Resource> resource;
        ComPtr<ID3D12Resource> uploadHeap;
        unsigned int srvHeapIndex = 0;
    };

    struct FallingLight {
        XMFLOAT3 position = {0.0f, 5.0f, 0.0f};
        XMFLOAT3 velocity = {0.0f, 0.0f, 0.0f};
        XMFLOAT3 color = {1.0f, 0.7f, 0.3f};
        float intensity = 16.0f;
        float range = 10.0f;
        bool settled = false;
    };

    struct SceneObject {
        XMFLOAT4X4 world = {};
        BoundingSphere bounds = {};
    };

    struct OctreeNode {
        BoundingBox bounds = {};
        std::vector<unsigned int> objectIndices;
        std::array<int, 8> children = {-1, -1, -1, -1, -1, -1, -1, -1};
    };

private:
    static constexpr unsigned int SwapChainBufferCount = 2;
    static constexpr unsigned int LightingCbElementCount = 2048;
    static constexpr unsigned int SceneObjectCount = 400;
    static constexpr unsigned int MaxRenderedObjectCount = SceneObjectCount;
    static constexpr unsigned int OctreeMaxDepth = 5;
    static constexpr unsigned int OctreeLeafCapacity = 32;
    static constexpr float CameraNearZ = 0.1f;
    static constexpr float CameraFarZ = 260.0f;

    HWND mHwnd = nullptr;
    unsigned int mClientWidth = 1280;
    unsigned int mClientHeight = 720;

    ComPtr<IDXGIFactory6> mDxgiFactory;
    ComPtr<IDXGISwapChain> mSwapChain;
    ComPtr<ID3D12Device> mDevice;

    ComPtr<ID3D12Fence> mFence;
    unsigned long long mCurrentFence = 0;
    bool mComInitialized = false;

    ComPtr<ID3D12CommandQueue> mCommandQueue;
    ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc;
    ComPtr<ID3D12GraphicsCommandList> mCommandList;

    std::array<ComPtr<ID3D12Resource>, SwapChainBufferCount> mSwapChainBuffer;
    ComPtr<ID3D12Resource> mDepthStencilBuffer;

    ComPtr<ID3D12DescriptorHeap> mRtvHeap;
    ComPtr<ID3D12DescriptorHeap> mDsvHeap;
    ComPtr<ID3D12DescriptorHeap> mCbvSrvHeap;

    unsigned int mRtvDescriptorSize = 0;
    unsigned int mDsvDescriptorSize = 0;
    unsigned int mCbvSrvUavDescriptorSize = 0;

    DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

    D3D12_VIEWPORT mScreenViewport = {};
    D3D12_RECT mScissorRect = {};

    unsigned int mCurrBackBuffer = 0;

    std::unique_ptr<RenderingSystem> mRenderingSystem;

    MeshData mSceneMesh;
    MeshData mParticleMesh;
    BoundingSphere mModelBounds = {};
    std::vector<SceneObject> mSceneObjects;
    std::vector<OctreeNode> mOctreeNodes;
    std::vector<unsigned int> mVisibleObjectIndices;
    ComPtr<ID3D12Resource> mVertexBufferGPU;
    ComPtr<ID3D12Resource> mVertexBufferUploader;
    ComPtr<ID3D12Resource> mIndexBufferGPU;
    ComPtr<ID3D12Resource> mIndexBufferUploader;
    ComPtr<ID3D12Resource> mParticleVertexBufferGPU;
    ComPtr<ID3D12Resource> mParticleVertexBufferUploader;
    ComPtr<ID3D12Resource> mParticleIndexBufferGPU;
    ComPtr<ID3D12Resource> mParticleIndexBufferUploader;

    D3D12_VERTEX_BUFFER_VIEW mVertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW mIndexBufferView = {};
    D3D12_VERTEX_BUFFER_VIEW mParticleVertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW mParticleIndexBufferView = {};
    unsigned int mParticleMeshIndexCount = 0;

    std::vector<TextureResource> mTextureResources;
    std::unordered_map<std::string, unsigned int> mTextureNameToIndex;

    unsigned int mFallbackDiffuseIndex = 0;
    unsigned int mFallbackNormalIndex = 0;
    unsigned int mFallbackDisplacementIndex = 0;

    unsigned int mTextureSrvStart = 3;
    unsigned int mGBufferSrvStart = 0;

    std::unique_ptr<UploadBuffer<ObjectConstants>> mObjectCB;
    std::unique_ptr<UploadBuffer<PassConstants>> mPassCB;
    std::unique_ptr<UploadBuffer<LightingConstants>> mLightingCB;
    std::unique_ptr<UploadBuffer<ShadowConstants>> mShadowCB;

    std::vector<LightData> mLights;
    std::vector<FallingLight> mFallingLights;

    bool mFallingBallsEnabled = false;
    float mFallingSpawnTimer = 0.0f;
    float mFallingSpawnInterval = 0.02f;
    unsigned int mMaxFallingLights = 50;

    XMFLOAT3 mEyePos = {0.0f, 2.0f, -12.0f};
    float mYaw = 0.0f;
    float mPitch = 0.0f;
    POINT mLastMousePos = {0, 0};

    int mDebugViewMode = 1;
    bool mF1WasDown = false;
    bool mF2WasDown = false;
    bool mF3WasDown = false;
    bool mBWasDown = false;
    bool mTWasDown = false;
    bool mRWasDown = false;
    bool mCWasDown = false;
    bool mOWasDown = false;
    bool mFrustumCullingEnabled = true;
    bool mOctreeCullingEnabled = true;
    unsigned int mLastVisibleObjectCount = 0;
    unsigned int mLastDrawCallCount = 0;
    bool mAnimateTextures = false;
    float mTexAnimU = 0.0f;
    float mTexAnimV = 0.0f;
    float mTexScaleU = 1.0f;
    float mTexScaleV = 1.0f;
    int mLastTitleMode = -1;
    float mFpsTimer = 0.0f;
    unsigned int mFrameCount = 0;
    float mCurrentFps = 0.0f;

    ComPtr<ID3D12Resource> mShadowMap;
    ComPtr<ID3D12DescriptorHeap> mShadowDsvHeap;
    unsigned int mShadowSrvIndex = 0;
    D3D12_VIEWPORT mShadowViewport = {};
    D3D12_RECT mShadowScissor = {};
    bool mShadowMapStateIsSrv = false;
    static constexpr unsigned int kShadowMapSize = 1024;

    XMFLOAT4X4 mCascadeViewProj[3] = {};

    int mCurrentScene = 0;
    bool mNum1WasDown = false;
    bool mNum2WasDown = false;

    struct ParticleGpu {
        XMFLOAT3 Position = {0.0f, 0.0f, 0.0f};
        float Size = 0.16f;
        XMFLOAT3 Velocity = {0.0f, 0.0f, 0.0f};
        float Age = 0.0f;
        XMFLOAT4 Color = {1.0f, 1.0f, 1.0f, 1.0f};
        float Lifetime = 1.0f;
        XMFLOAT3 Padding = {0.0f, 0.0f, 0.0f};
    };

    struct ParticleSimConstants {
        float Dt = 0.0f;
        float TotalTime = 0.0f;
        unsigned int AliveCount = 0;
        unsigned int SpawnCount = 0;
        XMFLOAT3 EmitterPos = {0.0f, 0.0f, 0.0f};
        float BaseSize = 0.16f;
        XMFLOAT3 EmitterVelocity = {0.0f, 0.0f, 0.0f};
        float Gravity = 9.8f;
        float LifeMin = 1.0f;
        float LifeMax = 3.0f;
        float SpeedMin = 5.0f;
        float SpeedMax = 12.0f;
        unsigned int MaxParticles = 2048;
        XMFLOAT3 Pad0 = {0.0f, 0.0f, 0.0f};
        XMFLOAT3 CollisionCenter = {0.0f, 0.0f, 0.0f};
        float CollisionRadius = 0.0f;
        float Restitution = 0.5f;
        XMFLOAT3 Pad1 = {0.0f, 0.0f, 0.0f};
    };

    struct ParticleRenderConstants {
        XMFLOAT4X4 ViewProj = {};
        XMFLOAT3 CameraRight = {1.0f, 0.0f, 0.0f};
        float RenderSizeScale = 1.0f;
        XMFLOAT3 CameraUp = {0.0f, 1.0f, 0.0f};
        float AlphaDiscard = 0.5f;
    };

    void InitializeParticleResources();
    void UpdateParticles(float dt, float totalTime);
    void ExecuteParticleSimulation(ID3D12GraphicsCommandList* cmdList);
    void DrawParticles(ID3D12GraphicsCommandList* cmdList, const XMMATRIX& view, const XMMATRIX& proj);
    void UpdateParticleCounterFromReadback();
    void ResetParticles();

    static constexpr unsigned int kMaxParticles = 2048;
    std::array<ComPtr<ID3D12Resource>, 2> mParticleBuffers;
    std::array<ComPtr<ID3D12Resource>, 2> mParticleCounters;
    std::array<D3D12_RESOURCE_STATES, 2> mParticleBufferStates = {
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON
    };
    std::array<D3D12_RESOURCE_STATES, 2> mParticleCounterStates = {
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COMMON
    };
    ComPtr<ID3D12Resource> mParticleCounterReadback;
    ComPtr<ID3D12Resource> mParticleCounterResetUpload;
    std::unique_ptr<UploadBuffer<ParticleSimConstants>> mParticleSimCB;
    std::unique_ptr<UploadBuffer<ParticleRenderConstants>> mParticleRenderCB;
    unsigned int mParticleSrvIndex = 0;
    unsigned int mParticleUavStartIndex = 0;
    bool mParticleSourceIndexA = true;
    unsigned int mParticleAliveCount = 0;
    unsigned int mParticleSpawnCount = 0;
    float mParticleSpawnAccumulator = 0.0f;
    bool mParticleReadbackValid = false;
    bool mParticlesEnabled = false;
    bool mPWasDown = false;
};
