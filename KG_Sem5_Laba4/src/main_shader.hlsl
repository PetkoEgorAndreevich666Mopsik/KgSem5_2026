Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gDisplacementMap : register(t2);
SamplerState gLinearWrap : register(s0);
SamplerState gTerrainClamp : register(s1);

cbuffer ObjectConstants : register(b0)
{
    float4x4 gWorldViewProj;
    float4x4 gWorld;
    float4x4 gTextureTransform;
    float gTotalTime;
    float3 gTimePadding;
    float4 gObjectParams;
};

cbuffer PassConstants : register(b1)
{
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float gPassPadding;
    float4 gAmbientColor;
    float4 gBrush;
};

struct VSInput
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float3 TangentL : TANGENT;
    float3 BitangentL : BINORMAL;
    float2 TexC : TEXCOORD;
};

struct PixelIn
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float3 TangentW : TANGENT;
    float3 BitangentW : BINORMAL;
    float2 TexC : TEXCOORD;
    float TessFactor : TEXCOORD1;
};

PixelIn VS_Geometry(VSInput vin)
{
    PixelIn vout;

    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    float3x3 world3x3 = (float3x3)gWorld;
    vout.NormalW = normalize(mul(vin.NormalL, world3x3));
    vout.TangentW = normalize(mul(vin.TangentL, world3x3));
    vout.BitangentW = normalize(mul(vin.BitangentL, world3x3));

    float4 tex = mul(float4(vin.TexC, 0.0f, 1.0f), gTextureTransform);
    vout.TexC = tex.xy;
    vout.PosH = mul(float4(vout.PosW, 1.0f), gWorldViewProj);
    vout.TessFactor = 1.0f;

    return vout;
}

struct ControlPoint
{
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float3 TangentW : TANGENT;
    float3 BitangentW : BINORMAL;
    float2 TexC : TEXCOORD;
};

ControlPoint VS_ControlPoint(VSInput vin)
{
    ControlPoint cp;

    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    cp.PosW = posW.xyz;

    float3x3 world3x3 = (float3x3)gWorld;
    cp.NormalW = normalize(mul(vin.NormalL, world3x3));
    cp.TangentW = normalize(mul(vin.TangentL, world3x3));
    cp.BitangentW = normalize(mul(vin.BitangentL, world3x3));

    float4 tex = mul(float4(vin.TexC, 0.0f, 1.0f), gTextureTransform);
    cp.TexC = tex.xy;

    return cp;
}

struct HSConstants
{
    float EdgeTess[3] : SV_TessFactor;
    float InsideTess : SV_InsideTessFactor;
};

HSConstants HS_Constants(InputPatch<ControlPoint, 3> patch, uint patchId : SV_PrimitiveID)
{
    HSConstants hsc;

    float3 center = (patch[0].PosW + patch[1].PosW + patch[2].PosW) / 3.0f;
    float distanceToCamera = length(center - gEyePosW);

    float tessFactor = max(1.0, min(16.0, lerp(16.0, 2.0, saturate((distanceToCamera - 4.0) / 42.0))));
    if (gObjectParams.z > 0.0f)
    {
        tessFactor = max(tessFactor, gObjectParams.z);
    }

    hsc.EdgeTess[0] = tessFactor;
    hsc.EdgeTess[1] = tessFactor;
    hsc.EdgeTess[2] = tessFactor;
    hsc.InsideTess = tessFactor;

    return hsc;
}

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("HS_Constants")]
ControlPoint HS_Main(InputPatch<ControlPoint, 3> patch, uint cpId : SV_OutputControlPointID)
{
    return patch[cpId];
}

float ComputeWaterMask(float3 albedo)
{
    float blueDominance = albedo.b - max(albedo.r * 0.72f, albedo.g * 0.9f);
    float darkSurface = saturate((0.5f - dot(albedo, float3(0.25f, 0.5f, 0.25f))) * 2.1f);
    float waterMask = saturate((blueDominance + 0.02f) * 10.0f + darkSurface * 0.5f);
    return saturate(pow(waterMask, 1.35f));
}

