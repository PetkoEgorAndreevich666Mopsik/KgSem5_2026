#include "DirectXApp.h"

#include "DDSTextureLoader.h"
#include "GameTimer.h"
#include "model_loader.h"
#include "d3dUtil.h"
#include "../h/d3dx12.h"

#include <Windows.h>
#include <windowsx.h>
#include <wincodec.h>
#include <algorithm>
#include <cmath>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <limits>
#include <random>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <unordered_set>
#include <vector>
#include <fstream>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace {
std::string ToLowerAscii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

ComPtr<ID3D12Resource> CreateDefaultBuffer(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const void* initData,
    UINT64 byteSize,
    ComPtr<ID3D12Resource>& uploadBuffer) {
    ComPtr<ID3D12Resource> defaultBuffer;

    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(byteSize),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&defaultBuffer)),
        "Create default buffer failed");

    ThrowIfFailed(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(byteSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer)),
        "Create upload buffer failed");

    D3D12_SUBRESOURCE_DATA subData = {};
    subData.pData = initData;
    subData.RowPitch = byteSize;
    subData.SlicePitch = subData.RowPitch;

    auto toCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
        defaultBuffer.Get(),
        D3D12_RESOURCE_STATE_COMMON,
        D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->ResourceBarrier(1, &toCopyDest);

    UpdateSubresources<1>(cmdList, defaultBuffer.Get(), uploadBuffer.Get(), 0, 0, 1, &subData);

    auto toRead = CD3DX12_RESOURCE_BARRIER::Transition(
        defaultBuffer.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_GENERIC_READ);
    cmdList->ResourceBarrier(1, &toRead);

    return defaultBuffer;
}

bool LoadTgaTexture12(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const std::wstring& filePath,
    ComPtr<ID3D12Resource>& texture,
    ComPtr<ID3D12Resource>& uploadHeap) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) return false;

    unsigned char header[18];
    file.read(reinterpret_cast<char*>(header), 18);
    if (!file.good()) return false;

    int idLen = header[0];
    int width = header[12] | (header[13] << 8);
    int height = header[14] | (header[15] << 8);
    int bpp = header[16];
    int descriptor = header[17];
    int imgType = header[2];

    if (width <= 0 || height <= 0 || (bpp != 24 && bpp != 32)) return false;
    if (imgType != 2 && imgType != 10) return false;

    file.seekg(18 + idLen, std::ios::beg);

    std::vector<unsigned char> pixels(width * height * 4);
    int bytesPerPixel = bpp / 8;
    int rowSize = width * bytesPerPixel;

    if (imgType == 2) {
        std::vector<unsigned char> rowBuf(rowSize);
        for (int y = 0; y < height; ++y) {
            file.read(reinterpret_cast<char*>(rowBuf.data()), rowSize);
            for (int x = 0; x < width; ++x) {
                int srcIdx = x * bytesPerPixel;
                int dstIdx = (y * width + x) * 4;
                pixels[dstIdx + 0] = rowBuf[srcIdx + 2]; // R <- B
                pixels[dstIdx + 1] = rowBuf[srcIdx + 1]; // G
                pixels[dstIdx + 2] = rowBuf[srcIdx + 0]; // B <- R
                pixels[dstIdx + 3] = (bytesPerPixel == 4) ? rowBuf[srcIdx + 3] : 255;
            }
        }
    } else {
        int pixelCount = width * height;
        int curPixel = 0;
        std::vector<unsigned char> chunkHeader(1);
        std::vector<unsigned char> colorBuffer(bytesPerPixel);
        while (curPixel < pixelCount) {
            file.read(reinterpret_cast<char*>(chunkHeader.data()), 1);
            if (chunkHeader[0] < 128) {
                int runLength = chunkHeader[0] + 1;
                for (int i = 0; i < runLength; ++i) {
                    file.read(reinterpret_cast<char*>(colorBuffer.data()), bytesPerPixel);
                    int x = curPixel % width;
                    int y = curPixel / width;
                    int dstIdx = (y * width + x) * 4;
                    pixels[dstIdx + 0] = colorBuffer[2];
                    pixels[dstIdx + 1] = colorBuffer[1];
                    pixels[dstIdx + 2] = colorBuffer[0];
                    pixels[dstIdx + 3] = (bytesPerPixel == 4) ? colorBuffer[3] : 255;
                    curPixel++;
                }
            } else {
                int runLength = chunkHeader[0] - 127;
                file.read(reinterpret_cast<char*>(colorBuffer.data()), bytesPerPixel);
                for (int i = 0; i < runLength; ++i) {
                    int x = curPixel % width;
                    int y = curPixel / width;
                    int dstIdx = (y * width + x) * 4;
                    pixels[dstIdx + 0] = colorBuffer[2];
                    pixels[dstIdx + 1] = colorBuffer[1];
                    pixels[dstIdx + 2] = colorBuffer[0];
                    pixels[dstIdx + 3] = (bytesPerPixel == 4) ? colorBuffer[3] : 255;
                    curPixel++;
                }
            }
        }
    }

    if ((descriptor & 0x20) != 0) {
        for (int y = 0; y < height / 2; ++y) {
            for (int x = 0; x < width; ++x) {
                int i1 = (y * width + x) * 4;
                int i2 = ((height - 1 - y) * width + x) * 4;
                for (int c = 0; c < 4; ++c) {
                    std::swap(pixels[i1 + c], pixels[i2 + c]);
                }
            }
        }
    }

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = static_cast<UINT64>(width);
    texDesc.Height = static_cast<UINT>(height);
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture));
    if (FAILED(hr)) return false;

    UINT64 uploadBufferSize;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT numRows;
    UINT64 rowSizeInBytes;
    device->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &uploadBufferSize);

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = uploadBufferSize;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadHeap));
    if (FAILED(hr)) return false;

    void* mapped = nullptr;
    D3D12_RANGE readRange = {0, 0};
    uploadHeap->Map(0, &readRange, &mapped);
    BYTE* dest = static_cast<BYTE*>(mapped);
    for (int y = 0; y < height; ++y) {
        memcpy(dest + y * footprint.Footprint.RowPitch,
               pixels.data() + y * width * 4,
               width * 4);
    }
    uploadHeap->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = uploadHeap.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = texture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &toSrv);

    return true;
}

bool LoadWicTextureFromFile12(
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList,
    const std::wstring& filePath,
    ComPtr<ID3D12Resource>& texture,
    ComPtr<ID3D12Resource>& uploadHeap) {
    static ComPtr<IWICImagingFactory> wicFactory;
    if (!wicFactory) {
        HRESULT hr = CoCreateInstance(
            CLSID_WICImagingFactory2,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&wicFactory));
        if (FAILED(hr)) {
            hr = CoCreateInstance(
                CLSID_WICImagingFactory,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(&wicFactory));
        }
        if (FAILED(hr)) {
            return false;
        }
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wicFactory->CreateDecoderFromFilename(
        filePath.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &decoder))) {
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) {
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || width == 0 || height == 0) {
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wicFactory->CreateFormatConverter(&converter))) {
        return false;
    }

    if (FAILED(converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppRGBA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeCustom))) {
        return false;
    }

    const UINT rowPitch = width * 4u;
    const UINT imageSize = rowPitch * height;
    std::vector<unsigned char> pixels(imageSize);

    if (FAILED(converter->CopyPixels(nullptr, rowPitch, imageSize, pixels.data()))) {
        return false;
    }

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (FAILED(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture)))) {
        return false;
    }

    const UINT64 uploadSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);
    if (FAILED(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadHeap)))) {
        return false;
    }

    D3D12_SUBRESOURCE_DATA subresource = {};
    subresource.pData = pixels.data();
    subresource.RowPitch = rowPitch;
    subresource.SlicePitch = imageSize;

    UpdateSubresources(cmdList, texture.Get(), uploadHeap.Get(), 0, 0, 1, &subresource);

    auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &toSrv);

    return true;
}

}

DirectXApp::DirectXApp() = default;

DirectXApp::~DirectXApp() {
    if (mDevice) {
        FlushCommandQueue();
    }
    if (mComInitialized) {
        CoUninitialize();
        mComInitialized = false;
    }
}

bool DirectXApp::Initialize(HWND hwnd, unsigned int width, unsigned int height) {
    mHwnd = hwnd;
    mClientWidth = width;
    mClientHeight = height;

    if (!InitDirect3D()) {
        return false;
    }

    mRenderingSystem = std::make_unique<RenderingSystem>();
    if (!mRenderingSystem->Initialize(mDevice.Get(), mClientWidth, mClientHeight, mBackBufferFormat, mDepthStencilFormat)) {
        return false;
    }

    BuildScene();

    mScreenViewport.TopLeftX = 0.0f;
    mScreenViewport.TopLeftY = 0.0f;
    mScreenViewport.Width = static_cast<float>(mClientWidth);
    mScreenViewport.Height = static_cast<float>(mClientHeight);
    mScreenViewport.MinDepth = 0.0f;
    mScreenViewport.MaxDepth = 1.0f;

    mScissorRect = {0, 0, static_cast<LONG>(mClientWidth), static_cast<LONG>(mClientHeight)};

    return true;
}

bool DirectXApp::InitDirect3D() {
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(comHr)) {
        mComInitialized = true;
    } else if (comHr != RPC_E_CHANGED_MODE) {
        ThrowIfFailed(comHr, "CoInitializeEx failed");
    }

#if defined(_DEBUG)
    {
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
        }
    }
