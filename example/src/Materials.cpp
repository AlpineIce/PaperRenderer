#include "Materials.h"

//----------RASTER MATERIALS----------//

DefaultMaterial::DefaultMaterial(PaperRenderer::RenderEngine &renderer, const PaperRenderer::RasterPipelineBuildInfo &pipelineInfo, const PaperRenderer::Buffer &lightBuffer, const PaperRenderer::Buffer &lightInfoUBO)
    :PaperRenderer::Material(renderer, pipelineInfo),
    lightBuffer(lightBuffer),
    lightInfoUBO(lightInfoUBO)
{
}

DefaultMaterial::~DefaultMaterial()
{
}

void DefaultMaterial::bind(VkCommandBuffer cmdBuffer, const PaperRenderer::Camera &camera, std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites> &descriptorWrites)
{
    //additional non-default descriptor writes can be inserted into descriptorWrites here
    descriptorWrites[0].bufferWrites.push_back({
        .infos = {{
            .buffer = lightBuffer.getBuffer(),
            .offset = 0,
            .range = VK_WHOLE_SIZE
        }},
        .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .binding = 1
    });
    descriptorWrites[0].bufferWrites.push_back({
        .infos = {{
            .buffer = lightInfoUBO.getBuffer(),
            .offset = 0,
            .range = VK_WHOLE_SIZE
        }},
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .binding = 2
    });

    Material::bind(cmdBuffer, camera, descriptorWrites); //parent class function must be called
}

DefaultMaterialInstance::DefaultMaterialInstance(PaperRenderer::RenderEngine &renderer, const PaperRenderer::Material &baseMaterial, MaterialParameters parameters)
    :PaperRenderer::MaterialInstance(renderer, baseMaterial),
    parameters(parameters),
    parametersUBO(renderer, {
        .size = sizeof(MaterialParameters),
        .usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR,
        .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
    })
{
}

DefaultMaterialInstance::~DefaultMaterialInstance()
{
}

void DefaultMaterialInstance::bind(VkCommandBuffer cmdBuffer, std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites> &descriptorWrites)
{
    //additional non-default descriptor writes can be inserted into descriptorWrites here

    //fill UBO data
    PaperRenderer::BufferWrite uboWrite = {
        .offset = 0,
        .size=  sizeof(MaterialParameters),
        .readData = &parameters
    };

    parametersUBO.writeToBuffer({ uboWrite });

    //set 2, binding 0 (example material parameters)
    descriptorWrites[2].bufferWrites.push_back({
        .infos = {{
            .buffer = parametersUBO.getBuffer(),
            .offset = 0,
            .range = VK_WHOLE_SIZE
        }},
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .binding = 0
    });
    
    //remember to call the parent class function!
    MaterialInstance::bind(cmdBuffer, descriptorWrites);
}

LeafMaterial::LeafMaterial(PaperRenderer::RenderEngine &renderer, const PaperRenderer::RasterPipelineBuildInfo &pipelineInfo, const PaperRenderer::Buffer &lightBuffer, const PaperRenderer::Buffer &lightInfoUBO)
    :DefaultMaterial(renderer, pipelineInfo, lightBuffer, lightInfoUBO)
{
}

LeafMaterial::~LeafMaterial()
{
}

void LeafMaterial::bind(VkCommandBuffer cmdBuffer, const PaperRenderer::Camera &camera, std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites> &descriptorWrites)
{
    DefaultMaterial::bind(cmdBuffer, camera, descriptorWrites);
}

LeafMaterialInstance::LeafMaterialInstance(PaperRenderer::RenderEngine &renderer, const LeafMaterial &baseMaterial, MaterialParameters parameters)
    :DefaultMaterialInstance(renderer, baseMaterial, parameters)
{
}

LeafMaterialInstance::~LeafMaterialInstance()
{
}

void LeafMaterialInstance::bind(VkCommandBuffer cmdBuffer, std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites> &descriptorWrites)
{
    DefaultMaterialInstance::bind(cmdBuffer, descriptorWrites);
}
