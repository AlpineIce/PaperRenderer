#include "Materials.h"

//----------RASTER MATERIALS----------//

DefaultMaterial::DefaultMaterial(PaperRenderer::RenderEngine &renderer, const PaperRenderer::RasterPipelineBuildInfo &pipelineInfo, const LightingData& lightingData)
    :lightingData(lightingData),
    material(renderer, pipelineInfo, [&](VkCommandBuffer cmdBuffer, const PaperRenderer::Camera& camera) { bind(cmdBuffer, camera); }),
    renderer(renderer)
{
}

DefaultMaterial::~DefaultMaterial()
{
}

void DefaultMaterial::bind(VkCommandBuffer cmdBuffer, const PaperRenderer::Camera& camera) const 
{
    //camera matrices binding (set 0)
    const PaperRenderer::DescriptorBinding cameraBinding = {
        .bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .pipelineLayout = material.getRasterPipeline().getLayout(),
        .descriptorSetIndex = 0, //set 0
        .dynamicOffsets = { camera.getUBODynamicOffset() }
    };
    camera.getUBODescriptor().bindDescriptorSet(cmdBuffer, cameraBinding);

    //lighting binding (set 1)
    const PaperRenderer::DescriptorBinding lightingBinding = {
        .bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .pipelineLayout = material.getRasterPipeline().getLayout(),
        .descriptorSetIndex = 1, //set 1
        .dynamicOffsets = {}
    };
    lightingData.lightingDescriptor->bindDescriptorSet(cmdBuffer, lightingBinding);
}

DefaultMaterialInstance::DefaultMaterialInstance(PaperRenderer::RenderEngine& renderer, DefaultMaterial& baseMaterial, MaterialParameters parameters, VkDescriptorSetLayout uboDescriptorLayout)
    :parameters(parameters),
    parametersUBO(renderer, {
        .size = sizeof(MaterialParameters) * 2,
        .usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR,
        .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
    }),
    uboDescriptor(renderer, uboDescriptorLayout),
    materialInstance(renderer, baseMaterial.getMaterial(), [&](VkCommandBuffer cmdBuffer) { bind(cmdBuffer); }),
    renderer(renderer)
{
    //update descriptor
    uboDescriptor.updateDescriptorSet({
        .bufferWrites = { {
            .infos = { {
                    .buffer = parametersUBO.getBuffer(),
                    .offset = 0,
                    .range = sizeof(MaterialParameters)
            } },
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .binding = 0
        } }
    });

    //update UBO
    updateUBO();
}

DefaultMaterialInstance::~DefaultMaterialInstance()
{
}

void DefaultMaterialInstance::bind(VkCommandBuffer cmdBuffer) const
{
    const PaperRenderer::DescriptorBinding binding = {
        .bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .pipelineLayout = materialInstance.getBaseMaterial().getRasterPipeline().getLayout(),
        .descriptorSetIndex = 2, //set 3
        .dynamicOffsets = { (uint32_t)sizeof(MaterialParameters) * renderer.getBufferIndex() }
    };
    uboDescriptor.bindDescriptorSet(cmdBuffer, binding);
}

void DefaultMaterialInstance::updateUBO() const
{
    //fill UBO data
    PaperRenderer::BufferWrite uboWrite = {
        .offset = sizeof(MaterialParameters) * renderer.getBufferIndex(),
        .size = sizeof(MaterialParameters),
        .readData = &parameters
    };
    parametersUBO.writeToBuffer({ uboWrite });
}