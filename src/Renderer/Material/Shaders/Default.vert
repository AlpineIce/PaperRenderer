#version 460
#extension GL_GOOGLE_include_directive : require
#include "Components/Common.glsl"

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
layout(location = 2) in vec2 vertexTexCoord;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec2 texCoord;

struct ObjectData
{
    mat4 model;
    vec4 position;
    mat4 objectTransform;
    vec4 PADDING3;
    vec4 PADDING4;
    vec4 PADDING5;
};

layout(std430, set = 2, binding = 0) readonly buffer ObjectBuffer
{
    ObjectData data[];
} objBuffer;

void main()
{
    normal = vertexNormal;
    texCoord = vertexTexCoord;
    
    worldPosition = vec3(objBuffer.data[gl_BaseInstance].model * vec4(vertexPosition, 1.0)).xyz;

    gl_Position = objBuffer.data[gl_BaseInstance].objectTransform *  vec4(vertexPosition, 1.0);
}