[domain("tri")]
PixelIn DS_Main(HSConstants hsc, float3 bary : SV_DomainLocation, const OutputPatch<ControlPoint, 3> patch)
{
    PixelIn outV;

    float3 posW = bary.x * patch[0].PosW + bary.y * patch[1].PosW + bary.z * patch[2].PosW;
    float3 normalW = normalize(bary.x * patch[0].NormalW + bary.y * patch[1].NormalW + bary.z * patch[2].NormalW);
    float3 tangentW = normalize(bary.x * patch[0].TangentW + bary.y * patch[1].TangentW + bary.z * patch[2].TangentW);
    float3 bitangentW = normalize(bary.x * patch[0].BitangentW + bary.y * patch[1].BitangentW + bary.z * patch[2].BitangentW);
    float2 texC = bary.x * patch[0].TexC + bary.y * patch[1].TexC + bary.z * patch[2].TexC;

    float height = gDisplacementMap.SampleLevel(gLinearWrap, texC, 0.0f).r;
    float displacement = (height - 0.5f) * 2.0f;
    float dispStrength = (gObjectParams.y > 0.0f) ? gObjectParams.y : 0.2f;
        posW += normalW * (displacement * dispStrength);

    if (gObjectParams.w > 1.5f)
    {
        float3 baseAlbedo = gDiffuseMap.SampleLevel(gLinearWrap, texC, 0.0f).rgb;
        float waterMask = ComputeWaterMask(baseAlbedo);

        float2 waveUv = texC * float2(54.0f, 28.0f);
        float phase0 = waveUv.x * 1.1f + gTotalTime * 1.8f;
        float phase1 = waveUv.y * 2.1f - gTotalTime * 2.5f;
        float phase2 = dot(waveUv, float2(1.2f, 1.55f)) + gTotalTime * 3.2f;
        float phase3 = dot(waveUv, float2(-1.6f, 0.85f)) - gTotalTime * 2.2f;

        float wave = sin(phase0) * 0.7f + sin(phase1) * 0.48f + sin(phase2) * 0.3f + sin(phase3) * 0.2f;
        float2 slope;
        slope.x = cos(phase0) * 0.7f * 1.1f + cos(phase2) * 0.3f * 1.2f - cos(phase3) * 0.2f * 1.6f;
        slope.y = cos(phase1) * 0.48f * 2.1f + cos(phase2) * 0.3f * 1.55f + cos(phase3) * 0.2f * 0.85f;

        posW += normalW * (wave * 0.042f * waterMask);

        float3 waterNormal = normalize(normalW - tangentW * (slope.x * 0.34f * waterMask) - bitangentW * (slope.y * 0.34f * waterMask));
        normalW = normalize(lerp(normalW, waterNormal, waterMask));
    }

    outV.PosW = posW;
    outV.NormalW = normalW;
    outV.TangentW = tangentW;
    outV.BitangentW = bitangentW;
    outV.TexC = texC;
    outV.PosH = mul(float4(posW, 1.0f), gWorldViewProj);
    outV.TessFactor = hsc.InsideTess;

    return outV;
}

struct GBufferOut
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
    float Depth : SV_Target2;
};

