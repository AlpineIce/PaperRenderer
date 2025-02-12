#pragma once
#include "Pipeline.h"
#include "Descriptor.h"

namespace PaperRenderer
{
    class ComputeShader
    {
    private:
        const ComputePipeline pipeline;

        class RenderEngine& renderer;

    public:
        ComputeShader(class RenderEngine& renderer, const ComputePipelineInfo& pipelineInfo);
        ~ComputeShader();
        ComputeShader(const ComputeShader&) = delete;

        const ComputePipeline& getPipeline() const { return pipeline; }

        //binds pipeline, writes descriptors, and does vkCmdDispatch on work group size
        void dispatch(const VkCommandBuffer& cmdBuffer,
            const std::vector<SetBinding>& descriptorSetsBindings,
            const glm::uvec3& workGroupSizes
        ) const;
    };
}