#endif

    ThrowIfFailed(CreateDXGIFactory2(0, IID_PPV_ARGS(&mDxgiFactory)), "CreateDXGIFactory2 failed");

    ComPtr<IDXGIAdapter1> hardwareAdapter;
    for (UINT adapterIndex = 0;
         mDxgiFactory->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                  IID_PPV_ARGS(&hardwareAdapter)) != DXGI_ERROR_NOT_FOUND;
         ++adapterIndex) {
        DXGI_ADAPTER_DESC1 desc = {};
        hardwareAdapter->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }

        if (SUCCEEDED(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr))) {
            break;
        }
        hardwareAdapter.Reset();
    }

    if (!hardwareAdapter) {
        ComPtr<IDXGIAdapter> warpAdapter;
        ThrowIfFailed(mDxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)), "EnumWarpAdapter failed");
        ThrowIfFailed(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&mDevice)),
                      "Create WARP device failed");
    } else {
        ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&mDevice)),
                      "Create hardware device failed");
    }

    ThrowIfFailed(mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)), "Create fence failed");

    mRtvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mDsvDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    mCbvSrvUavDescriptorSize = mDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CreateCommandObjects();
    CreateSwapChain();
    CreateRtvAndDsvDescriptorHeaps();
    CreateRenderTargetViews();
    CreateDepthStencilBuffer();

    return true;
}

void DirectXApp::CreateCommandObjects() {
    D3D12_COMMAND_QUEUE_DESC qdesc = {};
    qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qdesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    ThrowIfFailed(mDevice->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&mCommandQueue)), "Create command queue failed");

    ThrowIfFailed(mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&mDirectCmdListAlloc)),
                  "Create command allocator failed");

    ThrowIfFailed(mDevice->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        mDirectCmdListAlloc.Get(),
        nullptr,
        IID_PPV_ARGS(&mCommandList)),
        "Create command list failed");

    ThrowIfFailed(mCommandList->Close(), "Close command list failed");
}

void DirectXApp::CreateSwapChain() {
    mSwapChain.Reset();

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = mClientWidth;
    sd.BufferDesc.Height = mClientHeight;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferDesc.Format = mBackBufferFormat;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = SwapChainBufferCount;
    sd.OutputWindow = mHwnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    ThrowIfFailed(mDxgiFactory->CreateSwapChain(mCommandQueue.Get(), &sd, &mSwapChain),
                  "Create swap chain failed");
}

void DirectXApp::CreateRtvAndDsvDescriptorHeaps() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mRtvHeap)), "Create RTV heap failed");

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mDsvHeap)), "Create DSV heap failed");
}

void DirectXApp::CreateRenderTargetViews() {
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (unsigned int i = 0; i < SwapChainBufferCount; ++i) {
        ThrowIfFailed(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])), "Get swap chain buffer failed");
        mDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, mRtvDescriptorSize);
    }
}

void DirectXApp::CreateDepthStencilBuffer() {
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = mClientWidth;
    depthDesc.Height = mClientHeight;
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = mDepthStencilFormat;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = mDepthStencilFormat;
    clear.DepthStencil.Depth = 1.0f;
    clear.DepthStencil.Stencil = 0;

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear,
        IID_PPV_ARGS(&mDepthStencilBuffer)),
        "Create depth buffer failed");

    mDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), nullptr, DepthStencilView());
}

void DirectXApp::OnResize(unsigned int width, unsigned int height) {
    if (!mDevice || !mSwapChain) {
        return;
    }

    mClientWidth = (std::max)(1u, width);
    mClientHeight = (std::max)(1u, height);

    FlushCommandQueue();

    for (auto& buffer : mSwapChainBuffer) {
        buffer.Reset();
    }
    mDepthStencilBuffer.Reset();

    ThrowIfFailed(mSwapChain->ResizeBuffers(
        SwapChainBufferCount,
        mClientWidth,
        mClientHeight,
        mBackBufferFormat,
        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH),
        "ResizeBuffers failed");

    mCurrBackBuffer = 0;

    CreateRenderTargetViews();
    CreateDepthStencilBuffer();

    if (mRenderingSystem) {
        mRenderingSystem->OnResize(mDevice.Get(), mClientWidth, mClientHeight);
    }

    CreatePostProcessResources();

    mScreenViewport.TopLeftX = 0.0f;
    mScreenViewport.TopLeftY = 0.0f;
    mScreenViewport.Width = static_cast<float>(mClientWidth);
    mScreenViewport.Height = static_cast<float>(mClientHeight);
    mScreenViewport.MinDepth = 0.0f;
    mScreenViewport.MaxDepth = 1.0f;
    mScissorRect = {0, 0, static_cast<LONG>(mClientWidth), static_cast<LONG>(mClientHeight)};

    if (mObjectCB && mPassCB && mLightingCB) {
        BuildMainSrvHeap();
    }
}

void DirectXApp::BuildScene() {
    FlushCommandQueue();
    mVisibleSubmeshes.clear();
    mShadowSubmeshes.clear();
    mLastVisibleObjectCount = 0;
    mLastDrawCallCount = 0;
    ThrowIfFailed(mDirectCmdListAlloc->Reset(), "Reset command allocator failed");
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr), "Reset command list failed");

    LoadModels();
    BuildSceneObjects();
    BuildGeometryBuffers();
    LoadTextures();
    CreateFallbackTextures();

    BuildConstantBuffers();
    CreateShadowResources();
    CreatePostProcessResources();
    InitializeParticleResources();
    BuildMainSrvHeap();
    BindSubmeshTextures();
    BuildLights();

    ThrowIfFailed(mCommandList->Close(), "Close command list failed");
    ID3D12CommandList* cmdLists[] = {mCommandList.Get()};
    mCommandQueue->ExecuteCommandLists(1, cmdLists);
    FlushCommandQueue();

    mVertexBufferUploader.Reset();
    mIndexBufferUploader.Reset();

    ResetParticles();
}

void DirectXApp::LoadModels() {
    if (mCurrentScene == 0) {
        mTerrain.Load("../assets/terrain", mSceneMesh);
        for (auto& submesh : mSceneMesh.submeshes) {
            submesh.material.diffuseTextureName = "Albedo_Out";
            submesh.material.normalTextureName = "Normals_Out";
        }
        mShadowSubmeshes = mTerrain.Select(BoundingFrustum(), {}, false, false);
        mModelBounds = BoundingSphere(XMFLOAT3(0, 32, 0), 370);
        mEyePos = XMFLOAT3(0, 130, -300);
        mYaw = 0;
        mPitch = -0.28f;
        return;
    }
    const std::string path = mCurrentScene == 1 ? "../assets/Earth.fbx" : "../assets/sponza.obj";
    mSceneMesh = ModelLoader::LoadModel(path, XMMatrixIdentity());
    if (mSceneMesh.vertices.empty() || mSceneMesh.indices.empty() || mSceneMesh.submeshes.empty())
        throw std::runtime_error("Model contains no geometry: " + path);
    XMFLOAT3 low(FLT_MAX, FLT_MAX, FLT_MAX), high(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    for (const auto& vertex : mSceneMesh.vertices) {
        low.x = std::min(low.x, vertex.Position.x);
        low.y = std::min(low.y, vertex.Position.y);
        low.z = std::min(low.z, vertex.Position.z);
        high.x = std::max(high.x, vertex.Position.x);
        high.y = std::max(high.y, vertex.Position.y);
        high.z = std::max(high.z, vertex.Position.z);
    }
    const XMVECTOR center = (XMLoadFloat3(&low) + XMLoadFloat3(&high)) * 0.5f;
    float radius = 0;
    for (const auto& vertex : mSceneMesh.vertices)
        radius = std::max(radius, XMVectorGetX(XMVector3Length(XMLoadFloat3(&vertex.Position) - center)));
    if (!std::isfinite(radius) || radius < 0.0001f)
        throw std::runtime_error("Invalid model bounds: " + path);
    const float targetRadius = mCurrentScene == 1 ? 2.4f : 10.0f;
    const float offsetY = mCurrentScene == 1 ? targetRadius * 0.9f : 0.0f;
    for (auto& vertex : mSceneMesh.vertices)
        XMStoreFloat3(&vertex.Position, (XMLoadFloat3(&vertex.Position) - center) * (targetRadius / radius) + XMVectorSet(0, offsetY, 0, 0));
    for (unsigned int i = 0; i < mSceneMesh.submeshes.size(); ++i) {
        mShadowSubmeshes.push_back(i);
        if (mCurrentScene == 1) {
            auto& material = mSceneMesh.submeshes[i].material;
            material.diffuseTextureName = "Earth_ALB";
            material.normalTextureName = "Earth_NORM";
            material.displacementTextureName.clear();
            material.shininess = 64;
        }
    }
    mModelBounds = BoundingSphere(XMFLOAT3(0, offsetY, 0), targetRadius);
    mEyePos = mCurrentScene == 1 ? XMFLOAT3(0, 3, -10) : XMFLOAT3(0, 0, 0);
    mYaw = mCurrentScene == 2 ? XM_PIDIV2 : 0.0f;
    mPitch = mCurrentScene == 1 ? -0.08f : 0.0f;
}

void DirectXApp::BuildSceneObjects() {
    mSceneObjects.clear();
    SceneObject object;
    XMStoreFloat4x4(&object.world, XMMatrixIdentity());
    object.bounds = mModelBounds;
    mSceneObjects.push_back(object);
}

void DirectXApp::BuildGeometryBuffers() {
    const UINT vbByteSize = static_cast<UINT>(mSceneMesh.vertices.size() * sizeof(Vertex));
    const UINT ibByteSize = static_cast<UINT>(mSceneMesh.indices.size() * sizeof(unsigned int));

    mVertexBufferGPU = CreateDefaultBuffer(mDevice.Get(), mCommandList.Get(), mSceneMesh.vertices.data(), vbByteSize,
                                           mVertexBufferUploader);

    mIndexBufferGPU = CreateDefaultBuffer(mDevice.Get(), mCommandList.Get(), mSceneMesh.indices.data(), ibByteSize,
                                          mIndexBufferUploader);

    mVertexBufferView.BufferLocation = mVertexBufferGPU->GetGPUVirtualAddress();
    mVertexBufferView.StrideInBytes = sizeof(Vertex);
    mVertexBufferView.SizeInBytes = vbByteSize;

    mIndexBufferView.BufferLocation = mIndexBufferGPU->GetGPUVirtualAddress();
    mIndexBufferView.Format = DXGI_FORMAT_R32_UINT;
    mIndexBufferView.SizeInBytes = ibByteSize;
}

void DirectXApp::LoadTextures() {
    if (!mIblTextures[0].resource) {
        const wchar_t* files[] = {L"../assets/ibl/irradiance.dds", L"../assets/ibl/prefilter.dds", L"../assets/ibl/brdf.dds"};
        for (unsigned int i = 0; i < 3; ++i) {
            auto& tex = mIblTextures[i];
            tex.path = files[i];
            ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(mDevice.Get(), mCommandList.Get(), files[i], tex.resource, tex.uploadHeap), "Load IBL texture failed");
        }
    }
    mTextureResources.clear();
    mTextureNameToIndex.clear();
    const std::array<std::wstring, 1> dirs = {
        mCurrentScene == 0 ? L"../assets/terrain/maps" :
            (mCurrentScene == 1 ? L"../assets/textures/earth" : L"../assets/textures/sponza")
    };

    for (const auto& dir : dirs) {
        if (!std::filesystem::exists(dir)) {
            continue;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const auto ext = ToLowerAscii(entry.path().extension().string());
            const bool isDDS = (ext == ".dds");
            const bool isTGA = (ext == ".tga");
            const bool isWIC = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tif" || ext == ".tiff");
            if (!isDDS && !isTGA && !isWIC) {
                continue;
            }

            TextureResource tex;
            tex.path = entry.path().wstring();

            bool loaded = false;
            if (isDDS) {
                const HRESULT hr = DirectX::CreateDDSTextureFromFile12(
                    mDevice.Get(),
                    mCommandList.Get(),
                    tex.path.c_str(),
                    tex.resource,
                    tex.uploadHeap);
                loaded = SUCCEEDED(hr);
            } else if (isTGA) {
                loaded = LoadTgaTexture12(
                    mDevice.Get(),
                    mCommandList.Get(),
                    tex.path,
                    tex.resource,
                    tex.uploadHeap);
            } else {
                loaded = LoadWicTextureFromFile12(
                    mDevice.Get(),
                    mCommandList.Get(),
                    tex.path,
                    tex.resource,
                    tex.uploadHeap);
            }

            if (!loaded) {
                std::string msg = "Failed to load texture: " + entry.path().string() + "\n";
                OutputDebugStringA(msg.c_str());
                continue;
            }

            const std::string name = ToLowerAscii(entry.path().stem().string());
            if (mTextureNameToIndex.find(name) != mTextureNameToIndex.end()) {
                continue;
            }

            const unsigned int newIndex = static_cast<unsigned int>(mTextureResources.size());
            mTextureNameToIndex[name] = newIndex;
            mTextureResources.push_back(std::move(tex));
        }
    }


}

