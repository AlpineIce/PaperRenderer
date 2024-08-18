#version 460
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
layout(location = 2) in vec2 vertexTexCoord;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec2 texCoord;

layout(std430, push_constant) uniform GlobalInputData
{
    mat4 projection;
    mat4 view;
} inputData;

struct ObjectData
{
    mat4 model;
};

layout(std430, set = 2, binding = 0) readonly buffer ObjectBuffer
{
    ObjectData data[];
} objBuffer;

void main()
{
    normal = normalize(mat3(transpose(inverse(objBuffer.data[gl_InstanceIndex].model))) * vertexNormal);
    texCoord = vertexTexCoord;
    worldPosition = vec3(objBuffer.data[gl_InstanceIndex].model * vec4(vertexPosition, 1.0)).xyz;

    gl_Position = inputData.projection * inputData.view * objBuffer.data[gl_InstanceIndex].model *  vec4(vertexPosition, 1.0);
}