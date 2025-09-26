#pragma once
#include "Common.h"

class AnimationPipeline
{
private:
    PaperRenderer::ComputeShader pipeline;

    struct InstanceAnimationInfo
    {
        uint64_t inVboAddress = 0;
        uint64_t outVboAddress = 0;
        glm::vec3 instancePosition = glm::vec3(0.0);
        uint32_t vertexCount = 0;
        uint32_t seed = 0;
    };

    PaperRenderer::RenderEngine& renderer;

public:
    AnimationPipeline(PaperRenderer::RenderEngine& renderer);
    ~AnimationPipeline();
    AnimationPipeline(const AnimationPipeline&) = delete;

    PaperRenderer::Queue& animateInstances(const std::vector<PaperRenderer::ModelInstance*>& instances, const PaperRenderer::SynchronizationInfo& syncInfo);
};