#version 460
#extension GL_EXT_scalar_block_layout : require
#extension GL_GOOGLE_include_directive : require

#include "pbr.glsl"
#include "leaf.glsl"

layout(location = 0) out vec4 color;

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

layout(std430, set = 0, binding = 0) uniform GlobalInputData
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
    //discard if alpha is bad
    if(getAlpha(texCoord) < 0.5)
    {
        discard;
    }

    //get camera position from view matrix
    const vec3 camPos = inverse(inputData.view)[3].xyz;
    
    //BRDF
    BRDFInput brdfInput;
    brdfInput.baseColor = materialParameters.baseColor * getOcclusion(texCoord);
    brdfInput.emissive = materialParameters.emission;
    brdfInput.roughness = materialParameters.roughness;
    brdfInput.metallic = materialParameters.metallic;

    //output
    color = calculatePBR(brdfInput, camPos, worldPosition, normal);
}