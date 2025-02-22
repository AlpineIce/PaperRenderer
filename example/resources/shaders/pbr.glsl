#ifndef PBR_GLSL
#define PBR_GLSL 1

#extension GL_EXT_scalar_block_layout : require

struct PointLight
{
    vec3 position;
    vec3 color;
    float radius;
    float bounds;
    bool castShadow;
};

layout(std430, set = 1, binding = 0) uniform LightInformation
{
    vec4 ambientLight;
    uint pointLightCount;
} lightInfo;

layout(scalar, set = 1, binding = 1) readonly buffer PointLights
{
    PointLight lights[];
} pointLights;

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
    vec4 ambientLight;
    float metallic;
    float roughness;
};

//----------CALCULATION FUNCTIONS----------//

#define PI 3.14159265359

//lambertian diffuse
vec3 diffuse(vec3 N, vec3 L, vec3 baseColor)
{
    return (max(dot(N, L), 0.0)) * baseColor;
}

//Trowbridge–Reitz(GGX)
float normalDistribution(vec3 N, vec3 H, float roughness)
{
    const float a2 = roughness * roughness;
    const float NdotH = max(dot(N, H), 0.0);

    const float denominator = ((NdotH * NdotH) * (a2 - 1.0)) + 1.0;
    return a2 / (denominator * denominator);
}

//Schlick approximation (strength should be 5 for normal fresnel approximation)
vec3 fresnel(vec3 V, vec3 H, vec3 F0, float strength)
{
    const float cosTheta = dot(V, H);
    //const float exponent = (-5.55473 * cosTheta) - (6.98316 * cosTheta);

    return F0 + ((1.0 - F0) * pow(1.0 - cosTheta, strength));
    //return F0 + ((1.0 - F0) * pow(2.0, exponent));
}

float shlickGGX(vec3 A, vec3 B, float roughness)
{
    const float k = ((roughness + 1.0) * (roughness + 1.0)) / 8.0;
    const float AdotB = max(dot(A, B), 0.0);

    return AdotB / ((AdotB * (1.0 - k)) + k);
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

//light attenuation
float attenuate(vec3 L, float bounds)
{
    float distance = length(L);
    return pow(clamp(1.0 - pow((distance / bounds), 4.0), 0.0, 1.0), 2.0) / max(((distance) * (distance)), 0.0001);
}

//bring it all together
vec3 calculatePointLight(const vec3 N, const vec3 V, const vec3 worldPosition, BRDFInput inputValues, const PointLight light)
{
    const vec3 L = normalize(light.position.xyz - worldPosition);
    const vec3 H = normalize(V + L);

    //only calculate if within its bounds
    if(length(light.position.xyz - worldPosition) < light.bounds)
    {
        //clamp the roughness value to either 0.001 (non-metal) or 0.0 (metal, allows for pure mirror)
        inputValues.roughness = clamp(inputValues.roughness, mix(0.001, 0.0, inputValues.metallic), 1.0);
        const vec3 F0 = mix(vec3(0.04), inputValues.baseColor.xyz, inputValues.metallic);
        const vec3 F = fresnel(V, H, F0, 5.0);

        vec3 kD = vec3(1.0) - F;
        kD *= 1.0 - inputValues.metallic;

        vec3 diffuse = diffuse(N, L, inputValues.baseColor.xyz);
        vec3 specular = cookTorance(N, V, L, H, F, inputValues.roughness);

        return max((kD * diffuse) + (specular * dot(N, L) * 2.0), 0.0) * attenuate(light.position.xyz - worldPosition, light.bounds) * light.color;
    }
    else
    {
        return vec3(0.0);
    }
}

#endif