void DirectXApp::CreateFallbackTextures() {
    auto addSolid = [&](const std::string& key, unsigned int rgba) {
        TextureResource tex;

        D3D12_RESOURCE_DESC texDesc = {};
        texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        texDesc.Width = 1;
        texDesc.Height = 1;
        texDesc.DepthOrArraySize = 1;
        texDesc.MipLevels = 1;
        texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ThrowIfFailed(mDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&tex.resource)),
            "Create fallback texture failed");

        const UINT64 uploadSize = GetRequiredIntermediateSize(tex.resource.Get(), 0, 1);

        ThrowIfFailed(mDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uploadSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&tex.uploadHeap)),
            "Create fallback upload failed");

        D3D12_SUBRESOURCE_DATA subresource = {};
        subresource.pData = &rgba;
        subresource.RowPitch = 4;
        subresource.SlicePitch = 4;

        UpdateSubresources(mCommandList.Get(), tex.resource.Get(), tex.uploadHeap.Get(), 0, 0, 1, &subresource);
        auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
            tex.resource.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        mCommandList->ResourceBarrier(1, &toSrv);

        const unsigned int newIndex = static_cast<unsigned int>(mTextureResources.size());
        mTextureNameToIndex[key] = newIndex;
        mTextureResources.push_back(std::move(tex));

        return newIndex;
    };

    mFallbackDiffuseIndex = addSolid("__fallback_diffuse", 0xFFFFFFFFu);
    mFallbackNormalIndex = addSolid("__fallback_normal", 0xFFFF8080u);
    mFallbackDisplacementIndex = addSolid("__fallback_displacement", 0xFF000000u);
}

void DirectXApp::BuildConstantBuffers() {
    const unsigned int objectCount = (std::max)(
        1u,
        static_cast<unsigned int>(mSceneObjects.size() * mSceneMesh.submeshes.size()));
    mObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(mDevice.Get(), objectCount * (kNumCascades + 1u), true);
    mPassCB = std::make_unique<UploadBuffer<PassConstants>>(mDevice.Get(), 1, true);
    mLightingCB = std::make_unique<UploadBuffer<LightingConstants>>(mDevice.Get(), LightingCbElementCount, true);
    mShadowCB = std::make_unique<UploadBuffer<ShadowConstants>>(mDevice.Get(), 1, true);
    mPostCB = std::make_unique<UploadBuffer<PostConstants>>(mDevice.Get(), 1, true);
    mParticleSimCB = std::make_unique<UploadBuffer<ParticleSimConstants>>(mDevice.Get(), 1, true);
    mParticleRenderCB = std::make_unique<UploadBuffer<ParticleRenderConstants>>(mDevice.Get(), 1, true);
}

