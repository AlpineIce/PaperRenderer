#pragma once
#include "RHI/AccelerationStructure.h"

namespace PaperRenderer
{
    struct RayTraceRenderInfo
    {

    };

    class RayTraceRender
    {
    private:
        class RenderEngine* rendererPtr;
        class Camera* cameraPtr;

    public:
        RayTraceRender(RenderEngine* renderer, Camera* camera, const RayTraceRenderInfo& renderPassInfo);
        ~RayTraceRender();

        void render(const PaperMemory::SynchronizationInfo& syncInfo);
    };
}