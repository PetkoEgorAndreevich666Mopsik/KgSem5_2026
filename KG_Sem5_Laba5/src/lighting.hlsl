Texture2D gAlbedoTex : register(t0);
Texture2D gNormalTex : register(t1);
Texture2D gDepthTex : register(t2);
Texture2DArray gShadowMap : register(t3);
TextureCube gIrradiance : register(t4);
TextureCube gPrefilter : register(t5);
Texture2D gBrdfLut : register(t6);
Texture2D<float4> gRtMask : register(t7);
SamplerState gLinearClamp : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

#include "atmosphere.hlsl"

cbuffer PassConstants : register(b0)
{
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float gPassPadding;
    float4 gAmbientColor;
    float4 gBrush;
    float4 gAtmosphereParameters;
    float4 gShadowSettings;
    float4 gShadowLight;
    float4 gShadowOptions;
};

struct LightData
{
    float3 Position;
    float Range;
    float3 Direction;
    float SpotAngle;
    float3 Color;
    float Intensity;
    uint Type;
    float3 Padding;
};

cbuffer LightConstants : register(b1)
{
    LightData gLight;
    uint gEnableAmbient;
    float3 gDummy;
};

cbuffer ShadowConstants : register(b2)
{
    float4x4 gLightViewProj[3];
    float4 gCascadeSplits;
    float4 gLightDirection;
};

static const int kNumCascades = 3;
static const float kShadowMapSize = 1024.0f;
static const float kBias = 0.0005f;

struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VSOut VS_Fullscreen(uint vid : SV_VertexID)
{
    VSOut vout;
    float2 pos[3] = { float2(-1, -1), float2(-1, 3), float2(3, -1) };
    float2 uv[3] = { float2(0, 1), float2(0, -1), float2(2, 1) };
    vout.PosH = float4(pos[vid], 0, 1);
    vout.TexC = uv[vid];
    return vout;
}

float3 ReconstructWorldPos(float2 uv, float depth, float4x4 invViewProj)
{
    float2 ndc = uv * 2.0f - 1.0f;
    ndc.y = -ndc.y;
    float4 clip = float4(ndc, depth, 1.0f);
    float4 world = mul(clip, invViewProj);
    return world.xyz / world.w;
}

static const float PI = 3.14159265359f;

float3 Fresnel(float cosTheta, float3 f0)
{
    return f0 + (1.0f - f0) * pow(1.0f - saturate(cosTheta), 5.0f);
}

float Geometry(float ndot, float k)
{
    return ndot / max(ndot * (1.0f - k) + k, 0.0001f);
}

float3 DirectBRDF(float3 P, float3 N, float3 V, float3 albedo, float roughness, float metallic)
{
    float3 L = normalize(-gLight.Direction);
    float attenuation = 1.0f;
    if (gLight.Type != 0) {
        float3 delta = gLight.Position - P;
        float distance = length(delta);
        L = delta / max(distance, 0.0001f);
        attenuation = pow(saturate(1.0f - distance / max(gLight.Range, 0.0001f)), 2.0f);
        if (gLight.Type == 2)
            attenuation *= smoothstep(gLight.SpotAngle, gLight.SpotAngle + 0.08f, dot(normalize(-gLight.Direction), L));
    }
    float3 H = normalize(V + L);
    float nv = max(saturate(dot(N, V)), 0.0001f);
    float nl = saturate(dot(N, L));
    float nh = saturate(dot(N, H));
    float a2 = pow(roughness, 4.0f);
    float denominator = nh * nh * (a2 - 1.0f) + 1.0f;
    float D = a2 / max(PI * denominator * denominator, 0.000001f);
    float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
    float G = Geometry(nv, k) * Geometry(nl, k);
    float3 F = Fresnel(dot(H, V), lerp(0.04f.xxx, albedo, metallic));
    float3 specular = D * G * F / max(4.0f * nv * nl, 0.0001f);
    float3 sunlight = 1.0f.xxx;
    if (gLight.Type == 0) {
        Atmosphere medium;
        medium.Rayleigh = float3(6.95e-6f, 1.18e-5f, 2.44e-5f) * gAtmosphereParameters.x;
        medium.Mie = gAtmosphereParameters.y;
        medium.SunDirection = L;
        medium.SunIntensity = 1.0f;
        sunlight = SunIrradiance(medium);
    }
    return ((1.0f - F) * (1.0f - metallic) * albedo / PI + specular)
        * gLight.Color * sunlight * gLight.Intensity * attenuation * nl;
}

float ComputeShadowFactor(float3 worldPos, float3 normalW, float viewDepth)
{
    int cascadeIndex = kNumCascades - 1;
    for (int i = 0; i < kNumCascades - 1; ++i)
    {
        if (viewDepth < gCascadeSplits[i])
        {
            cascadeIndex = i;
            break;
        }
    }

    float4 shadowPos = mul(float4(worldPos, 1.0f), gLightViewProj[cascadeIndex]);
    float3 projCoords = shadowPos.xyz / shadowPos.w;
    projCoords.xy = projCoords.xy * 0.5f + 0.5f;
    projCoords.y = 1.0f - projCoords.y;

    if (projCoords.x < 0.0f || projCoords.x > 1.0f ||
        projCoords.y < 0.0f || projCoords.y > 1.0f ||
        projCoords.z < 0.0f || projCoords.z > 1.0f)
    {
        return 1.0f;
    }

    float3 lightDirection = normalize(-gLightDirection.xyz);
    float ndotl = saturate(dot(normalW, lightDirection));
    float bias = (0.00005f + (1.0f - ndotl) * 0.00025f) * (1.0f + 0.18f * cascadeIndex);
    float shadow = 0.0f;
    float2 texelSize = 1.0f / kShadowMapSize;

    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float2 offset = float2(x, y) * texelSize;
            shadow += gShadowMap.SampleCmpLevelZero(
                gShadowSampler,
                float3(projCoords.xy + offset, (float)cascadeIndex),
                saturate(projCoords.z - bias));
        }
    }

    return shadow / 9.0f;
}

