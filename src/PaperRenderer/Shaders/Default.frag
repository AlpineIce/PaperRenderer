#version 460

layout(location = 0) out vec4 color;

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;

void main()
{
    vec3 lightDirection = normalize(vec3(1.0, 2.0, 1.0));
    float lightLevel = mix(max(dot(lightDirection, normal).x, 0.0), 0.1, 0.5);

    color = vec4(vec3(lightLevel, lightLevel, lightLevel), 1.0);
}


