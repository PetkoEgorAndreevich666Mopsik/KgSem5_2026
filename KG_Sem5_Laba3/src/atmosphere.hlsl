static const float AtmospherePi = 3.14159265359f;

struct Atmosphere
{
    float3 Rayleigh;
    float Mie;
    float3 SunDirection;
    float SunIntensity;
};

float OpticalMass(float verticalCosine)
{
    float cosine = saturate(verticalCosine);
    float angle = acos(cosine) * 180.0f / AtmospherePi;
    return 1.0f / (cosine + 0.15f * pow(93.885f - angle, -1.253f));
}

float RayleighPhase(float cosine)
{
    return 3.0f * (1.0f + cosine * cosine) / (16.0f * AtmospherePi);
}

float MiePhase(float cosine)
{
    const float g = 0.8f;
    float denominator = max(1.0f + g * g - 2.0f * g * clamp(cosine, -1.0f, 1.0f), 1e-4f);
    return (1.0f - g * g) / (4.0f * AtmospherePi * denominator * sqrt(denominator));
}

float3 AtmosphereTransmittance(Atmosphere medium, float distance)
{
    return exp(-(medium.Rayleigh + medium.Mie) * max(distance, 0.0f));
}

float3 SunIrradiance(Atmosphere medium)
{
    float mass = OpticalMass(medium.SunDirection.y);
    return medium.SunIntensity * exp(-(medium.Rayleigh * 8400.0f + medium.Mie * 1250.0f) * mass)
        * smoothstep(-0.02f, 0.0f, medium.SunDirection.y);
}

float3 SurfaceInScattering(Atmosphere medium, float cosine, float distance)
{
    float3 extinction = medium.Rayleigh + medium.Mie;
    float3 scattering = medium.Rayleigh * RayleighPhase(cosine) + medium.Mie * MiePhase(cosine);
    return SunIrradiance(medium) * scattering / max(extinction, 1e-8f) * (1.0f - AtmosphereTransmittance(medium, distance));
}

float3 AtmosphereSky(Atmosphere medium, float3 direction)
{
    float cosine = dot(direction, medium.SunDirection);
    float mass = OpticalMass(direction.y);
    float airPath = 8400.0f * mass;
    float hazePath = 1250.0f * mass;
    float3 hazeTransmission = AtmosphereTransmittance(medium, hazePath);
    float3 outerScattering = RayleighPhase(cosine) * (1.0f - exp(-medium.Rayleigh * (airPath - hazePath)));
    return SurfaceInScattering(medium, cosine, hazePath) + SunIrradiance(medium) * outerScattering * hazeTransmission;
}
