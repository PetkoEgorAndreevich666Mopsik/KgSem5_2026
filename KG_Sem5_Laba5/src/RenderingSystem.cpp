#include "RenderingSystem.h"

#include "d3dUtil.h"
#include "../h/d3dx12.h"

#include <array>

using Microsoft::WRL::ComPtr;

bool RenderingSystem::Initialize(ID3D12Device* device,
                                 unsigned int width,
                                 unsigned int height,
                                 DXGI_FORMAT backBufferFormat,
                                 DXGI_FORMAT depthStencilFormat) {
    mWidth = width;
    mHeight = height;
    mBackBufferFormat = backBufferFormat;
    mDepthStencilFormat = depthStencilFormat;

    mGBuffer = std::make_unique<GBuffer>();
    if (!mGBuffer->Initialize(device, width, height)) {
        return false;
    }

    BuildRootSignatures(device);
    BuildPSOs(device);
    return true;
}

void RenderingSystem::OnResize(ID3D12Device* device, unsigned int width, unsigned int height) {
    mWidth = width;
    mHeight = height;
    if (mGBuffer) {
        mGBuffer->OnResize(device, width, height);
    }
}

void RenderingSystem::BuildRootSignatures(ID3D12Device* device) {
    {
        CD3DX12_DESCRIPTOR_RANGE ranges[3];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
        ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);

        CD3DX12_ROOT_PARAMETER params[10];
        params[0].InitAsConstantBufferView(0);
        params[1].InitAsConstantBufferView(1);
        params[2].InitAsDescriptorTable(1, &ranges[0], D3D12_SHADER_VISIBILITY_ALL);
        params[3].InitAsDescriptorTable(1, &ranges[1], D3D12_SHADER_VISIBILITY_ALL);
        params[4].InitAsDescriptorTable(1, &ranges[2], D3D12_SHADER_VISIBILITY_ALL);

        CD3DX12_STATIC_SAMPLER_DESC linearWrap(
            0,
            D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP,
            D3D12_TEXTURE_ADDRESS_MODE_WRAP);

        CD3DX12_STATIC_SAMPLER_DESC terrainClamp(
            1, D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        const D3D12_STATIC_SAMPLER_DESC geometrySamplers[] = {linearWrap, terrainClamp};
        CD3DX12_ROOT_SIGNATURE_DESC desc(
            5,
            params,
            2,
            geometrySamplers,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> errors;
        ThrowIfFailed(D3D12SerializeRootSignature(
            &desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serialized,
            &errors),
            "Serialize geometry root signature failed");

        ThrowIfFailed(device->CreateRootSignature(
            0,
            serialized->GetBufferPointer(),
            serialized->GetBufferSize(),
            IID_PPV_ARGS(&mGeometryRootSignature)),
            "Create geometry root signature failed");

        D3D12_FEATURE_DATA_D3D12_OPTIONS7 options{};
        D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{D3D_SHADER_MODEL_6_5};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options, sizeof(options))) &&
            options.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1 &&
            SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel))) &&
            shaderModel.HighestShaderModel >= D3D_SHADER_MODEL_6_5) {
            for (UINT i = 0; i < 4; ++i)
                params[5 + i].InitAsShaderResourceView(3 + i, 0, D3D12_SHADER_VISIBILITY_MESH);
            params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            params[9].InitAsConstants(3, 2, 0, D3D12_SHADER_VISIBILITY_AMPLIFICATION);
            desc.NumParameters = _countof(params);
            desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
            ThrowIfFailed(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors),
                          "Serialize mesh root signature failed");
            ThrowIfFailed(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                          IID_PPV_ARGS(&mMeshRootSignature)), "Create mesh root signature failed");
        }
    }

    {
        CD3DX12_DESCRIPTOR_RANGE srvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0);

        CD3DX12_ROOT_PARAMETER params[4];
        params[0].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);
        params[1].InitAsConstantBufferView(0);
        params[2].InitAsConstantBufferView(1);
        params[3].InitAsConstantBufferView(2);

        CD3DX12_STATIC_SAMPLER_DESC samplers[2] = {};
        samplers[0].Init(
            0,
            D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        samplers[1].Init(
            1,
            D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
            D3D12_TEXTURE_ADDRESS_MODE_BORDER,
            D3D12_TEXTURE_ADDRESS_MODE_BORDER,
            D3D12_TEXTURE_ADDRESS_MODE_BORDER,
            0.0f,
            16,
            D3D12_COMPARISON_FUNC_LESS_EQUAL,
            D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE);

        CD3DX12_ROOT_SIGNATURE_DESC desc(
            4,
            params,
            2,
            samplers,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> errors;
        ThrowIfFailed(D3D12SerializeRootSignature(
            &desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serialized,
            &errors),
            "Serialize lighting root signature failed");

        ThrowIfFailed(device->CreateRootSignature(
            0,
            serialized->GetBufferPointer(),
            serialized->GetBufferSize(),
            IID_PPV_ARGS(&mLightingRootSignature)),
            "Create lighting root signature failed");
    }

    {
        CD3DX12_ROOT_PARAMETER params[1];
        params[0].InitAsConstantBufferView(0);

        CD3DX12_ROOT_SIGNATURE_DESC desc(
            1,
            params,
            0,
            nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> errors;
        ThrowIfFailed(D3D12SerializeRootSignature(
            &desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serialized,
            &errors),
            "Serialize shadow root signature failed");

        ThrowIfFailed(device->CreateRootSignature(
            0,
            serialized->GetBufferPointer(),
            serialized->GetBufferSize(),
            IID_PPV_ARGS(&mShadowRootSignature)),
            "Create shadow root signature failed");
    }

    {
        CD3DX12_DESCRIPTOR_RANGE particleSrvRange;
        particleSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

        CD3DX12_DESCRIPTOR_RANGE consumeUavRange;
        consumeUavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

        CD3DX12_DESCRIPTOR_RANGE appendUavRange;
        appendUavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1);

        CD3DX12_ROOT_PARAMETER params[5];
        params[0].InitAsDescriptorTable(1, &particleSrvRange, D3D12_SHADER_VISIBILITY_ALL);
        params[1].InitAsDescriptorTable(1, &consumeUavRange, D3D12_SHADER_VISIBILITY_ALL);
        params[2].InitAsDescriptorTable(1, &appendUavRange, D3D12_SHADER_VISIBILITY_ALL);
        params[3].InitAsConstantBufferView(0);
        params[4].InitAsConstantBufferView(1);

        CD3DX12_ROOT_SIGNATURE_DESC desc(
            5,
            params,
            0,
            nullptr,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> errors;
        ThrowIfFailed(D3D12SerializeRootSignature(
            &desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serialized,
            &errors),
            "Serialize particle root signature failed");

        ThrowIfFailed(device->CreateRootSignature(
            0,
            serialized->GetBufferPointer(),
            serialized->GetBufferSize(),
            IID_PPV_ARGS(&mParticleRootSignature)),
            "Create particle root signature failed");
    }

    {
        CD3DX12_DESCRIPTOR_RANGE srvRange;
        srvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);

        CD3DX12_ROOT_PARAMETER params[2];
        params[0].InitAsDescriptorTable(1, &srvRange, D3D12_SHADER_VISIBILITY_PIXEL);
        params[1].InitAsConstantBufferView(0);

        CD3DX12_STATIC_SAMPLER_DESC linearClamp(
            0,
            D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

        CD3DX12_ROOT_SIGNATURE_DESC desc(
            2,
            params,
            1,
            &linearClamp,
            D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> errors;
        ThrowIfFailed(D3D12SerializeRootSignature(
            &desc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &serialized,
            &errors),
            "Serialize post root signature failed");

        ThrowIfFailed(device->CreateRootSignature(
            0,
            serialized->GetBufferPointer(),
            serialized->GetBufferSize(),
            IID_PPV_ARGS(&mPostRootSignature)),
            "Create post root signature failed");
    }
}

void RenderingSystem::BuildMeshPSOs(ID3D12Device* device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& geometry) {
    ComPtr<ID3D12Device2> streamDevice;
    ThrowIfFailed(device->QueryInterface(IID_PPV_ARGS(&streamDevice)), "Get mesh pipeline device");
    ComPtr<ID3DBlob> amplification, mesh, pixel;
    ThrowIfFailed(D3DReadFileToBlob(L"mesh_amplification.cso", &amplification), "Read amplification shader");
    ThrowIfFailed(D3DReadFileToBlob(L"mesh_geometry.cso", &mesh), "Read mesh shader");
    ThrowIfFailed(D3DReadFileToBlob(L"mesh_pixel.cso", &pixel), "Read mesh pixel shader");

    struct {
        CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE rootSignature;
        CD3DX12_PIPELINE_STATE_STREAM_SUBOBJECT<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS> as;
        CD3DX12_PIPELINE_STATE_STREAM_SUBOBJECT<D3D12_SHADER_BYTECODE, D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS> ms;
        CD3DX12_PIPELINE_STATE_STREAM_PS ps;
        CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY topology;
        CD3DX12_PIPELINE_STATE_STREAM_RASTERIZER rasterizer;
        CD3DX12_PIPELINE_STATE_STREAM_BLEND_DESC blend;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL depthStencil;
        CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT depthFormat;
        CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS targets;
        CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_MASK sampleMask;
        CD3DX12_PIPELINE_STATE_STREAM_SAMPLE_DESC samples;
    } stream;
    stream.rootSignature = mMeshRootSignature.Get();
    stream.as = {amplification->GetBufferPointer(), amplification->GetBufferSize()};
    stream.ms = {mesh->GetBufferPointer(), mesh->GetBufferSize()};
    stream.ps = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    stream.topology = geometry.PrimitiveTopologyType;
    stream.rasterizer = CD3DX12_RASTERIZER_DESC(geometry.RasterizerState);
    stream.blend = CD3DX12_BLEND_DESC(geometry.BlendState);
    stream.depthStencil = CD3DX12_DEPTH_STENCIL_DESC(geometry.DepthStencilState);
    stream.depthFormat = geometry.DSVFormat;
    stream.sampleMask = geometry.SampleMask;
    stream.samples = geometry.SampleDesc;
    D3D12_RT_FORMAT_ARRAY targets{};
    targets.NumRenderTargets = geometry.NumRenderTargets;
    for (UINT i = 0; i < targets.NumRenderTargets; ++i) targets.RTFormats[i] = geometry.RTVFormats[i];
    stream.targets = targets;

    const D3D12_PIPELINE_STATE_STREAM_DESC desc{sizeof(stream), &stream};
    ThrowIfFailed(streamDevice->CreatePipelineState(&desc, IID_PPV_ARGS(&mMeshPSO)), "Create mesh PSO");
    auto wireframe = geometry.RasterizerState;
    wireframe.FillMode = D3D12_FILL_MODE_WIREFRAME;
    stream.rasterizer = CD3DX12_RASTERIZER_DESC(wireframe);
    ThrowIfFailed(streamDevice->CreatePipelineState(&desc, IID_PPV_ARGS(&mMeshWirePSO)), "Create mesh wireframe PSO");
}

void RenderingSystem::BuildPSOs(ID3D12Device* device) {
    auto vsGeom = d3dUtil::CompileShader(L"src/main_shader.hlsl", nullptr, "VS_Geometry", "vs_5_0");
    auto vsCp = d3dUtil::CompileShader(L"src/main_shader.hlsl", nullptr, "VS_ControlPoint", "vs_5_0");
    auto hs = d3dUtil::CompileShader(L"src/main_shader.hlsl", nullptr, "HS_Main", "hs_5_0");
    auto ds = d3dUtil::CompileShader(L"src/main_shader.hlsl", nullptr, "DS_Main", "ds_5_0");
    auto psGeom = d3dUtil::CompileShader(L"src/main_shader.hlsl", nullptr, "PS_Geometry", "ps_5_0");

    auto vsLighting = d3dUtil::CompileShader(L"src/lighting.hlsl", nullptr, "VS_Fullscreen", "vs_5_0");
    auto psLighting = d3dUtil::CompileShader(L"src/lighting.hlsl", nullptr, "PS_Lighting", "ps_5_0");

    std::array<D3D12_INPUT_ELEMENT_DESC, 5> inputLayout = {
        D3D12_INPUT_ELEMENT_DESC{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        D3D12_INPUT_ELEMENT_DESC{"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        D3D12_INPUT_ELEMENT_DESC{"TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        D3D12_INPUT_ELEMENT_DESC{"BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        D3D12_INPUT_ELEMENT_DESC{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC geomDesc = {};
    geomDesc.InputLayout = {inputLayout.data(), static_cast<UINT>(inputLayout.size())};
    geomDesc.pRootSignature = mGeometryRootSignature.Get();
    geomDesc.VS = {vsGeom->GetBufferPointer(), vsGeom->GetBufferSize()};
    geomDesc.PS = {psGeom->GetBufferPointer(), psGeom->GetBufferSize()};
    geomDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    geomDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    geomDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    geomDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    geomDesc.SampleMask = UINT_MAX;
    geomDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    geomDesc.NumRenderTargets = 3;
    geomDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    geomDesc.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    geomDesc.RTVFormats[2] = DXGI_FORMAT_R32_FLOAT;
    geomDesc.DSVFormat = mDepthStencilFormat;
    geomDesc.SampleDesc.Count = 1;
    ThrowIfFailed(device->CreateGraphicsPipelineState(&geomDesc, IID_PPV_ARGS(&mGeometryPSO)),
                  "Create geometry PSO failed");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC geomWireDesc = geomDesc;
    geomWireDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    ThrowIfFailed(device->CreateGraphicsPipelineState(&geomWireDesc, IID_PPV_ARGS(&mGeometryWirePSO)),
                  "Create geometry wireframe PSO failed");
    if (mMeshRootSignature) BuildMeshPSOs(device, geomDesc);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC tessDesc = geomDesc;
    tessDesc.VS = {vsCp->GetBufferPointer(), vsCp->GetBufferSize()};
    tessDesc.HS = {hs->GetBufferPointer(), hs->GetBufferSize()};
    tessDesc.DS = {ds->GetBufferPointer(), ds->GetBufferSize()};
    tessDesc.PS = {psGeom->GetBufferPointer(), psGeom->GetBufferSize()};
    tessDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    ThrowIfFailed(device->CreateGraphicsPipelineState(&tessDesc, IID_PPV_ARGS(&mTessellationPSO)),
                  "Create tessellation PSO failed");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC tessWireDesc = tessDesc;
    tessWireDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    ThrowIfFailed(device->CreateGraphicsPipelineState(&tessWireDesc, IID_PPV_ARGS(&mTessellationWirePSO)),
                  "Create tessellation wireframe PSO failed");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC lightDesc = {};
    lightDesc.InputLayout = {nullptr, 0};
    lightDesc.pRootSignature = mLightingRootSignature.Get();
    lightDesc.VS = {vsLighting->GetBufferPointer(), vsLighting->GetBufferSize()};
    lightDesc.PS = {psLighting->GetBufferPointer(), psLighting->GetBufferSize()};
    lightDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    lightDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    lightDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    lightDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
    lightDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    lightDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
    lightDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    lightDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    lightDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
    lightDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

    lightDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    lightDesc.DepthStencilState.DepthEnable = FALSE;
    lightDesc.DepthStencilState.StencilEnable = FALSE;

    lightDesc.SampleMask = UINT_MAX;
    lightDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    lightDesc.NumRenderTargets = 1;
    lightDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    lightDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    lightDesc.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&lightDesc, IID_PPV_ARGS(&mLightingPSO)),
                  "Create lighting PSO failed");

    auto vsShadow = d3dUtil::CompileShader(L"src/main_shader.hlsl", nullptr, "VS_Shadow", "vs_5_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowDesc = {};
    shadowDesc.InputLayout = {inputLayout.data(), static_cast<UINT>(inputLayout.size())};
    shadowDesc.pRootSignature = mShadowRootSignature.Get();
    shadowDesc.VS = {vsShadow->GetBufferPointer(), vsShadow->GetBufferSize()};
    shadowDesc.PS = {nullptr, 0};
    shadowDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    shadowDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    shadowDesc.RasterizerState.DepthBias = 100;
    shadowDesc.RasterizerState.DepthBiasClamp = 0.002f;
    shadowDesc.RasterizerState.SlopeScaledDepthBias = 1.5f;
    shadowDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    shadowDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    shadowDesc.DepthStencilState.DepthEnable = TRUE;
    shadowDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    shadowDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    shadowDesc.SampleMask = UINT_MAX;
    shadowDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    shadowDesc.NumRenderTargets = 0;
    shadowDesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    shadowDesc.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&shadowDesc, IID_PPV_ARGS(&mShadowPSO)),
                  "Create shadow PSO failed");

    auto vsParticle = d3dUtil::CompileShader(L"src/particles.hlsl", nullptr, "VS_Particle", "vs_5_0");
    auto gsParticle = d3dUtil::CompileShader(L"src/particles.hlsl", nullptr, "GS_Particle", "gs_5_0");
    auto psParticle = d3dUtil::CompileShader(L"src/particles.hlsl", nullptr, "PS_Particle", "ps_5_0");
    auto csParticleUpdate = d3dUtil::CompileShader(L"src/particles.hlsl", nullptr, "CS_UpdateParticles", "cs_5_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC particleDesc = {};
    particleDesc.InputLayout = {nullptr, 0};
    particleDesc.pRootSignature = mParticleRootSignature.Get();
    particleDesc.VS = {vsParticle->GetBufferPointer(), vsParticle->GetBufferSize()};
    particleDesc.GS = {gsParticle->GetBufferPointer(), gsParticle->GetBufferSize()};
    particleDesc.PS = {psParticle->GetBufferPointer(), psParticle->GetBufferSize()};
    particleDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    particleDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    particleDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    particleDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
    particleDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    particleDesc.DepthStencilState.DepthEnable = TRUE;
    particleDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    particleDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    particleDesc.DepthStencilState.StencilEnable = FALSE;
    particleDesc.SampleMask = UINT_MAX;
    particleDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    particleDesc.NumRenderTargets = 1;
    particleDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    particleDesc.DSVFormat = mDepthStencilFormat;
    particleDesc.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&particleDesc, IID_PPV_ARGS(&mParticleGraphicsPSO)),
                  "Create particle graphics PSO failed");

    D3D12_COMPUTE_PIPELINE_STATE_DESC particleCsDesc = {};
    particleCsDesc.pRootSignature = mParticleRootSignature.Get();
    particleCsDesc.CS = {csParticleUpdate->GetBufferPointer(), csParticleUpdate->GetBufferSize()};

    ThrowIfFailed(device->CreateComputePipelineState(&particleCsDesc, IID_PPV_ARGS(&mParticleComputePSO)),
                  "Create particle compute PSO failed");

    auto vsPost = d3dUtil::CompileShader(L"src/post_effects.hlsl", nullptr, "VS_FullscreenQuad", "vs_5_0");
    auto psPost = d3dUtil::CompileShader(L"src/post_effects.hlsl", nullptr, "PS_PostEffects", "ps_5_0");

    D3D12_GRAPHICS_PIPELINE_STATE_DESC postDesc = {};
    postDesc.InputLayout = {nullptr, 0};
    postDesc.pRootSignature = mPostRootSignature.Get();
    postDesc.VS = {vsPost->GetBufferPointer(), vsPost->GetBufferSize()};
    postDesc.PS = {psPost->GetBufferPointer(), psPost->GetBufferSize()};
    postDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    postDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    postDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    postDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    postDesc.DepthStencilState.DepthEnable = FALSE;
    postDesc.DepthStencilState.StencilEnable = FALSE;
    postDesc.SampleMask = UINT_MAX;
    postDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    postDesc.NumRenderTargets = 1;
    postDesc.RTVFormats[0] = mBackBufferFormat;
    postDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
    postDesc.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&postDesc, IID_PPV_ARGS(&mPostPSO)),
                  "Create post PSO failed");
}
