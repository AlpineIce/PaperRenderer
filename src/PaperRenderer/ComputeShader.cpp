#include "ComputeShader.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
    ComputeShader::ComputeShader(RenderEngine& renderer, const ComputePipelineInfo& pipelineInfo)
        :pipeline(renderer, pipelineInfo),
        renderer(renderer)
    {
    }

    ComputeShader::~ComputeShader()
    {
    }

    void ComputeShader::dispatch(const VkCommandBuffer& cmdBuffer,
        const std::vector<SetBinding>& descriptorSetsBindings,
        const glm::uvec3& workGroupSizes
    ) const
    {
        //bind pipeline
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.getPipeline());

        //bind descriptors
        for(const SetBinding& setBinding : descriptorSetsBindings)
        {
            setBinding.set.bindDescriptorSet(cmdBuffer, setBinding.binding);
        }

        //dispatch
        vkCmdDispatch(cmdBuffer, workGroupSizes.x, workGroupSizes.y, workGroupSizes.z);
    }
}
