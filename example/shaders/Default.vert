#version 460
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
layout(location = 2) in vec2 vertexTexCoord;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec2 texCoord;

layout(std430, set = 0, binding = 0) uniform GlobalInputData
{
    mat4 projection;
    mat4 view;
} inputData;

layout(std430, set = 2, binding = 0) readonly buffer ObjectBuffer
{
    mat4 matrices[];
} objBuffer;

void main()
{
    /*const mat4 projection = {
        { 0.733, 0.0, 0.0, 0.0},
        { 0.0, 1.303, 0.0, 0.0},
        { 0.0, 0.0, -1.0, -1.0},
        { 0.0, 0.0, -0.2, 0.0}
    };
    const mat4 view = {
        { 1.0, 0.0, 0.0, 0.0 },
        { 0.0, 0.0, 1.0, 0.0 },
        { 0.0, -1.0, 0.0, 0.0 },
        { 0.0, 0.0, 0.0, 1.0 }
    };*/

    const mat4 modelMatrix = objBuffer.matrices[gl_InstanceIndex];

    normal = normalize(mat3(transpose(inverse(modelMatrix))) * vertexNormal);
    texCoord = vertexTexCoord;
    worldPosition = vec3(/*modelMatrix * */vec4(vertexPosition, 1.0));

    gl_Position = inputData.projection * inputData.view * modelMatrix * vec4(vertexPosition, 1.0);
}