float FilteredVisibility(int2 pixel, float3 position, float3 normal)
{
    float4 center = gRtMask.Load(int3(pixel, 0));
    if (gShadowSettings.x < 2 || gShadowSettings.y == 0) return center.x;
    if (gShadowSettings.w == 0) return center.y;
    uint width, height;
    gRtMask.GetDimensions(width, height);
    float2 size = float2(width, height);
    float depth = gDepthTex.Load(int3(pixel, 0)).r;
    float3 adjacent = ReconstructWorldPos((pixel + float2(1.5, 0.5)) / size, depth, gInvViewProj);
    float footprint = max(length(adjacent - position), 0.00001);
    float radius = clamp(center.w / footprint * 0.2, 1, 3);
    float total = 0;
    float weightSum = 0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            int2 samplePixel = clamp(pixel + int2(round(float2(x, y) * radius)), int2(0, 0), int2(width - 1, height - 1));
            float sampleDepth = gDepthTex.Load(int3(samplePixel, 0)).r;
            float3 sampleNormal = normalize(gNormalTex.Load(int3(samplePixel, 0)).xyz * 2 - 1);
            float3 samplePosition = ReconstructWorldPos((samplePixel + 0.5) / size, sampleDepth, gInvViewProj);
            float planeDistance = abs(dot(samplePosition - position, normal));
            float weight = exp(-0.5 * (x*x + y*y)) * pow(saturate(dot(normal, sampleNormal)), 32)
                * exp(-planeDistance / max(footprint * 0.5, 0.001));
            if (sampleDepth >= 1 || length(samplePosition - position) > footprint * 12) weight = 0;
            total += gRtMask.Load(int3(samplePixel, 0)).y * weight;
            weightSum += weight;
        }
    }
    return lerp(center.y, total / max(weightSum, 0.00001), saturate(center.w / footprint));
}

float4 PS_Lighting(VSOut pin) : SV_Target
{
    float2 uv = pin.TexC;
    float4 material = gAlbedoTex.Sample(gLinearClamp, uv);
    float3 albedo = material.rgb;
    float metallic = saturate(material.a);
    float roughness = clamp(gNormalTex.Sample(gLinearClamp, uv).a, 0.04f, 1.0f);
    float3 normalPacked = gNormalTex.Sample(gLinearClamp, uv).rgb;
    float depth = gDepthTex.Sample(gLinearClamp, uv).r;

    if (depth >= 1.0f) {
        return 0.0f.xxxx;
    }
    float3 normalW = normalize(normalPacked * 2.0f - 1.0f);
    float3 worldPos = ReconstructWorldPos(uv, depth, gInvViewProj);

    if (gShadowOptions.y > 0 && gShadowSettings.x >= 0) {
        if (gEnableAmbient == 0) return 0;
        int2 pixel = int2(pin.PosH.xy);
        float4 mask = gRtMask.Load(int3(pixel, 0));
        float value = gShadowOptions.y == 1 ? mask.x : (gShadowOptions.y == 2 ? mask.y :
            (gShadowOptions.y == 3 ? FilteredVisibility(pixel, worldPos, normalW) : mask.z / (1 + mask.z)));
        return float4(value.xxx, 0);
    }

    float3 V = normalize(gEyePosW - worldPos);
    float3 color = float3(0.0f, 0.0f, 0.0f);

    if (gEnableAmbient != 0)
    {
        float nv = saturate(dot(normalW, V));
        float3 f0 = lerp(0.04f.xxx, albedo, metallic);
        float3 F = f0 + (max((1.0f - roughness).xxx, f0) - f0) * pow(1.0f - nv, 5.0f);
        float3 diffuse = gIrradiance.Sample(gLinearClamp, normalW).rgb * albedo;
        uint width, height, levels;
        gPrefilter.GetDimensions(0, width, height, levels);
        float3 reflection = gPrefilter.SampleLevel(gLinearClamp, reflect(-V, normalW), roughness * (levels - 1)).rgb;
        float2 brdf = gBrdfLut.Sample(gLinearClamp, float2(nv, roughness)).rg;
        Atmosphere medium;
        medium.Rayleigh = float3(6.95e-6f, 1.18e-5f, 2.44e-5f) * gAtmosphereParameters.x;
        medium.Mie = gAtmosphereParameters.y;
        medium.SunDirection = normalize(-gLightDirection.xyz);
        medium.SunIntensity = 20.0f;
        float3 skyLight = AtmosphereSky(medium, float3(0.0f, 1.0f, 0.0f)) * 0.25f;
        color = ((1.0f - F) * (1.0f - metallic) * diffuse + reflection * (F * brdf.x + brdf.y)) * skyLight;
    }
    else
    {
        color = DirectBRDF(worldPos, normalW, V, albedo, roughness, metallic);

        if (gLight.Type == 0)
        {
            const float nearZ = 0.1f;
            const float farZ = 1500.0f;
            float viewDepth = nearZ * farZ / (farZ - depth * (farZ - nearZ));
            float shadow = gShadowSettings.x < 0 ? ComputeShadowFactor(worldPos, normalW, viewDepth) :
                (gShadowSettings.x == 0 ? 1 : FilteredVisibility(int2(pin.PosH.xy), worldPos, normalW));
            color *= shadow;
        }
    }

    return float4(color, 0.0f);
}
