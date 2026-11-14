Texture2D<float4> gAlbedo : register(t0);
Texture2D<float4> gNormal : register(t1);
Texture2D<float> gDepth : register(t2);
RaytracingAccelerationStructure gScene : register(t3);
RWTexture2D<float4> gMask : register(u0);
cbuffer Pass : register(b0) {
    float4x4 gInvViewProj;
    float3 gEye;
    float gPadding;
    float4 gAmbient;
    float4 gBrush;
    float4 gAtmosphere;
    float4 gShadowSettings;
    float4 gShadowLight;
    float4 gShadowOptions;
};

float3 World(float2 uv, float depth) {
    float4 p = mul(float4(uv.x * 2 - 1, 1 - uv.y * 2, depth, 1), gInvViewProj);
    return p.xyz / p.w;
}

float Hit(float3 origin, float3 direction) {
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0;
    ray.TMax = 100000;
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> query;
    query.TraceRayInline(gScene, RAY_FLAG_NONE, 255, ray);
    while (query.Proceed()) {}
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? query.CommittedRayT() : -1;
}

[numthreads(8, 8, 1)]
void CS_Shadow(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    gMask.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    float depth = gDepth[id.xy];
    if (depth >= 1 || gShadowSettings.x == 0) {
        gMask[id.xy] = float4(1, 1, 0, 0);
        return;
    }
    float3 p = World((id.xy + 0.5) / float2(width, height), depth);
    float3 n = normalize(gNormal[id.xy].xyz * 2 - 1);
    float3 light = normalize(gShadowLight.xyz);
    float3 toEye = p - gEye;
    float depthError = dot(toEye, toEye) * 5.96046448e-8 / max(gShadowOptions.z, 0.1);
    float bias = max(gShadowOptions.x, max(depthError, max(abs(p.x), max(abs(p.y), abs(p.z))) * 0.000002));
    float3 origin = p + n * bias + light * bias;
    float hit = Hit(origin, light);
    float hard = hit < 0 ? 1 : 0;
    if (gShadowSettings.x == 1 || gShadowSettings.y == 0) {
        gMask[id.xy] = float4(hard, hard, max(hit, 0), 0);
        return;
    }
    float3 tangent = normalize(cross(abs(light.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0), light));
    float3 bitangent = cross(light, tangent);
    uint count = (uint)gShadowSettings.z;
    float rotation = frac(sin(dot(float2(id.xy), float2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    float visible = 0;
    float nearest = hit < 0 ? 100000 : hit;
    for (uint i = 0; i < count; ++i) {
        float radius = sqrt((i + 0.5) / count) * tan(gShadowSettings.y);
        float angle = i * 2.39996323 + rotation;
        float3 direction = normalize(light + radius * (cos(angle) * tangent + sin(angle) * bitangent));
        float distance = Hit(origin, direction);
        visible += distance < 0 ? 1 : 0;
        if (distance >= 0) nearest = min(nearest, distance);
    }
    float penumbra = nearest < 100000 ? nearest * tan(gShadowSettings.y) : 0;
    gMask[id.xy] = float4(hard, visible / count, nearest < 100000 ? nearest : 0, penumbra);
}