void DirectXApp::BuildMainSrvHeap() {
    const unsigned int textureCount = static_cast<unsigned int>(mTextureResources.size());
    const unsigned int descriptorCount = 4 + textureCount + GBuffer::Count + 1 + 3 + 4 + 4;

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = descriptorCount;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mCbvSrvHeap)), "Create SRV heap failed");

    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(mCbvSrvHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = mObjectCB->Resource()->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    mDevice->CreateConstantBufferView(&cbvDesc, cpuHandle);
    cpuHandle.Offset(1, mCbvSrvUavDescriptorSize);

    cbvDesc.BufferLocation = mPassCB->Resource()->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
    mDevice->CreateConstantBufferView(&cbvDesc, cpuHandle);
    cpuHandle.Offset(1, mCbvSrvUavDescriptorSize);

    cbvDesc.BufferLocation = mLightingCB->Resource()->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(LightingConstants));
    mDevice->CreateConstantBufferView(&cbvDesc, cpuHandle);
    cpuHandle.Offset(1, mCbvSrvUavDescriptorSize);

    cbvDesc.BufferLocation = mShadowCB->Resource()->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = d3dUtil::CalcConstantBufferByteSize(sizeof(ShadowConstants));
    mDevice->CreateConstantBufferView(&cbvDesc, cpuHandle);
    cpuHandle.Offset(1, mCbvSrvUavDescriptorSize);

    mTextureSrvStart = 4;

    for (unsigned int i = 0; i < textureCount; ++i) {
        auto& tex = mTextureResources[i];

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = tex.resource->GetDesc().Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = tex.resource->GetDesc().MipLevels;
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        mDevice->CreateShaderResourceView(tex.resource.Get(), &srvDesc, cpuHandle);
        tex.srvHeapIndex = mTextureSrvStart + i;
        cpuHandle.Offset(1, mCbvSrvUavDescriptorSize);
    }

    mGBufferSrvStart = mTextureSrvStart + textureCount;

    CD3DX12_CPU_DESCRIPTOR_HANDLE gbufferCpu(mCbvSrvHeap->GetCPUDescriptorHandleForHeapStart(),
                                             static_cast<INT>(mGBufferSrvStart),
                                             mCbvSrvUavDescriptorSize);
    CD3DX12_GPU_DESCRIPTOR_HANDLE gbufferGpu(mCbvSrvHeap->GetGPUDescriptorHandleForHeapStart(),
                                             static_cast<INT>(mGBufferSrvStart),
                                             mCbvSrvUavDescriptorSize);

    mRenderingSystem->GetGBuffer()->CreateSrvs(
        mDevice.Get(),
        gbufferCpu,
        gbufferGpu,
        mCbvSrvUavDescriptorSize);

    if (mShadowMap) {
        mShadowSrvIndex = mGBufferSrvStart + GBuffer::Count;

        CD3DX12_CPU_DESCRIPTOR_HANDLE shadowCpu(
            mCbvSrvHeap->GetCPUDescriptorHandleForHeapStart(),
            static_cast<INT>(mShadowSrvIndex),
            mCbvSrvUavDescriptorSize);

        D3D12_SHADER_RESOURCE_VIEW_DESC shadowSrvDesc = {};
        shadowSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        shadowSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
        shadowSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        shadowSrvDesc.Texture2DArray.MostDetailedMip = 0;
        shadowSrvDesc.Texture2DArray.MipLevels = 1;
        shadowSrvDesc.Texture2DArray.FirstArraySlice = 0;
        shadowSrvDesc.Texture2DArray.ArraySize = kNumCascades;
        shadowSrvDesc.Texture2DArray.PlaneSlice = 0;
        shadowSrvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;

        mDevice->CreateShaderResourceView(mShadowMap.Get(), &shadowSrvDesc, shadowCpu);
    }

    for (unsigned int i = 0; i < 3; ++i) {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
        srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv.Format = mIblTextures[i].resource->GetDesc().Format;
        if (i < 2) {
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srv.TextureCube.MipLevels = mIblTextures[i].resource->GetDesc().MipLevels;
        } else {
            srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srv.Texture2D.MipLevels = 1;
        }
        CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mCbvSrvHeap->GetCPUDescriptorHandleForHeapStart(),
            static_cast<INT>(mShadowSrvIndex + 1 + i), mCbvSrvUavDescriptorSize);
        mDevice->CreateShaderResourceView(mIblTextures[i].resource.Get(), &srv, handle);
    }
    mParticleSrvIndex = mShadowSrvIndex + 4;
    mParticleUavStartIndex = mParticleSrvIndex + 2;

    CD3DX12_CPU_DESCRIPTOR_HANDLE particleCpu(
        mCbvSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        static_cast<INT>(mParticleSrvIndex),
        mCbvSrvUavDescriptorSize);

    D3D12_SHADER_RESOURCE_VIEW_DESC particleSrvDesc = {};
    particleSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    particleSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
    particleSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    particleSrvDesc.Buffer.FirstElement = 0;
    particleSrvDesc.Buffer.NumElements = kMaxParticles;
    particleSrvDesc.Buffer.StructureByteStride = sizeof(ParticleGpu);
    particleSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    for (unsigned int i = 0; i < 2; ++i) {
        mDevice->CreateShaderResourceView(mParticleBuffers[i].Get(), &particleSrvDesc, particleCpu);
        particleCpu.Offset(1, mCbvSrvUavDescriptorSize);
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = kMaxParticles;
    uavDesc.Buffer.StructureByteStride = sizeof(ParticleGpu);
    uavDesc.Buffer.CounterOffsetInBytes = 0;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    for (unsigned int i = 0; i < 2; ++i) {
        mDevice->CreateUnorderedAccessView(
            mParticleBuffers[i].Get(),
            mParticleCounters[i].Get(),
            &uavDesc,
            particleCpu);
        particleCpu.Offset(1, mCbvSrvUavDescriptorSize);
    }

    mPostSrvStart = mParticleUavStartIndex + 2;

    CD3DX12_CPU_DESCRIPTOR_HANDLE postCpu(
        mCbvSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        static_cast<INT>(mPostSrvStart),
        mCbvSrvUavDescriptorSize);

    D3D12_SHADER_RESOURCE_VIEW_DESC sceneSrvDesc = {};
    sceneSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sceneSrvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    sceneSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sceneSrvDesc.Texture2D.MostDetailedMip = 0;
    sceneSrvDesc.Texture2D.MipLevels = 1;
    sceneSrvDesc.Texture2D.PlaneSlice = 0;
    sceneSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    mDevice->CreateShaderResourceView(mPostProcessTexture.Get(), &sceneSrvDesc, postCpu);
    postCpu.Offset(1, mCbvSrvUavDescriptorSize);

    auto* gbuffer = mRenderingSystem->GetGBuffer();
    for (unsigned int i = 0; i < GBuffer::Count; ++i) {
        D3D12_SHADER_RESOURCE_VIEW_DESC gbufferSrvDesc = {};
        gbufferSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        gbufferSrvDesc.Format = gbuffer->GetFormat(static_cast<GBuffer::TextureType>(i));
        gbufferSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        gbufferSrvDesc.Texture2D.MostDetailedMip = 0;
        gbufferSrvDesc.Texture2D.MipLevels = 1;
        gbufferSrvDesc.Texture2D.PlaneSlice = 0;
        gbufferSrvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
        mDevice->CreateShaderResourceView(
            gbuffer->GetTexture(static_cast<GBuffer::TextureType>(i)),
            &gbufferSrvDesc,
            postCpu);
        postCpu.Offset(1, mCbvSrvUavDescriptorSize);
    }
}

void DirectXApp::BindSubmeshTextures() {
    auto resolve = [&](const std::string& rawName, unsigned int fallbackIndex) -> unsigned int {
        const std::string key = ToLowerAscii(rawName);
        auto it = mTextureNameToIndex.find(key);
        if (it == mTextureNameToIndex.end()) {
            return mTextureResources[fallbackIndex].srvHeapIndex;
        }
        return mTextureResources[it->second].srvHeapIndex;
    };

    for (auto& submesh : mSceneMesh.submeshes) {
        submesh.material.diffuseSrvHeapIndex = resolve(submesh.material.diffuseTextureName, mFallbackDiffuseIndex);
        submesh.material.normalSrvHeapIndex = resolve(submesh.material.normalTextureName, mFallbackNormalIndex);

        if (submesh.material.displacementTextureName.empty()) {
            submesh.material.displacementSrvHeapIndex = mTextureResources[mFallbackDisplacementIndex].srvHeapIndex;
        } else {
            submesh.material.displacementSrvHeapIndex =
                resolve(submesh.material.displacementTextureName, mFallbackDisplacementIndex);
        }
    }
}

void DirectXApp::BuildLights() {
    mLights.clear();
    LightData light;
    light.Type = static_cast<unsigned int>(LightType::Directional);
    light.Direction = XMFLOAT3(-0.3f, -1.0f, 0.4f);
    light.Color = XMFLOAT3(1.0f, 0.97f, 0.9f);
    light.Intensity = 3.0f;
    mLights.push_back(light);
}

void DirectXApp::UpdateFallingLights(float dt) {
    if (!mFallingBallsEnabled) {
        mFallingLights.clear();
        return;
    }

    static std::mt19937 rng{1337u};
    std::uniform_real_distribution<float> xzDist(-14.0f, 14.0f);
    std::uniform_real_distribution<float> colorDist(0.4f, 1.0f);
    std::uniform_real_distribution<float> speedDist(1.2f, 2.6f);
    std::uniform_real_distribution<float> rangeDist(6.0f, 12.0f);

    mFallingSpawnTimer += dt;
    while (mFallingSpawnTimer >= mFallingSpawnInterval && mFallingLights.size() < mMaxFallingLights) {
        mFallingSpawnTimer -= mFallingSpawnInterval;

        FallingLight light;
        light.position = XMFLOAT3(xzDist(rng), 12.0f + speedDist(rng) * 2.0f, xzDist(rng));
        light.velocity = XMFLOAT3(0.0f, -(3.5f + speedDist(rng)), 0.0f);
        light.color = XMFLOAT3(colorDist(rng), colorDist(rng), colorDist(rng));
        light.intensity = 6.5f + speedDist(rng) * 2.0f;
        light.range = rangeDist(rng);
        light.settled = false;
        mFallingLights.push_back(light);
    }

    constexpr float kGroundY = -0.02f;
    constexpr float kGravity = 8.5f;

    for (auto& light : mFallingLights) {
        if (!light.settled) {
            light.velocity.y -= kGravity * dt;
            light.position.y += light.velocity.y * dt;
            if (light.position.y <= kGroundY) {
                light.position.y = kGroundY;
                light.velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
                light.settled = true;
            }
        } else {
            light.intensity -= 1.2f * dt;
            light.range -= 0.7f * dt;
        }
    }

    mFallingLights.erase(
        std::remove_if(mFallingLights.begin(), mFallingLights.end(), [](const FallingLight& light) {
            return light.intensity <= 0.2f || light.range <= 0.8f;
        }),
        mFallingLights.end());
}

void DirectXApp::CreateShadowResources() {
    mShadowMap.Reset();
    mShadowDsvHeap.Reset();

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = kShadowMapSize;
    texDesc.Height = kShadowMapSize;
    texDesc.DepthOrArraySize = kNumCascades;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_D32_FLOAT;
    clear.DepthStencil.Depth = 1.0f;
    clear.DepthStencil.Stencil = 0;

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear,
        IID_PPV_ARGS(&mShadowMap)),
        "Create shadow map failed");

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = kNumCascades;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&mShadowDsvHeap)),
                  "Create shadow DSV heap failed");

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
    dsvDesc.Texture2DArray.MipSlice = 0;
    dsvDesc.Texture2DArray.ArraySize = 1;

    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart());
    for (unsigned int i = 0; i < kNumCascades; ++i) {
        dsvDesc.Texture2DArray.FirstArraySlice = i;
        mDevice->CreateDepthStencilView(mShadowMap.Get(), &dsvDesc, dsvHandle);
        dsvHandle.Offset(1, mDsvDescriptorSize);
    }

    mShadowViewport = {0.0f, 0.0f, static_cast<float>(kShadowMapSize), static_cast<float>(kShadowMapSize), 0.0f, 1.0f};
    mShadowScissor = {0, 0, static_cast<LONG>(kShadowMapSize), static_cast<LONG>(kShadowMapSize)};
    mShadowMapStateIsSrv = false;
}

void DirectXApp::CreatePostProcessResources() {
    mPostProcessTexture.Reset();
    mPostProcessRtvHeap.Reset();
    mPostProcessState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(mDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&mPostProcessRtvHeap)),
                  "Create post-process RTV heap failed");

    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = mClientWidth;
    textureDesc.Height = mClientHeight;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    clear.Color[0] = 0.0f;
    clear.Color[1] = 0.0f;
    clear.Color[2] = 0.0f;
    clear.Color[3] = 1.0f;

    ThrowIfFailed(mDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        mPostProcessState,
        &clear,
        IID_PPV_ARGS(&mPostProcessTexture)),
        "Create post-process texture failed");

    mPostProcessRtv = mPostProcessRtvHeap->GetCPUDescriptorHandleForHeapStart();
    mDevice->CreateRenderTargetView(mPostProcessTexture.Get(), nullptr, mPostProcessRtv);
}

