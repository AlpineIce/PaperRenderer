#pragma once
#include "glm/glm.hpp"

#include <list>

namespace Renderer
{
    struct AmbientLight
    {
        glm::vec4 color = glm::vec4(1.0f);
    };

    struct DirectLight
    {
        glm::vec4 direction = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
        glm::vec4 color = glm::vec4(1.0f);
        //float softness = 0.0f;
    };

    struct PointLight
    {
        glm::vec4 position = glm::vec4(0.0f);
        glm::vec4 color = glm::vec4(1.0f);
        //float radius = 0.0f;
    };

    struct PointLightObject
    {
        PointLight light;
        std::list<PointLight const*>::iterator lightReference;
    };

    //data for shader uniform buffer
    struct ShaderLightingInformation
    {
        AmbientLight ambientLight;
        DirectLight directLight;
        glm::vec3 camPos;
        uint32_t pointLightCount;
    };
}
