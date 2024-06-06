#include "RayTrace.h"

namespace PaperRenderer
{
    RTPreprocessPipeline::RTPreprocessPipeline(std::string fileDir)
        :ComputeShader()
    {
    }

    RTPreprocessPipeline::~RTPreprocessPipeline()
    {
    }

    PaperMemory::CommandBuffer RTPreprocessPipeline::submit()
    {
        return { VK_NULL_HANDLE, PaperMemory::QueueType::COMPUTE };
    }
}