void DirectXApp::UpdateCamera(float dt) {
    const float baseSpeed = mCurrentScene == 0 ? 45.0f : 5.0f;
    const float moveSpeed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) ? baseSpeed * 3.5f : baseSpeed;

    const XMVECTOR forward = XMVector3Normalize(XMVectorSet(
        std::cos(mPitch) * std::sin(mYaw),
        std::sin(mPitch),
        std::cos(mPitch) * std::cos(mYaw),
        0.0f));

    const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));

    XMVECTOR position = XMLoadFloat3(&mEyePos);

    if (GetAsyncKeyState('W') & 0x8000) {
        position += forward * moveSpeed * dt;
    }
    if (GetAsyncKeyState('S') & 0x8000) {
        position -= forward * moveSpeed * dt;
    }
    if (GetAsyncKeyState('A') & 0x8000) {
        position -= right * moveSpeed * dt;
    }
    if (GetAsyncKeyState('D') & 0x8000) {
        position += right * moveSpeed * dt;
    }

    if (GetAsyncKeyState('Q') & 0x8000) position -= worldUp * moveSpeed * dt;
    if (GetAsyncKeyState('E') & 0x8000) position += worldUp * moveSpeed * dt;
    XMStoreFloat3(&mEyePos, position);
}

void DirectXApp::Update(const GameTimer& gt) {
    UpdateCamera(gt.DeltaTime());

    if (GetAsyncKeyState('3') & 0x8000) mDebugViewMode = 1;
    if (GetAsyncKeyState('4') & 0x8000) mDebugViewMode = 2;
    if (GetAsyncKeyState('5') & 0x8000) mDebugViewMode = 3;
    const bool cull = (GetAsyncKeyState('8') & 0x8000) != 0;
    const bool lod = (GetAsyncKeyState('9') & 0x8000) != 0;
    if (cull && !mCWasDown) mFrustumCullingEnabled = !mFrustumCullingEnabled;
    if (lod && !mOWasDown) mTerrainLodEnabled = !mTerrainLodEnabled;
    mCWasDown = cull;
    mOWasDown = lod;
    mFrameCount++;
    mFpsTimer += gt.DeltaTime();
    if (mFpsTimer >= 0.5f) {
        mCurrentFps = mFrameCount / mFpsTimer;
        mFrameCount = 0;
        mFpsTimer = 0;
        std::array<unsigned int, 5> counts = {};
        if (mCurrentScene == 0)
            for (auto patch : mVisibleSubmeshes) ++counts[mTerrain.Nodes()[patch].level];
        std::wostringstream title;
        const wchar_t* sceneName = mCurrentScene == 0 ? L"Terrain" : (mCurrentScene == 1 ? L"Earth" : L"Sponza");
        title << sceneName << L" | FPS " << int(mCurrentFps);
        if (mCurrentScene == 0) {
            title << L" | Patches " << mLastVisibleObjectCount << L" | Culled " << mTerrain.Culled()
                << L" | LOD " << (mTerrainLodEnabled ? L"ON" : L"OFF") << L" | Levels ";
            for (auto count : counts) title << count << L" ";
        } else {
            title << L" | Draws " << mLastDrawCallCount;
        }
        title << L" | Culling " << (mFrustumCullingEnabled ? L"ON" : L"OFF")
            << L" | Num1:Terrain Num2:Earth Num3:Sponza | 3:Material 4:Normals 5:Wire 8:Culling 9:LOD";
        SetWindowTextW(mHwnd, title.str().c_str());
    }

    const XMVECTOR forward = XMVector3Normalize(XMVectorSet(
        std::cos(mPitch) * std::sin(mYaw),
        std::sin(mPitch),
        std::cos(mPitch) * std::cos(mYaw),
        0.0f));

    const XMVECTOR eye = XMLoadFloat3(&mEyePos);
    const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    const XMMATRIX view = XMMatrixLookToLH(eye, forward, up);
    const XMMATRIX proj = XMMatrixPerspectiveFovLH(0.25f * XM_PI,
                                                   static_cast<float>(mClientWidth) / static_cast<float>(mClientHeight),
                                                   CameraNearZ,
                                                   CameraFarZ);
    if (mAnimateTextures) {
        mTexAnimU += 0.04f * gt.DeltaTime();
        mTexAnimV += 0.015f * gt.DeltaTime();
        if (mTexAnimU > 1.0f) {
            mTexAnimU -= 1.0f;
        }
        if (mTexAnimV > 1.0f) {
            mTexAnimV -= 1.0f;
        }
    }
    PassConstants pass = {};
    XMMATRIX invViewProj = XMMatrixInverse(nullptr, view * proj);
    XMStoreFloat4x4(&pass.InvViewProj, XMMatrixTranspose(invViewProj));
    pass.EyePosW = mEyePos;
    pass.AmbientColor = XMFLOAT4(0.08f, 0.08f, 0.1f, 1.0f);
    mPassCB->CopyData(0, pass);

    ShadowConstants shadow = {};
    const float nearZ = CameraNearZ;
    const float farZ = CameraFarZ;
    const float lambda = 0.75f;
    const float clipRange = farZ - nearZ;

    for (unsigned int i = 0; i < kNumCascades; ++i) {
        const float p = static_cast<float>(i + 1) / static_cast<float>(kNumCascades);
        const float logSplit = nearZ * std::pow(farZ / nearZ, p);
        const float uniformSplit = nearZ + clipRange * p;
        const float splitDist = lambda * logSplit + (1.0f - lambda) * uniformSplit;

        if (i == 0) shadow.CascadeSplits.x = splitDist;
        else if (i == 1) shadow.CascadeSplits.y = splitDist;
        else shadow.CascadeSplits.z = splitDist;

        float splitNear = (i == 0) ? nearZ : (lambda * std::pow(farZ / nearZ, static_cast<float>(i) / kNumCascades)
            + (1.0f - lambda) * (nearZ + clipRange * static_cast<float>(i) / kNumCascades));
        float splitFar = splitDist;

        XMMATRIX subProj = XMMatrixPerspectiveFovLH(0.25f * XM_PI,
            static_cast<float>(mClientWidth) / static_cast<float>(mClientHeight),
            splitNear, splitFar);

        XMVECTOR frustumCorners[8] = {
            XMVectorSet(-1.0f,  1.0f, 0.0f, 1.0f),
            XMVectorSet( 1.0f,  1.0f, 0.0f, 1.0f),
            XMVectorSet(-1.0f, -1.0f, 0.0f, 1.0f),
            XMVectorSet( 1.0f, -1.0f, 0.0f, 1.0f),
            XMVectorSet(-1.0f,  1.0f, 1.0f, 1.0f),
            XMVectorSet( 1.0f,  1.0f, 1.0f, 1.0f),
            XMVectorSet(-1.0f, -1.0f, 1.0f, 1.0f),
            XMVectorSet( 1.0f, -1.0f, 1.0f, 1.0f),
        };

        XMMATRIX invSubProj = XMMatrixInverse(nullptr, view * subProj);
        for (auto& corner : frustumCorners) {
            corner = XMVector4Transform(corner, invSubProj);
            corner = XMVectorDivide(corner, XMVectorSplatW(corner));
        }

        XMVECTOR center = XMVectorZero();
        for (int c = 0; c < 8; ++c) {
            center = XMVectorAdd(center, frustumCorners[c]);
        }
        center = XMVectorScale(center, 1.0f / 8.0f);

        float radius = 0.0f;
        for (int c = 0; c < 8; ++c) {
            float dist = XMVectorGetX(XMVector3Length(XMVectorSubtract(frustumCorners[c], center)));
            radius = (std::max)(radius, dist);
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        XMVECTOR lightDir = XMVector3Normalize(XMLoadFloat3(&mLights[0].Direction));
        XMVECTOR lightPos = XMVectorSubtract(center, XMVectorScale(lightDir, radius * 2.0f));
        XMMATRIX lightView = XMMatrixLookAtLH(lightPos, center, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));

        const float texelWorldSize = (2.0f * radius) / static_cast<float>(kShadowMapSize);
        XMVECTOR centerLightSpace = XMVector3TransformCoord(center, lightView);
        const float offsetX = std::floor(XMVectorGetX(centerLightSpace) / texelWorldSize) * texelWorldSize - XMVectorGetX(centerLightSpace);
        const float offsetY = std::floor(XMVectorGetY(centerLightSpace) / texelWorldSize) * texelWorldSize - XMVectorGetY(centerLightSpace);
        XMMATRIX snap = XMMatrixTranslation(offsetX, offsetY, 0.0f);
        lightView = lightView * snap;

        float l = -radius;
        float r = radius;
        float b = -radius;
        float t = radius;
        float n = -radius * 4.0f;
        float f = radius * 4.0f;
        XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

        XMMATRIX lpv = XMMatrixTranspose(lightView * lightProj);
        XMStoreFloat4x4(&shadow.LightViewProj[i], lpv);
        mCascadeViewProj[i] = shadow.LightViewProj[i];
    }

    XMFLOAT3 lightDirStorage;
    XMStoreFloat3(&lightDirStorage, XMLoadFloat3(&mLights[0].Direction));
    shadow.LightDirection = XMFLOAT4(lightDirStorage.x, lightDirStorage.y, lightDirStorage.z, 0.0f);

    mShadowCB->CopyData(0, shadow);

    PostConstants post = {};
    post.InvRenderTargetSize = XMFLOAT2(
        1.0f / static_cast<float>((std::max)(mClientWidth, 1u)),
        1.0f / static_cast<float>((std::max)(mClientHeight, 1u)));
    post.Time = gt.TotalTime();
    post.BloomStrength = 0.15f;
    post.VignetteStrength = 0.18f;
    mPostCB->CopyData(0, post);


}

