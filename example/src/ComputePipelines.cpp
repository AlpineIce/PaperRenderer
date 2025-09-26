#include "ComputePipelines.h"
#include "Materials.h"

AnimationPipeline::AnimationPipeline(PaperRenderer::RenderEngine& renderer)
    :pipeline(renderer, {
        .shaderData = readFromFile("resources/shaders/basic_animation.spv"),
        .descriptorSets = {},
        .pcRanges = {
            {
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .offset = 0,
                .size = sizeof(InstanceAnimationInfo)
            }
        }
    }),
    renderer(renderer)
{
}

AnimationPipeline::~AnimationPipeline()
{
}

PaperRenderer::Queue& AnimationPipeline::animateInstances(const std::vector<PaperRenderer::ModelInstance *>& instances, const PaperRenderer::SynchronizationInfo& syncInfo)
{
    PaperRenderer::CommandBuffer cmdBuffer(renderer.getDevice().getCommands(), PaperRenderer::QueueType::COMPUTE);

    const VkCommandBufferBeginInfo cmdBufferBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = NULL,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = NULL
    };
    vkBeginCommandBuffer(cmdBuffer, &cmdBufferBeginInfo);
    
    // Invoke pipeline for each instance
    for(PaperRenderer::ModelInstance* instance : instances)
    {
        const InstanceAnimationInfo instanceAnimationInfo = {
            .inVboAddress = instance->getGeometryData().getParentModel().getGeometryData().getVBO().getBufferDeviceAddress(),
            .outVboAddress = instance->getGeometryData().getVBO().getBufferDeviceAddress(),
            .instancePosition = instance->getTransformation().position,
            .vertexCount = (uint32_t)(instance->getGeometryData().getVBO().getSize() / sizeof(Vertex)), // Sloppy but works
            .seed = (uint32_t)(glfwGetTime() * 10000.0)
        };

        const VkPushConstantsInfo pcInfo = {
            .sType = VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO,
            .pNext = NULL,
            .layout = pipeline.getPipeline().getLayout(),
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = sizeof(InstanceAnimationInfo),
            .pValues =  &instanceAnimationInfo
        };
        vkCmdPushConstants2(cmdBuffer, &pcInfo);
        pipeline.dispatch(cmdBuffer, {}, glm::uvec3((instanceAnimationInfo.vertexCount / 256) + 1, 1, 1));
    }

    vkEndCommandBuffer(cmdBuffer);

    return renderer.getDevice().getCommands().submitToQueue(PaperRenderer::QueueType::COMPUTE, syncInfo, { cmdBuffer });
}
