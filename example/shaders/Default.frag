#version 460
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) out vec4 color;

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

struct PointLight
{
    vec3 position;
    vec3 color;
};

layout(scalar, set = 0, binding = 1) readonly buffer PointLights
{
    PointLight lights[];
} pointLights;

layout(std430, set = 0, binding = 2) uniform LightInformation
{
    vec4 ambientLight;
    vec4 camPos;
    uint pointLightCount;
} lightInfo;

void main()
{
    //solve the integral, use basic lambertian diffuse (N dot L / pi)
    vec3 totalLight = lightInfo.ambientLight.xyz * lightInfo.ambientLight.w; //ambient light
    const vec3 N = normalize(normal);
    const vec3 V = normalize(lightInfo.camPos.xyz - worldPosition);

    for(uint i = 0; i < lightInfo.pointLightCount; i++)
    {
        const PointLight light = pointLights.lights[i];
        const vec3 L = normalize(light.position.xyz - worldPosition);
        const vec3 H = normalize(V + L);
        
        totalLight += (dot(N, L) / 3.14) * light.color * (1.0 / (length(L) * length(L)));
    }

    //ambient light
    totalLight += lightInfo.ambientLight.xyz * lightInfo.ambientLight.w;

    color = vec4(totalLight, 1.0);
}