void DirectXApp::DrawPostEffects(ID3D12GraphicsCommandList* cmdList) {
    if (!cmdList || !mPostCB) {
        return;
    }

    cmdList->SetPipelineState(mRenderingSystem->GetPostPSO());
    cmdList->SetGraphicsRootSignature(mRenderingSystem->GetPostRootSignature());
    cmdList->SetGraphicsRootDescriptorTable(0, GetGpuSrvHandle(mPostSrvStart));
    cmdList->SetGraphicsRootConstantBufferView(1, mPostCB->Resource()->GetGPUVirtualAddress());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cmdList->DrawInstanced(4, 1, 0, 0);
}

void DirectXApp::Draw(const GameTimer& gt) {
    if (mSceneMesh.submeshes.empty() || mSceneMesh.indices.empty() || mSceneMesh.vertices.empty()) {
        OutputDebugStringA("Draw skipped: mesh is empty.\n");
        return;
    }
    FlushCommandQueue();

    ThrowIfFailed(mDirectCmdListAlloc->Reset(), "Reset command allocator failed");
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr), "Reset command list failed");

    ID3D12DescriptorHeap* descriptorHeaps[] = {mCbvSrvHeap.Get()};
    mCommandList->SetDescriptorHeaps(1, descriptorHeaps);

    {
        if (mShadowMapStateIsSrv) {
            auto shadowBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
                mShadowMap.Get(),
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_DEPTH_WRITE);
            mCommandList->ResourceBarrier(1, &shadowBarrier);
            mShadowMapStateIsSrv = false;
        }

        mCommandList->RSSetViewports(1, &mShadowViewport);
        mCommandList->RSSetScissorRects(1, &mShadowScissor);

        mCommandList->SetPipelineState(mRenderingSystem->GetShadowPSO());
        mCommandList->SetGraphicsRootSignature(mRenderingSystem->GetShadowRootSignature());
        mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferView);
        mCommandList->IASetIndexBuffer(&mIndexBufferView);

        const unsigned int objectElementSize = mObjectCB->GetElementSize();

        for (unsigned int cascade = 0; cascade < kNumCascades; ++cascade) {
            CD3DX12_CPU_DESCRIPTOR_HANDLE shadowDsv(
                mShadowDsvHeap->GetCPUDescriptorHandleForHeapStart(),
                static_cast<INT>(cascade),
                mDsvDescriptorSize);

            mCommandList->ClearDepthStencilView(shadowDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            mCommandList->OMSetRenderTargets(0, nullptr, FALSE, &shadowDsv);

            const XMVECTOR shadowForward = XMVector3Normalize(XMVectorSet(
                std::cos(mPitch) * std::sin(mYaw),
                std::sin(mPitch),
                std::cos(mPitch) * std::cos(mYaw),
                0.0f));
            const XMVECTOR shadowEye = XMLoadFloat3(&mEyePos);
            const XMVECTOR shadowUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            const XMMATRIX shadowView = XMMatrixLookToLH(shadowEye, shadowForward, shadowUp);
            const XMMATRIX shadowProj = XMMatrixPerspectiveFovLH(0.25f * XM_PI,
                static_cast<float>(mClientWidth) / static_cast<float>(mClientHeight),
                CameraNearZ, CameraFarZ);

            const XMMATRIX cascadeViewProj = XMMatrixTranspose(XMLoadFloat4x4(&mCascadeViewProj[cascade]));
            const unsigned int shadowObjectCount = static_cast<unsigned int>(mSceneObjects.size() * mSceneMesh.submeshes.size());
            const unsigned int shadowCbBase = shadowObjectCount * (cascade + 1u);
            unsigned int shadowCbIdx = shadowCbBase;
            for (unsigned int sceneObjectIndex = 0; sceneObjectIndex < mSceneObjects.size(); ++sceneObjectIndex) {
                const auto& sceneObject = mSceneObjects[sceneObjectIndex];
                const XMMATRIX world = XMLoadFloat4x4(&sceneObject.world);

                for (auto patch : mShadowSubmeshes) {
                    const auto& submesh = mSceneMesh.submeshes[patch];
                    ObjectConstants obj = {};
                    XMStoreFloat4x4(&obj.World, XMMatrixTranspose(world));
                    XMStoreFloat4x4(&obj.WorldViewProj, XMMatrixTranspose(world * cascadeViewProj));
                    mObjectCB->CopyData(static_cast<int>(shadowCbIdx), obj);

                    mCommandList->SetGraphicsRootConstantBufferView(
                        0,
                        mObjectCB->Resource()->GetGPUVirtualAddress() +
                            static_cast<UINT64>(shadowCbIdx) * objectElementSize);

                    mCommandList->DrawIndexedInstanced(
                        mCurrentScene == 0 ? 32u * 32u * 6u : submesh.indexCount,
                        1,
                        submesh.startIndexLocation,
                        submesh.baseVertexLocation,
                        0);
                    ++shadowCbIdx;
                }
            }
        }

        auto shadowToSrv = CD3DX12_RESOURCE_BARRIER::Transition(
            mShadowMap.Get(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        mCommandList->ResourceBarrier(1, &shadowToSrv);
        mShadowMapStateIsSrv = true;
    }

    auto* gbuffer = mRenderingSystem->GetGBuffer();

    std::array<D3D12_RESOURCE_BARRIER, GBuffer::Count> toRT{};
    for (unsigned int i = 0; i < GBuffer::Count; ++i) {
        toRT[i] = CD3DX12_RESOURCE_BARRIER::Transition(
            gbuffer->GetTexture(static_cast<GBuffer::TextureType>(i)),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
    }
    mCommandList->ResourceBarrier(static_cast<UINT>(toRT.size()), toRT.data());

    D3D12_CPU_DESCRIPTOR_HANDLE gbuffRtvs[3] = {
        gbuffer->GetRtv(GBuffer::Albedo),
        gbuffer->GetRtv(GBuffer::Normal),
        gbuffer->GetRtv(GBuffer::Depth)
    };

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    gbuffer->Clear(mCommandList.Get());
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                                        1.0f, 0, 0, nullptr);

    auto dsv = DepthStencilView();
    mCommandList->OMSetRenderTargets(3, gbuffRtvs, FALSE, &dsv);
    mCommandList->IASetVertexBuffers(0, 1, &mVertexBufferView);
    mCommandList->IASetIndexBuffer(&mIndexBufferView);

    mCommandList->SetGraphicsRootSignature(mRenderingSystem->GetGeometryRootSignature());

    mCommandList->SetGraphicsRootConstantBufferView(1, mPassCB->Resource()->GetGPUVirtualAddress());

    const XMVECTOR forward = XMVector3Normalize(XMVectorSet(
        std::cos(mPitch) * std::sin(mYaw),
        std::sin(mPitch),
        std::cos(mPitch) * std::cos(mYaw),
        0.0f));
    const XMVECTOR eye = XMLoadFloat3(&mEyePos);
    const XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMMATRIX view = XMMatrixLookToLH(eye, forward, up);
    const XMMATRIX proj = XMMatrixPerspectiveFovLH(0.25f * XM_PI,
                                                   static_cast<float>(mClientWidth) / static_cast<float>(mClientHeight),
                                                   CameraNearZ,
                                                   CameraFarZ);

    BoundingFrustum cameraFrustum;
    BoundingFrustum::CreateFromMatrix(cameraFrustum, proj);
    BoundingFrustum worldFrustum;
    cameraFrustum.Transform(worldFrustum, XMMatrixInverse(nullptr, view));

    mVisibleSubmeshes.clear();
    if (mCurrentScene == 0) {
        mVisibleSubmeshes = mTerrain.Select(worldFrustum, mEyePos, mFrustumCullingEnabled, mTerrainLodEnabled);
    } else if (!mFrustumCullingEnabled || worldFrustum.Contains(mModelBounds) != DISJOINT) {
        for (unsigned int i = 0; i < mSceneMesh.submeshes.size(); ++i) mVisibleSubmeshes.push_back(i);
    }
    mLastVisibleObjectCount = static_cast<unsigned int>(mVisibleSubmeshes.size());
    mLastDrawCallCount = mLastVisibleObjectCount;

    const unsigned int objectElementSize = mObjectCB->GetElementSize();
    unsigned int objectCbIndex = 0;

    for (unsigned int sceneObjectIndex = 0; sceneObjectIndex < mSceneObjects.size(); ++sceneObjectIndex) {
        const auto& sceneObject = mSceneObjects[sceneObjectIndex];
        const XMMATRIX world = XMLoadFloat4x4(&sceneObject.world);

        for (auto patch : mVisibleSubmeshes) {
            const auto& submesh = mSceneMesh.submeshes[patch];
            const bool hasDisplacement = !submesh.material.displacementTextureName.empty() &&
                                         submesh.material.displacementSrvHeapIndex !=
                                             mTextureResources[mFallbackDisplacementIndex].srvHeapIndex;
            const bool tessellated = hasDisplacement;
            const bool wireframeDebug = (mDebugViewMode == 3);
            const XMMATRIX texTransform =
                XMMatrixScaling(mTexScaleU, mTexScaleV, 1.0f) * XMMatrixTranslation(mTexAnimU, mTexAnimV, 0.0f);

            ObjectConstants obj = {};
            XMStoreFloat4x4(&obj.World, XMMatrixTranspose(world));
            XMStoreFloat4x4(&obj.WorldViewProj, XMMatrixTranspose(world * view * proj));
            XMStoreFloat4x4(&obj.TextureTransform, XMMatrixTranspose(texTransform));
            obj.TimePadding = {std::clamp(std::sqrt(2.0f / (submesh.material.shininess + 2.0f)), 0.08f, 1.0f), submesh.material.metallic, 0.0f};
            obj.TotalTime = mAnimateTextures ? gt.TotalTime() : 0.0f;
            obj.Params.x = static_cast<float>(mDebugViewMode);
            obj.Params.y = 0.085f;
            obj.Params.z = 0.0f;
            obj.Params.w = mCurrentScene == 0 ? -1.0f : 0.0f;
            if (mCurrentScene == 0) obj.TimePadding.z = float(mTerrain.Nodes()[patch].level) / 4.0f;
            mObjectCB->CopyData(static_cast<int>(objectCbIndex), obj);
            mCommandList->SetGraphicsRootConstantBufferView(
                0,
                mObjectCB->Resource()->GetGPUVirtualAddress() +
                    static_cast<UINT64>(objectCbIndex) * objectElementSize);

            if (tessellated) {
                mCommandList->SetPipelineState(wireframeDebug
                                                   ? mRenderingSystem->GetTessellationWirePSO()
                                                   : mRenderingSystem->GetTessellationPSO());
                mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
            } else {
                mCommandList->SetPipelineState(wireframeDebug
                                                   ? mRenderingSystem->GetGeometryWirePSO()
                                                   : mRenderingSystem->GetGeometryPSO());
                mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            }

            mCommandList->SetGraphicsRootDescriptorTable(2, GetGpuSrvHandle(submesh.material.diffuseSrvHeapIndex));
            mCommandList->SetGraphicsRootDescriptorTable(3, GetGpuSrvHandle(submesh.material.normalSrvHeapIndex));
            mCommandList->SetGraphicsRootDescriptorTable(4, GetGpuSrvHandle(submesh.material.displacementSrvHeapIndex));

            mCommandList->DrawIndexedInstanced(
                submesh.indexCount,
                1,
                submesh.startIndexLocation,
                submesh.baseVertexLocation,
                0);
            ++objectCbIndex;
        }
    }

    std::array<D3D12_RESOURCE_BARRIER, GBuffer::Count> toSrv{};
    for (unsigned int i = 0; i < GBuffer::Count; ++i) {
        toSrv[i] = CD3DX12_RESOURCE_BARRIER::Transition(
            gbuffer->GetTexture(static_cast<GBuffer::TextureType>(i)),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    mCommandList->ResourceBarrier(static_cast<UINT>(toSrv.size()), toSrv.data());

    TransitionResource(
        mCommandList.Get(),
        mPostProcessTexture.Get(),
        mPostProcessState,
        D3D12_RESOURCE_STATE_RENDER_TARGET);

    const float clearColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
    mCommandList->ClearRenderTargetView(mPostProcessRtv, clearColor, 0, nullptr);
    mCommandList->OMSetRenderTargets(1, &mPostProcessRtv, TRUE, nullptr);

    mCommandList->SetPipelineState(mRenderingSystem->GetLightingPSO());
    mCommandList->SetGraphicsRootSignature(mRenderingSystem->GetLightingRootSignature());
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    mCommandList->SetGraphicsRootDescriptorTable(0, GetGpuSrvHandle(mGBufferSrvStart));
    mCommandList->SetGraphicsRootConstantBufferView(1, mPassCB->Resource()->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRootConstantBufferView(3, mShadowCB->Resource()->GetGPUVirtualAddress());

    const unsigned int lightElementSize = mLightingCB->GetElementSize();
    unsigned int lightCbIndex = 0;
    const auto lightingCbAddress = [&](unsigned int index) -> D3D12_GPU_VIRTUAL_ADDRESS {
        return mLightingCB->Resource()->GetGPUVirtualAddress() + static_cast<UINT64>(index) * lightElementSize;
    };

    LightingConstants ambientConst = {};
    ambientConst.EnableAmbient = 1;
    mLightingCB->CopyData(static_cast<int>(lightCbIndex), ambientConst);
    mCommandList->SetGraphicsRootConstantBufferView(2, lightingCbAddress(lightCbIndex));
    mCommandList->DrawInstanced(3, 1, 0, 0);
    ++lightCbIndex;

    for (const auto& light : mLights) {
        if (lightCbIndex >= LightingCbElementCount) {
            break;
        }

        LightingConstants lightConst = {};
        lightConst.EnableAmbient = 0;
        lightConst.Light = light;
        mLightingCB->CopyData(static_cast<int>(lightCbIndex), lightConst);
        mCommandList->SetGraphicsRootConstantBufferView(2, lightingCbAddress(lightCbIndex));
        mCommandList->DrawInstanced(3, 1, 0, 0);
        ++lightCbIndex;
    }

    for (const auto& fl : mFallingLights) {
        if (lightCbIndex >= LightingCbElementCount) {
            break;
        }

        LightingConstants lightConst = {};
        lightConst.EnableAmbient = 0;
        lightConst.Light.Type = static_cast<unsigned int>(LightType::Point);
        lightConst.Light.Position = fl.position;
        lightConst.Light.Color = fl.color;
        lightConst.Light.Intensity = fl.intensity;
        lightConst.Light.Range = fl.range;
        mLightingCB->CopyData(static_cast<int>(lightCbIndex), lightConst);
        mCommandList->SetGraphicsRootConstantBufferView(2, lightingCbAddress(lightCbIndex));
        mCommandList->DrawInstanced(3, 1, 0, 0);
        ++lightCbIndex;
    }



    TransitionResource(
        mCommandList.Get(),
        mPostProcessTexture.Get(),
        mPostProcessState,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    auto bbToRt = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &bbToRt);

    auto rtv = CurrentBackBufferView();
    mCommandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
    mCommandList->OMSetRenderTargets(1, &rtv, TRUE, nullptr);
    DrawPostEffects(mCommandList.Get());

    auto bbToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &bbToPresent);

    ThrowIfFailed(mCommandList->Close(), "Close command list failed");

    ID3D12CommandList* cmdLists[] = {mCommandList.Get()};
    mCommandQueue->ExecuteCommandLists(1, cmdLists);

    ThrowIfFailed(mSwapChain->Present(1, 0), "Present failed");
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    FlushCommandQueue();
}

LRESULT DirectXApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_KEYDOWN:
        if ((lParam & (1LL << 30)) == 0 && wParam >= VK_NUMPAD1 && wParam <= VK_NUMPAD3) {
            const int scene = static_cast<int>(wParam - VK_NUMPAD1);
            if (scene != mCurrentScene) {
                mCurrentScene = scene;
                mDebugViewMode = 1;
                BuildScene();
            }
            return 0;
        }
        break;
    case WM_RBUTTONDOWN:
        mLastMousePos.x = GET_X_LPARAM(lParam);
        mLastMousePos.y = GET_Y_LPARAM(lParam);
        SetCapture(mHwnd);
        return 0;

    case WM_RBUTTONUP:
        ReleaseCapture();
        return 0;

    case WM_MOUSEMOVE:
        if (wParam & MK_RBUTTON) {
            const float dx = XMConvertToRadians(0.25f * static_cast<float>(GET_X_LPARAM(lParam) - mLastMousePos.x));
            const float dy = XMConvertToRadians(0.25f * static_cast<float>(GET_Y_LPARAM(lParam) - mLastMousePos.y));

            mYaw += dx;
            mPitch += dy;
            mPitch = std::clamp(mPitch, -1.45f, 1.45f);
        }

        mLastMousePos.x = GET_X_LPARAM(lParam);
        mLastMousePos.y = GET_Y_LPARAM(lParam);
        return 0;

    case WM_SIZE:
        if (mDevice && wParam != SIZE_MINIMIZED) {
            OnResize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void DirectXApp::FlushCommandQueue() {
    ++mCurrentFence;

    ThrowIfFailed(mCommandQueue->Signal(mFence.Get(), mCurrentFence), "Signal fence failed");

    if (mFence->GetCompletedValue() < mCurrentFence) {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrentFence, eventHandle), "SetEventOnCompletion failed");
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXApp::CurrentBackBufferView() const {
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
        static_cast<INT>(mCurrBackBuffer),
        mRtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE DirectXApp::DepthStencilView() const {
    return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

ID3D12Resource* DirectXApp::CurrentBackBuffer() const {
    return mSwapChainBuffer[mCurrBackBuffer].Get();
}

D3D12_GPU_DESCRIPTOR_HANDLE DirectXApp::GetGpuSrvHandle(unsigned int heapIndex) const {
    return CD3DX12_GPU_DESCRIPTOR_HANDLE(
        mCbvSrvHeap->GetGPUDescriptorHandleForHeapStart(),
        static_cast<INT>(heapIndex),
        mCbvSrvUavDescriptorSize);
}

void DirectXApp::TransitionResource(ID3D12GraphicsCommandList* cmdList,
                                    ID3D12Resource* resource,
                                    D3D12_RESOURCE_STATES& currentState,
                                    D3D12_RESOURCE_STATES targetState) {
    if (!cmdList || !resource || currentState == targetState) {
        return;
    }

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, currentState, targetState);
    cmdList->ResourceBarrier(1, &barrier);
    currentState = targetState;
}

void DirectXApp::InitializeParticleResources() {
    const UINT64 particleBufferBytes = static_cast<UINT64>(kMaxParticles) * sizeof(ParticleGpu);
    constexpr UINT64 counterBytes = 4096;

    for (unsigned int i = 0; i < 2; ++i) {
        const CD3DX12_HEAP_PROPERTIES defaultHeapProps(D3D12_HEAP_TYPE_DEFAULT);
        const CD3DX12_RESOURCE_DESC particleDesc = CD3DX12_RESOURCE_DESC::Buffer(
            particleBufferBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        ThrowIfFailed(mDevice->CreateCommittedResource(
            &defaultHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &particleDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&mParticleBuffers[i])),
            "Create particle structured buffer failed");

        const CD3DX12_RESOURCE_DESC counterDesc = CD3DX12_RESOURCE_DESC::Buffer(
            counterBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        ThrowIfFailed(mDevice->CreateCommittedResource(
            &defaultHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &counterDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&mParticleCounters[i])),
            "Create particle counter buffer failed");
    }

    {
        const CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
        const CD3DX12_RESOURCE_DESC resetDesc = CD3DX12_RESOURCE_DESC::Buffer(counterBytes);
        ThrowIfFailed(mDevice->CreateCommittedResource(
            &uploadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &resetDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&mParticleCounterResetUpload)),
            "Create particle counter reset upload buffer failed");

        void* mapped = nullptr;
        D3D12_RANGE readRange = {0, 0};
        ThrowIfFailed(mParticleCounterResetUpload->Map(0, &readRange, &mapped),
                      "Map particle counter reset upload buffer failed");
        std::memset(mapped, 0, static_cast<size_t>(counterBytes));
        D3D12_RANGE writeRange = {0, sizeof(unsigned int)};
        mParticleCounterResetUpload->Unmap(0, &writeRange);
    }

    {
        const CD3DX12_HEAP_PROPERTIES readbackHeapProps(D3D12_HEAP_TYPE_READBACK);
        const CD3DX12_RESOURCE_DESC readbackDesc = CD3DX12_RESOURCE_DESC::Buffer(counterBytes);
        ThrowIfFailed(mDevice->CreateCommittedResource(
            &readbackHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &readbackDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&mParticleCounterReadback)),
            "Create particle readback buffer failed");
    }

    mParticleSourceIndexA = true;
    mParticleAliveCount = 0;
    mParticleSpawnCount = 0;
    mParticleBufferStates = {D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON};
    mParticleCounterStates = {D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON};
}

void DirectXApp::UpdateParticleCounterFromReadback() {
    if (!mParticleCounterReadback) {
        return;
    }

    void* mapped = nullptr;
    D3D12_RANGE readRange = {0, sizeof(unsigned int)};
    if (SUCCEEDED(mParticleCounterReadback->Map(0, &readRange, &mapped)) && mapped) {
        const auto* counterPtr = reinterpret_cast<const unsigned int*>(mapped);
        mParticleAliveCount = (std::min)(*counterPtr, kMaxParticles);
        D3D12_RANGE writeRange = {0, 0};
        mParticleCounterReadback->Unmap(0, &writeRange);
    }
}

void DirectXApp::ResetParticles() {
    mParticleSourceIndexA = true;
    mParticleAliveCount = 0;
    mParticleSpawnCount = 0;
    mParticleSpawnAccumulator = 0.0f;
    mParticleReadbackValid = false;
}

void DirectXApp::UpdateParticles(float dt, float totalTime) {
    mParticleSpawnCount = 0;
    if (!mParticleSimCB || !mParticlesEnabled) {
        return;
    }

    UpdateParticleCounterFromReadback();
    constexpr float kSpawnRate = 260.0f;
    mParticleSpawnAccumulator += dt * kSpawnRate;
    mParticleSpawnCount = static_cast<unsigned int>(mParticleSpawnAccumulator);
    mParticleSpawnAccumulator -= static_cast<float>(mParticleSpawnCount);

    if (mParticleAliveCount >= kMaxParticles) {
        mParticleSpawnCount = 0;
    } else {
        const unsigned int freeCount = kMaxParticles - mParticleAliveCount;
        mParticleSpawnCount = (std::min)(mParticleSpawnCount, freeCount);
    }

    ParticleSimConstants sim = {};
    sim.Dt = dt;
    sim.TotalTime = totalTime;
    sim.AliveCount = mParticleAliveCount;
    sim.SpawnCount = mParticleSpawnCount;
    sim.EmitterPos = XMFLOAT3(0.0f, 5.0f, 0.0f);
    sim.BaseSize = 0.18f;
    sim.EmitterVelocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
    sim.Gravity = 9.8f;
    sim.LifeMin = 2.0f;
    sim.LifeMax = 4.0f;
    sim.SpeedMin = 2.0f;
    sim.SpeedMax = 5.0f;
    sim.MaxParticles = kMaxParticles;
    sim.CollisionCenter = XMFLOAT3(0.0f, 0.0f, 0.0f);
    sim.CollisionRadius = 0.0f;
    sim.Restitution = 0.5f;

    mParticleSimCB->CopyData(0, sim);
}

void DirectXApp::ExecuteParticleSimulation(ID3D12GraphicsCommandList* cmdList) {
    if (!cmdList || !mParticleSimCB || !mParticleBuffers[0] || !mParticleBuffers[1] ||
        !mParticleCounters[0] || !mParticleCounters[1] || !mParticleCounterReadback ||
        !mParticleCounterResetUpload) {
        return;
    }

    const unsigned int sourceIndex = mParticleSourceIndexA ? 0u : 1u;
    const unsigned int destIndex = 1u - sourceIndex;

    TransitionResource(
        cmdList,
        mParticleCounters[destIndex].Get(),
        mParticleCounterStates[destIndex],
        D3D12_RESOURCE_STATE_COPY_DEST);
    cmdList->CopyBufferRegion(
        mParticleCounters[destIndex].Get(),
        0,
        mParticleCounterResetUpload.Get(),
        0,
        sizeof(unsigned int));

    const unsigned int workItemCount = (std::max)(mParticleAliveCount, mParticleSpawnCount);
    if (workItemCount == 0u) {
        TransitionResource(
            cmdList,
            mParticleCounters[destIndex].Get(),
            mParticleCounterStates[destIndex],
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyBufferRegion(mParticleCounterReadback.Get(), 0, mParticleCounters[destIndex].Get(), 0, sizeof(unsigned int));
        mParticleSourceIndexA = (destIndex == 0u);
        return;
    }

    TransitionResource(
        cmdList,
        mParticleBuffers[sourceIndex].Get(),
        mParticleBufferStates[sourceIndex],
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitionResource(
        cmdList,
        mParticleBuffers[destIndex].Get(),
        mParticleBufferStates[destIndex],
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitionResource(
        cmdList,
        mParticleCounters[sourceIndex].Get(),
        mParticleCounterStates[sourceIndex],
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitionResource(
        cmdList,
        mParticleCounters[destIndex].Get(),
        mParticleCounterStates[destIndex],
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    cmdList->SetComputeRootSignature(mRenderingSystem->GetParticleRootSignature());
    cmdList->SetPipelineState(mRenderingSystem->GetParticleComputePSO());
    cmdList->SetComputeRootDescriptorTable(0, GetGpuSrvHandle(mParticleSrvIndex + sourceIndex));
    cmdList->SetComputeRootDescriptorTable(1, GetGpuSrvHandle(mParticleUavStartIndex + sourceIndex));
    cmdList->SetComputeRootDescriptorTable(2, GetGpuSrvHandle(mParticleUavStartIndex + destIndex));
    cmdList->SetComputeRootConstantBufferView(3, mParticleSimCB->Resource()->GetGPUVirtualAddress());

    const unsigned int groupsX = (workItemCount + 255u) / 256u;
    cmdList->Dispatch(groupsX, 1, 1);

    D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(mParticleBuffers[destIndex].Get());
    cmdList->ResourceBarrier(1, &uavBarrier);

    TransitionResource(
        cmdList,
        mParticleCounters[destIndex].Get(),
        mParticleCounterStates[destIndex],
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    cmdList->CopyBufferRegion(mParticleCounterReadback.Get(), 0, mParticleCounters[destIndex].Get(), 0, sizeof(unsigned int));

    mParticleSourceIndexA = (destIndex == 0u);
}

void DirectXApp::DrawParticles(ID3D12GraphicsCommandList* cmdList, const XMMATRIX& view, const XMMATRIX& proj) {
    if (mParticleAliveCount == 0u || !cmdList || !mParticleRenderCB) {
        return;
    }

    const unsigned int sourceIndex = mParticleSourceIndexA ? 0u : 1u;

    TransitionResource(
        cmdList,
        mParticleBuffers[sourceIndex].Get(),
        mParticleBufferStates[sourceIndex],
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    const XMVECTOR forward = XMVector3Normalize(XMVectorSet(
        std::cos(mPitch) * std::sin(mYaw),
        std::sin(mPitch),
        std::cos(mPitch) * std::cos(mYaw),
        0.0f));
    const XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMVECTOR right = XMVector3Normalize(XMVector3Cross(worldUp, forward));
    const XMVECTOR up = XMVector3Normalize(XMVector3Cross(forward, right));

    ParticleRenderConstants render = {};
    XMStoreFloat4x4(&render.ViewProj, XMMatrixTranspose(view * proj));
    XMStoreFloat3(&render.CameraRight, right);
    XMStoreFloat3(&render.CameraUp, up);
    render.RenderSizeScale = 1.0f;
    render.AlphaDiscard = 0.5f;
    mParticleRenderCB->CopyData(0, render);

    cmdList->SetPipelineState(mRenderingSystem->GetParticlePSO());
    cmdList->SetGraphicsRootSignature(mRenderingSystem->GetParticleRootSignature());
    cmdList->SetGraphicsRootDescriptorTable(0, GetGpuSrvHandle(mParticleSrvIndex + sourceIndex));
    cmdList->SetGraphicsRootConstantBufferView(4, mParticleRenderCB->Resource()->GetGPUVirtualAddress());
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    cmdList->DrawInstanced(mParticleAliveCount, 1, 0, 0);
}

