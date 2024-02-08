#version 460
#extension GL_GOOGLE_include_directive : require

layout(location = 0) out vec4 color;

layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

#include "Components/PBR.glsl"

void main()
{
    PBRinput pbrParams;
    pbrParams.albedo = vec4(1.0, 1.0, 1.0, 1.0);
    pbrParams.roughness = 0.5;
    pbrParams.metallic = 0.0;
    pbrParams.normal = normal;
    
    color = calculatePBR(pbrParams);
}


