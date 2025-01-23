#pragma once
#include "Pipeline.h"
#include "Descriptor.h"

namespace PaperRenderer
{
    class ComputeShader
    {
    private:
        std::unique_ptr<ComputePipeline> pipeline;

        class RenderEngine& renderer;

    public:
        ComputeShader(class RenderEngine& renderer, const ComputePipelineBuildInfo& pipelineInfo);
        ~ComputeShader();
        ComputeShader(const ComputeShader&) = delete;

        //binds pipeline, writes descriptors, and does vkCmdDispatch on work group size
        void dispatch(const VkCommandBuffer& cmdBuffer,
            const std::vector<DescriptorBind>& descriptorBindings,
            const glm::uvec3& workGroupSizes
        );
    };
}