GBufferOut PS_Geometry(PixelIn pin)
{
    GBufferOut pout;

    float4 albedo = gDiffuseMap.Sample(gLinearWrap, pin.TexC);
    float3 normalW = normalize(pin.NormalW);

    float3 normalTS = gNormalMap.Sample(gLinearWrap, pin.TexC).xyz * 2.0f - 1.0f;
    float3 T = normalize(pin.TangentW);
    float3 B = normalize(pin.BitangentW);
    float3 N = normalize(pin.NormalW);
    float3x3 TBN = float3x3(T, B, N);
    normalW = normalize(mul(normalTS, TBN));

    if (gObjectParams.w < 0.0f) {
        uint width, height;
        gDiffuseMap.GetDimensions(width, height);
        float2 uv = (saturate(pin.TexC) * (float2(width, height) - 1.0f) + 0.5f) / float2(width, height);
        albedo = gDiffuseMap.Sample(gTerrainClamp, uv);
        normalW = normalize(gNormalMap.Sample(gTerrainClamp, uv).xyz * 2.0f - 1.0f);
        float3 original = normalize(pin.BitangentW);
        float3 current = normalize(pin.NormalW);
        float2 slope = normalW.xz / max(normalW.y, 0.05f)
            + current.xz / max(current.y, 0.05f) - original.xz / max(original.y, 0.05f);
        normalW = normalize(float3(slope.x, 1.0f, slope.y));
        if (pin.TangentW.y < -0.5f) {
            normalW = normalize(pin.NormalW);
            albedo.rgb *= 0.65f;
        }
    }

    if (gObjectParams.w > 1.5f)
    {
        float waterMask = ComputeWaterMask(albedo.rgb);
        float fresnel = pow(1.0f - saturate(dot(normalize(gEyePosW - pin.PosW), normalW)), 3.0f);
        float waveHighlight = pow(saturate(1.0f - normalW.y), 2.2f);
        float3 waterTint = lerp(float3(0.01f, 0.09f, 0.19f), float3(0.14f, 0.45f, 0.62f), fresnel);
        float3 waterColor = albedo.rgb * 0.24f + waterTint + (fresnel * 0.22f + waveHighlight * 0.18f);
        albedo.rgb = lerp(albedo.rgb, waterColor, saturate(waterMask * 1.15f));
    }

    if (gObjectParams.x >= 1.5f && gObjectParams.x < 2.5f)
    {
        albedo = float4(normalW * 0.5f + 0.5f, 1.0f);
    }
    else if (gObjectParams.x >= 2.5f)
    {
        float t = gObjectParams.w < 0.0f ? gTimePadding.z : saturate((pin.TessFactor - 1.0f) / 7.0f);
        float3 tessColor = lerp(float3(0.1f, 0.2f, 1.0f), float3(1.0f, 0.1f, 0.05f), t);
        albedo = float4(tessColor, 1.0f);
    }

    if (gObjectParams.w < 0.0f && gBrush.w > 0.0f && pin.TangentW.y > -0.5f) {
        float distance = length(pin.PosW.xz - gBrush.xy);
        float width = max(0.15f, fwidth(distance) * 1.5f);
        float ring = 1.0f - smoothstep(width, width * 2.0f, abs(distance - gBrush.z));
        albedo.rgb = lerp(albedo.rgb, float3(1.0f, 0.85f, 0.05f), ring);
    }
    pout.Albedo = float4(pow(max(albedo.rgb, 0.0f), 2.2f), saturate(gTimePadding.y));
    pout.Normal = float4(normalW * 0.5f + 0.5f, gObjectParams.w > 1.5f ? lerp(0.8f, 0.12f, ComputeWaterMask(albedo.rgb)) : clamp(gTimePadding.x, 0.04f, 1.0f));
    pout.Depth = pin.PosH.z;

    return pout;
}

cbuffer ShadowPassConstants : register(b0)
{
    float4x4 gShadowWorldViewProj;
};

struct ShadowVSInput
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float3 TangentL : TANGENT;
    float3 BitangentL : BINORMAL;
    float2 TexC : TEXCOORD;
};

struct ShadowVSOut
{
    float4 PosH : SV_POSITION;
};

ShadowVSOut VS_Shadow(ShadowVSInput vin)
{
    ShadowVSOut vout;
    vout.PosH = mul(float4(vin.PosL, 1.0f), gShadowWorldViewProj);
    return vout;
}
