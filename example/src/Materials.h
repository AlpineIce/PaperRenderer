#pragma once
#include "Common.h"

//vertex definition
struct Vertex
{
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

//material parameters for this example (no texturing)
struct MaterialParameters
{
    glm::vec4 baseColor;
    glm::vec4 emission;
    float roughness;
    float metallic;
};

//----------RASTER MATERIALS----------//

//default material class inherits PaperRenderer::Material
class DefaultMaterial : public PaperRenderer::Material
{
private:
    const PaperRenderer::Buffer& lightBuffer;
    const PaperRenderer::Buffer& lightInfoUBO;

public:
    DefaultMaterial(PaperRenderer::RenderEngine& renderer, const PaperRenderer::RasterPipelineBuildInfo& pipelineInfo, const PaperRenderer::Buffer& lightBuffer, const PaperRenderer::Buffer& lightInfoUBO);
    ~DefaultMaterial() override;

    //bind class can override base class
    void bind(VkCommandBuffer cmdBuffer, const PaperRenderer::Camera& camera, std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites>& descriptorWrites) override
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
};

//default material instance class inherits PaperRenderer::MaterialInstance
class DefaultMaterialInstance : public PaperRenderer::MaterialInstance
{
private:
    //Parameters and corresponding UBO to be used for material instances. Setting uniforms in material instances saves on expensive pipeline binding
    const MaterialParameters parameters;
    std::unique_ptr<PaperRenderer::Buffer> parametersUBO;

public:
    DefaultMaterialInstance(PaperRenderer::RenderEngine& renderer, const PaperRenderer::Material& baseMaterial, MaterialParameters parameters)
        :PaperRenderer::MaterialInstance(renderer, baseMaterial),
        parameters(parameters)
    {
        //create UBO
        PaperRenderer::BufferInfo uboInfo = {
            .size = sizeof(MaterialParameters),
            .usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR,
            .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
        };

        parametersUBO = std::make_unique<PaperRenderer::Buffer>(renderer, uboInfo);

        //fill UBO data (this can change every frame too, which should be done in the bind() function, but for this example, that is unnecessary)
        PaperRenderer::BufferWrite uboWrite = {
            .offset = 0,
            .size=  sizeof(MaterialParameters),
            .data = &parameters
        };

        parametersUBO->writeToBuffer({ uboWrite });
    }

    ~DefaultMaterialInstance() override
    {
    }

    void bind(VkCommandBuffer cmdBuffer, std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites>& descriptorWrites) override
    {
        //additional non-default descriptor writes can be inserted into descriptorWrites here

        //set 2, binding 0 (example material parameters)
        descriptorWrites[2].bufferWrites.push_back({
            .infos = {{
                .buffer = parametersUBO->getBuffer(),
                .offset = 0,
                .range = VK_WHOLE_SIZE
            }},
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .binding = 0
        });
        //remember to call the parent class function!
        MaterialInstance::bind(cmdBuffer, descriptorWrites);
    }
};
