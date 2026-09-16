struct ParticleGpu
{
    float3 Position;
    float Size;
    float3 Velocity;
    float Age;
    float4 Color;
    float Lifetime;
    float3 Padding;
};

StructuredBuffer<ParticleGpu> gParticleSrv : register(t0);
ConsumeStructuredBuffer<ParticleGpu> gConsumeParticles : register(u0);
AppendStructuredBuffer<ParticleGpu> gAppendParticles : register(u1);

cbuffer ParticleSimConstants : register(b0)
{
    float gDt;
    float gTotalTime;
    uint gAliveCount;
    uint gSpawnCount;

    float3 gEmitterPos;
    float gBaseSize;

    float3 gEmitterVelocity;
    float gGravity;

    float gLifeMin;
    float gLifeMax;
    float gSpeedMin;
    float gSpeedMax;

    uint gMaxParticles;
    float3 gPad0;

    float3 gCollisionCenter;
    float gCollisionRadius;

    float gRestitution;
    float3 gPad1;
};

cbuffer ParticleRenderConstants : register(b1)
{
    float4x4 gViewProj;
    float3 gUnused0;
    float gRenderSizeScale;
    float3 gUnused1;
    float gUnused2;
};

float Hash01(uint n)
{
    n = (n << 13u) ^ n;
    uint nn = n * (n * n * 15731u + 789221u) + 1376312589u;
    return frac((float)nn * (1.0f / 4294967296.0f));
}

float3 BuildSpawnDirection(uint seed, float3 baseDir)
{
    float rx = Hash01(seed * 3u + 11u) * 2.0f - 1.0f;
    float ry = Hash01(seed * 3u + 17u) * 2.0f - 1.0f;
    float rz = Hash01(seed * 3u + 23u) * 2.0f - 1.0f;

    float3 jitter = normalize(float3(rx, ry, rz));
    return normalize(baseDir * 1.7f + jitter * 0.7f);
}

[numthreads(256, 1, 1)]
void CS_UpdateParticles(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint id = dispatchThreadId.x;

    if (id < gAliveCount)
    {
        ParticleGpu p = gConsumeParticles.Consume();
        p.Age += gDt;
        p.Velocity.y -= gGravity * gDt;
        p.Position += p.Velocity * gDt;

        if (gCollisionRadius > 0.0f)
        {
            const float particleRadius = max(p.Size * 0.95f, 0.01f);
            const float3 fromCenter = p.Position - gCollisionCenter;
            const float dist = length(fromCenter);
            const float collideDist = gCollisionRadius + particleRadius;

            if (dist < collideDist)
            {
                const float3 n = (dist > 1e-5f) ? (fromCenter / dist) : float3(0.0f, 1.0f, 0.0f);
                p.Position = gCollisionCenter + n * collideDist;

                const float vn = dot(p.Velocity, n);
                if (vn < 0.0f)
                {
                    p.Velocity = p.Velocity - (1.0f + gRestitution) * vn * n;
                }
            }
        }

        if (p.Age < p.Lifetime && p.Position.y > -20.0f)
        {
            gAppendParticles.Append(p);
        }
    }

    if (id < gSpawnCount && (gAliveCount + id) < gMaxParticles)
    {
        const uint seed = id + asuint(gTotalTime * 997.0f) * 37u;

        ParticleGpu p;
        p.Position = gEmitterPos;

        float3 baseDir = float3(0.0f, -1.0f, 0.0f);
        if (gCollisionRadius > 0.0f)
        {
            const float3 toCenter = gCollisionCenter - gEmitterPos;
            const float lenToCenter = length(toCenter);
            if (lenToCenter > 1e-5f)
            {
                baseDir = toCenter / lenToCenter;
            }
        }

        const float3 dir = BuildSpawnDirection(seed, baseDir);
        const float speedT = Hash01(seed * 5u + 101u);
        const float speed = lerp(gSpeedMin, gSpeedMax, speedT);
        p.Velocity = dir * speed + gEmitterVelocity;

        const float lifeT = Hash01(seed * 7u + 211u);
        p.Lifetime = lerp(gLifeMin, gLifeMax, lifeT);
        p.Age = 0.0f;

        p.Size = gBaseSize * lerp(0.75f, 1.25f, Hash01(seed * 13u + 59u));
        p.Color = float4(1.0f, 0.35f, 0.72f, 1.0f);
        p.Padding = 0.0f.xxx;

        gAppendParticles.Append(p);
    }
}

struct VSOut
{
    float4 PosH : SV_POSITION;
    float3 NormalW : NORMAL;
    float4 Color : COLOR0;
};

struct VSInput
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float3 TangentL : TANGENT;
    float3 BitangentL : BINORMAL;
    float2 TexC : TEXCOORD;
};

float3 RotateY(float3 p, float angle)
{
    const float s = sin(angle);
    const float c = cos(angle);
    return float3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
}

VSOut VS_Particle(VSInput vin, uint instanceId : SV_InstanceID)
{
    ParticleGpu p = gParticleSrv[instanceId];

    float lifeT = 1.0f - saturate(p.Age / max(p.Lifetime, 1e-4f));
    const float yaw = p.Age * 3.0f + p.Position.x * 1.7f + p.Position.z * 0.9f;
    const float scale = p.Size * gRenderSizeScale;
    const float3 posW = RotateY(vin.PosL * scale, yaw) + p.Position;
    const float3 normalW = normalize(RotateY(vin.NormalL, yaw));

    VSOut vout;
    vout.PosH = mul(float4(posW, 1.0f), gViewProj);
    vout.NormalW = normalW;
    vout.Color = float4(p.Color.rgb, lifeT);
    return vout;
}

float4 PS_Particle(VSOut pin) : SV_Target
{
    const float3 lightDir = normalize(float3(-0.35f, 0.8f, -0.45f));
    const float ndotl = saturate(dot(normalize(pin.NormalW), lightDir));
    const float shade = 0.35f + ndotl * 0.65f;
    return float4(pin.Color.rgb * shade, 1.0f);
}
