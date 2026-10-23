#pragma once

#include <DirectXMath.h>
#include <array>
#include <intsafe.h>

using namespace DirectX;

enum class LightType : unsigned int {
    Directional = 0,
    Point = 1,
    Spot = 2
};

struct LightData {
    XMFLOAT3 Position = {0.0f, 0.0f, 0.0f};
    float Range = 25.0f;

    XMFLOAT3 Direction = {0.0f, -1.0f, 0.0f};
    float SpotAngle = 0.7f;

    XMFLOAT3 Color = {1.0f, 1.0f, 1.0f};
    float Intensity = 1.0f;

    unsigned int Type = static_cast<unsigned int>(LightType::Point);
    XMFLOAT3 Padding = {0.0f, 0.0f, 0.0f};
};

static constexpr unsigned int kMaxLights = 64;

struct LightingConstants {
    LightData Light;
    UINT      EnableAmbient;
    DirectX::XMFLOAT3 Padding;
};

static constexpr unsigned int kNumCascades = 3;

struct ShadowConstants {
    XMFLOAT4X4 LightViewProj[3];
    XMFLOAT4 CascadeSplits;
    XMFLOAT4 LightDirection;
};
