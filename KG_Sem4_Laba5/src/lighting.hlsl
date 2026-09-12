Texture2D gAlbedoTex : register(t0);
Texture2D gNormalTex : register(t1);
Texture2D gDepthTex : register(t2);
Texture2DArray gShadowMap : register(t3);
SamplerState gLinearClamp : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

cbuffer PassConstants : register(b0)
{
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float gPassPadding;
    float4 gAmbientColor;
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
    float4 gDebugInfo;
};

static const int kNumCascades = 3;
static const float kShadowMapSize = 1024.0f;
static const float kBias = 0.0005f;
static const float3 kCascadeColors[3] = {
    float3(1.0f, 0.41f, 0.71f),
    float3(1.0f, 0.0f, 0.0f),
    float3(0.0f, 1.0f, 0.0f)
};

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

float3 ComputeDirectional(float3 N, float3 albedo, LightData light)
{
    float3 L = normalize(-light.Direction);
    float ndotl = saturate(dot(N, L));
    return albedo * light.Color * (light.Intensity * ndotl);
}

float3 ComputePoint(float3 P, float3 N, float3 albedo, LightData light)
{
    float3 toLight = light.Position - P;
    float dist = length(toLight);
    if (dist > light.Range)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 L = toLight / max(dist, 1e-4f);
    float ndotl = saturate(dot(N, L));
    float atten = saturate(1.0f - dist / max(light.Range, 1e-4f));
    atten *= atten;

    return albedo * light.Color * (light.Intensity * ndotl * atten);
}

float3 ComputeSpot(float3 P, float3 N, float3 albedo, LightData light)
{
    float3 toLight = light.Position - P;
    float dist = length(toLight);
    if (dist > light.Range)
    {
        return float3(0.0f, 0.0f, 0.0f);
    }

    float3 L = toLight / max(dist, 1e-4f);
    float cone = dot(normalize(-light.Direction), L);
    float spot = smoothstep(light.SpotAngle, light.SpotAngle + 0.08f, cone);

    float ndotl = saturate(dot(N, L));
    float atten = saturate(1.0f - dist / max(light.Range, 1e-4f));
    atten *= atten;

    return albedo * light.Color * (light.Intensity * ndotl * atten * spot);
}

float ComputeShadowFactor(float3 worldPos, float3 normalW, float viewDepth, out int cascadeIndex)
{
    cascadeIndex = kNumCascades - 1;
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

float4 PS_Lighting(VSOut pin) : SV_Target
{
    float2 uv = pin.TexC;
    float3 albedo = gAlbedoTex.Sample(gLinearClamp, uv).rgb;
    float3 normalPacked = gNormalTex.Sample(gLinearClamp, uv).rgb;
    float depth = gDepthTex.Sample(gLinearClamp, uv).r;

    float3 normalW = normalize(normalPacked * 2.0f - 1.0f);
    float3 worldPos = ReconstructWorldPos(uv, depth, gInvViewProj);

    float3 color = float3(0.0f, 0.0f, 0.0f);

    if (gEnableAmbient != 0)
    {
        color = gAmbientColor.rgb * albedo;
    }
    else
    {
        if (gLight.Type == 0)
        {
            color = ComputeDirectional(normalW, albedo, gLight);
        }
        else if (gLight.Type == 1)
        {
            color = ComputePoint(worldPos, normalW, albedo, gLight);
        }
        else if (gLight.Type == 2)
        {
            color = ComputeSpot(worldPos, normalW, albedo, gLight);
        }

        if (gLight.Type == 0)
        {
            const float nearZ = 0.1f;
            const float farZ = 260.0f;
            float viewDepth = nearZ * farZ / (farZ - depth * (farZ - nearZ));
            int cascadeIndex = 0;
            float shadow = ComputeShadowFactor(worldPos, normalW, viewDepth, cascadeIndex);
            color *= shadow;

            if (gDebugInfo.x > 0.5f)
            {
                color = kCascadeColors[cascadeIndex] * (0.6f + 0.4f * shadow);
            }
        }
    }

    return float4(color, 0.0f);
}
