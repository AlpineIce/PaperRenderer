#include "RayTrace.h"
#include "PaperRenderer.h"
#include "Camera.h"

namespace PaperRenderer
{
    RayTraceRender::RayTraceRender(RenderEngine *renderer, Camera *camera, const RayTraceRenderInfo& renderPassInfo)
        :rendererPtr(renderer)
    {
    }

    RayTraceRender::~RayTraceRender()
    {
    }

    void RayTraceRender::render(const PaperMemory::SynchronizationInfo &syncInfo)
    {
    }
}