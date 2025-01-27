#version 460
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
layout(location = 2) in vec2 vertexTexCoord;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec2 texCoord;

layout(std430, set = 0, binding = 0) uniform CameraInputData
{
    mat4 projection;
    mat4 view;
} inputData;

layout(scalar, set = 3, binding = 0) readonly buffer ObjectBuffer
{
    mat3x4 matrices[];
} objBuffer;

void main()
{
    const mat4x3 modelMatrix = transpose(objBuffer.matrices[gl_InstanceIndex]);

    normal = normalize(transpose(inverse(mat3(modelMatrix))) * vertexNormal);
    texCoord = vertexTexCoord;
    worldPosition = vec3(modelMatrix * vec4(vertexPosition, 1.0));

    //this code feels stupid, but it works with 48 bytes instead of 64
    gl_Position = inputData.projection * inputData.view * vec4(modelMatrix * vec4(vertexPosition, 1.0), 1.0);
}