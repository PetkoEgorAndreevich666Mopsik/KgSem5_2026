Texture2D gAlbedoTex : register(t0);
Texture2D gNormalTex : register(t1);
Texture2D gDepthTex : register(t2);
Texture2DArray gShadowMap : register(t3);
TextureCube gIrradiance : register(t4);
TextureCube gPrefilter : register(t5);
Texture2D gBrdfLut : register(t6);
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
    return ((1.0f - F) * (1.0f - metallic) * albedo / PI + specular)
        * gLight.Color * gLight.Intensity * attenuation * nl;
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

float4 PS_Lighting(VSOut pin) : SV_Target
{
    float2 uv = pin.TexC;
    float4 material = gAlbedoTex.Sample(gLinearClamp, uv);
    float3 albedo = material.rgb;
    float metallic = saturate(material.a);
    float roughness = clamp(gNormalTex.Sample(gLinearClamp, uv).a, 0.04f, 1.0f);
    float3 normalPacked = gNormalTex.Sample(gLinearClamp, uv).rgb;
    float depth = gDepthTex.Sample(gLinearClamp, uv).r;

    if (depth >= 1.0f) return float4(0.0f, 0.0f, 0.0f, 0.0f);
    float3 normalW = normalize(normalPacked * 2.0f - 1.0f);
    float3 worldPos = ReconstructWorldPos(uv, depth, gInvViewProj);

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
        color = (1.0f - F) * (1.0f - metallic) * diffuse + reflection * (F * brdf.x + brdf.y);
    }
    else
    {
        color = DirectBRDF(worldPos, normalW, V, albedo, roughness, metallic);

        if (gLight.Type == 0)
        {
            const float nearZ = 0.1f;
            const float farZ = 260.0f;
            float viewDepth = nearZ * farZ / (farZ - depth * (farZ - nearZ));
            float shadow = ComputeShadowFactor(worldPos, normalW, viewDepth);
            color *= shadow;
        }
    }

    return float4(color, 0.0f);
}
