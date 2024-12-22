#ifndef PBR_GLSL
#define PBR_GLSL 1

#extension GL_EXT_scalar_block_layout : require

struct PointLight
{
    vec3 position;
    vec3 color;
    float radius;
    bool castShadow;
};

layout(scalar, set = 0, binding = 1) readonly buffer PointLights
{
    PointLight lights[];
} pointLights;

layout(std430, set = 0, binding = 2) uniform LightInformation
{
    vec4 ambientLight;
    uint pointLightCount;
} lightInfo;

struct AmbientLight
{
    vec4 color;
};

struct DirectLight
{
    vec4 direction;
    vec4 color;
};

//----------PBR INPUT----------//

struct BRDFInput
{
    vec4 baseColor; //w holds alpha
    vec4 emissive;
    float metallic;
    float roughness;
};

//----------CALCULATION FUNCTIONS----------//

#define PI 3.14159265359

//lambertian diffuse
vec3 diffuse(vec3 N, vec3 L, vec3 baseColor)
{
    return (max(dot(N, L), 0.0) / PI) * baseColor;
}

//Trowbridge–Reitz(GGX)
float normalDistribution(vec3 N, vec3 H, float roughness)
{
    const float a2 = roughness * roughness;
    const float NdotH = max(dot(N, H), 0.0);
    const float NdotH2 = NdotH * NdotH;

    const float denominator = (NdotH2 * (a2 - 1.0)) + 1.0;
    return a2 / (PI * denominator * denominator);
}

//Schlick approximation with Spherical Gaussian approximation as exponent (from ue4 presentation from 2013)
vec3 fresnel(vec3 V, vec3 H, vec3 F0)
{
    const float cosTheta = max(dot(V, H), 0.0);
    const float exponent = (-5.55473 * cosTheta) - (6.98316 * cosTheta);

    return F0 + ((1.0 - F0) * pow(2.0, exponent));
}

float shlickGGX(vec3 A, vec3 B, float roughness)
{
    const float k = ((roughness + 1.0) * (roughness + 1.0)) / 8.0;
    const float AdotB = max(dot(A, B), 0.0);
    const float denominator = (AdotB * (1.0 - k)) + k;

    return AdotB / denominator;
}

float geometricAttenuation(vec3 N, vec3 L, vec3 V, float roughness)
{
    return shlickGGX(N, L, roughness) * shlickGGX(N, V, roughness);
}

//specular calculation
vec3 cookTorance(const vec3 N, const vec3 V, const vec3 L, const vec3 H, const vec3 F, const float roughness)
{
    const float D = normalDistribution(N, H, roughness);
    const float G = geometricAttenuation(N, L, V, roughness);

    const vec3 numerator = D * F * G;
    const float denominator = max(4.0 * max(dot(N, L), 0.0) * max(dot(N, V), 0.0), 0.0001);

    return numerator / denominator;
}

vec3 calculateLight(const vec3 N, const vec3 V, const vec3 L, const vec3 H, BRDFInput inputValues)
{
    inputValues.roughness = max(inputValues.roughness, 0.001);
    const vec3 F0 = mix(vec3(0.04), inputValues.baseColor.xyz, inputValues.metallic);
    const vec3 F = fresnel(V, H, F0);

    vec3 kD = vec3(1.0) - F;
    kD *= 1.0 - inputValues.metallic;

    vec3 diffuse = diffuse(N, L, inputValues.baseColor.xyz);
    vec3 specular = cookTorance(N, V, L, H, F, inputValues.roughness);

    return max((kD * diffuse) + (specular * dot(N, L) * 2.0), 0.0);
}

float attenuate(vec3 N)
{
    float distance = length(N);
    return 1.0 / max(((distance) * (distance)), 0.0001);
}

//take inputs and output a vec4 color to be directly drawn on screen before post-processing
vec4 calculatePBR(BRDFInput inputValues, vec3 camPos, vec3 worldPosition, vec3 normal)
{
    vec3 totalLight = lightInfo.ambientLight.xyz * lightInfo.ambientLight.w; //ambient light

    //point lights
    for(uint i = 0; i < lightInfo.pointLightCount; i++)
    {
        const PointLight light = pointLights.lights[i];
        const vec3 N = normalize(normal);
        const vec3 V = normalize(camPos - worldPosition);
        const vec3 L = normalize(light.position.xyz - worldPosition);
        const vec3 H = normalize(V + L);
        
        totalLight += calculateLight(N, V, L, H, inputValues) * light.color * attenuate(L);
    }

    //emission
    totalLight += inputValues.emissive.xyz * inputValues.emissive.w;

    return vec4(totalLight, 1.0);
}

#endif