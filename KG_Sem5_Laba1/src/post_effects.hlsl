Texture2D gSceneTex : register(t0);
Texture2D gAlbedoTex : register(t1);
Texture2D gNormalTex : register(t2);
Texture2D gDepthTex : register(t3);
SamplerState gLinearClamp : register(s0);

cbuffer PostConstants : register(b0)
{
    float2 gInvRenderTargetSize;
    float gTime;
    float gBloomStrength;
    float gVignetteStrength;
    float3 gPadding;
};

struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD0;
};

VSOut VS_FullscreenQuad(uint vertexId : SV_VertexID)
{
    float2 pos[4] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f, -1.0f),
        float2( 1.0f,  1.0f)
    };

    float2 uv[4] =
    {
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, 0.0f)
    };

    VSOut output;
    output.PosH = float4(pos[vertexId], 0.0f, 1.0f);
    output.TexC = uv[vertexId];
    return output;
}

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 ExtractBloom(float2 uv)
{
    const float3 color = gSceneTex.Sample(gLinearClamp, uv).rgb;
    const float brightness = Luminance(color);
    const float bloomMask = smoothstep(0.72f, 1.15f, brightness);
    return color * bloomMask;
}

float3 ApplyBloom(float3 color, float2 uv)
{
    const float2 texel = gInvRenderTargetSize * 2.0f;
    float3 bloom = ExtractBloom(uv) * 0.18f;

    [unroll]
    for (int y = -2; y <= 2; ++y)
    {
        [unroll]
        for (int x = -2; x <= 2; ++x)
        {
            if (x == 0 && y == 0)
            {
                continue;
            }

            const float2 offset = float2((float)x, (float)y);
            const float weight = 1.0f / (1.0f + dot(offset, offset));
            bloom += ExtractBloom(uv + offset * texel) * weight * 0.09f;
        }
    }

    return color + bloom * gBloomStrength;
}

float3 ApplyVignette(float3 color, float2 uv)
{
    const float2 centeredUv = uv * 2.0f - 1.0f;
    const float vignette = smoothstep(1.15f, 0.18f, dot(centeredUv, centeredUv));
    return color * lerp(1.0f, vignette, gVignetteStrength);
}

float4 PS_PostEffects(VSOut input) : SV_Target
{
    const float2 uv = input.TexC;
    float3 color = gSceneTex.Sample(gLinearClamp, uv).rgb;

    color = ApplyBloom(color, uv);
    color = ApplyVignette(color, uv);

    color = pow(max(color / (color + 1.0f), 0.0f), 1.0f / 2.2f);
    return float4(color, 1.0f);
}
