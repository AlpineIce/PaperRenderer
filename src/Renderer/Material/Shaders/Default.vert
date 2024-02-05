#version 460

layout(location = 0) in vec3 vertexPosition;
layout(location = 1) in vec3 vertexNormal;
layout(location = 2) in vec2 vertexTexCoord;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec2 texCoord;

struct CameraData
{
    mat4 view;
    mat4 projection;
};
layout(set = 0, binding = 0) uniform GlobalUniform
{
    CameraData camera;
} GU;

struct ObjectData
{
    mat4 model;
};

layout(std140, set = 2, binding = 0) readonly buffer ObjectBuffer
{
    ObjectData data[];
} objBuffer;

void main()
{
    normal = vertexNormal;
    texCoord = vertexTexCoord;
    
    worldPosition = vec3(objBuffer.data[gl_BaseInstance].model * vec4(vertexPosition, 1.0)).xyz;

    gl_Position = GU.camera.projection * GU.camera.view * objBuffer.data[gl_BaseInstance].model *  vec4(vertexPosition, 1.0);
}