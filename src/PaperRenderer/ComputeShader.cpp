#include "ComputeShader.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
    ComputeShader::ComputeShader(RenderEngine& renderer, const ComputePipelineBuildInfo& pipelineInfo)
        :pipeline(renderer.getPipelineBuilder().buildComputePipeline(pipelineInfo)),
        renderer(renderer)
    {
    }

    ComputeShader::~ComputeShader()
    {
        pipeline.reset();
    }

    void ComputeShader::dispatch(const VkCommandBuffer& cmdBuffer,
        const std::vector<DescriptorBind>& descriptorBindings,
        const glm::uvec3& workGroupSizes
    )
    {
        //bind pipeline
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline->getPipeline());

        //bind descriptors
        for(const DescriptorBind& binding : descriptorBindings)
        {
            renderer.getDescriptorAllocator().bindDescriptorSet(cmdBuffer, binding);
        }

        //dispatch
        vkCmdDispatch(cmdBuffer, workGroupSizes.x, workGroupSizes.y, workGroupSizes.z);
    }
}
