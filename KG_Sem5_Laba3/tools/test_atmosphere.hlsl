#include "../src/atmosphere.hlsl"

RWStructuredBuffer<float4> results : register(u0);

[numthreads(1, 1, 1)]
void CS_Test(uint3 id : SV_DispatchThreadID)
{
    Atmosphere medium;
    medium.Rayleigh = float3(6.95e-6f, 1.18e-5f, 2.44e-5f);
    medium.Mie = 2e-6f;
    medium.SunDirection = normalize(float3(0.3f, 0.8f, 0.4f));
    medium.SunIntensity = 20.0f;

    float3 nearT = AtmosphereTransmittance(medium, 10.0f);
    float3 farT = AtmosphereTransmittance(medium, 1000.0f);
    results[0] = float4(all(AtmosphereTransmittance(medium, 0) == 1),
        all(nearT > farT), all(farT > 0), all(nearT <= 1));
    results[1] = float4(all(SurfaceInScattering(medium, 0.5f, 0) == 0),
        all(SurfaceInScattering(medium, 0.5f, 1000) > SurfaceInScattering(medium, 0.5f, 10)),
        MiePhase(1) > MiePhase(0) * 10, abs(RayleighPhase(-0.5f) - RayleighPhase(0.5f)) < 1e-6f);

    medium.Mie = 0;
    float3 blueSky = AtmosphereSky(medium, float3(0, 1, 0));
    results[2] = float4(blueSky.b > blueSky.g, blueSky.g > blueSky.r,
        all(isfinite(AtmosphereSky(medium, float3(1, 0, 0)))),
        all(isfinite(AtmosphereSky(medium, float3(0, -1, 0)))));

    medium.Mie = 0.0006f;
    results[3] = float4(all(AtmosphereTransmittance(medium, 1000) < farT),
        all(isfinite(AtmosphereSky(medium, medium.SunDirection))),
        all(SunIrradiance(medium) > 0), all(isfinite(SurfaceInScattering(medium, 1, 1500))));

    medium.Rayleigh = 0;
    float3 greySky = AtmosphereSky(medium, float3(0, 1, 0));
    results[4] = float4(abs(greySky.r - greySky.b) < 1e-6f,
        all(AtmosphereSky(medium, medium.SunDirection) > AtmosphereSky(medium, -medium.SunDirection)),
        all(greySky > 0), OpticalMass(0) > OpticalMass(1));

    medium.Mie = 0;
    results[5] = float4(all(AtmosphereSky(medium, float3(0, 1, 0)) == 0),
        all(SurfaceInScattering(medium, 1, 1000) == 0),
        all(AtmosphereTransmittance(medium, 1000) == 1), all(isfinite(SunIrradiance(medium))));

    medium.Rayleigh = float3(6.95e-6f, 1.18e-5f, 2.44e-5f);
    medium.Mie = 2e-6f;
    bool orbitFinite = true;
    for (int i = 0; i <= 24; ++i) {
        float angle = i * AtmospherePi / 12.0f;
        medium.SunDirection = float3(0, sin(angle), cos(angle));
        orbitFinite = orbitFinite && all(isfinite(SunIrradiance(medium)))
            && all(isfinite(AtmosphereSky(medium, float3(0, 1, 0))))
            && all(SunIrradiance(medium) >= 0);
    }
    medium.SunDirection = float3(0, -1, 0);
    bool nightDark = all(SunIrradiance(medium) == 0) && all(AtmosphereSky(medium, float3(0, 1, 0)) == 0);
    medium.SunDirection = float3(0, 1, 0);
    float3 zenithSun = SunIrradiance(medium);
    medium.SunDirection = float3(0, 0, 1);
    results[6] = float4(orbitFinite, nightDark, all(zenithSun > SunIrradiance(medium)), all(zenithSun > 0));
}
