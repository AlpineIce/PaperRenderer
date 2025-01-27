#version 460
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : require

#include "pbr.glsl"

layout(location = 0) out vec4 color;

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

layout(std430, set = 0, binding = 0) uniform CameraInputData
{
    mat4 projection;
    mat4 view;
} inputData;

layout(std430, set = 2, binding = 0) uniform MaterialParameters
{
    vec4 baseColor;
    vec4 emission;
    float roughness;
    float metallic;
} materialParameters;

void main()
{
    //get camera position from view matrix
    const vec3 camPos = inverse(inputData.view)[3].xyz;
    
    //BRDF
    BRDFInput brdfInput;
    brdfInput.baseColor = materialParameters.baseColor;
    brdfInput.emissive = materialParameters.emission;
    brdfInput.ambientLight = vec4(0.2f, 0.2f, 0.2f, 1.0f);
    brdfInput.roughness = materialParameters.roughness;
    brdfInput.metallic = materialParameters.metallic;
    
    //calculate light
    vec3 totalLight = vec3(0.0);

    //point lights
    for(uint i = 0; i < lightInfo.pointLightCount; i++)
    {
        const PointLight light = pointLights.lights[i];
        const vec3 N = normalize(normal);
        const vec3 V = normalize(camPos - worldPosition);
        
        totalLight += calculatePointLight(N, V, worldPosition, brdfInput, light);
    }

    //emission
    totalLight += brdfInput.emissive.xyz * brdfInput.emissive.w;

    //ambient
    totalLight += brdfInput.ambientLight.xyz * brdfInput.ambientLight.w * brdfInput.baseColor.xyz;
    
    //output
    color = vec4(totalLight, materialParameters.baseColor.w);
}