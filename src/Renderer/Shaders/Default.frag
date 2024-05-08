#version 460

layout(location = 0) out vec4 color;

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

void main()
{
    color = vec4(normal, 1.0);
}


