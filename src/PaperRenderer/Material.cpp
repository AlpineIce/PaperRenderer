#include "Material.h"
#include "Camera.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
    //----------MATERIAL DEFINITIONS----------//

    Material::Material(RenderEngine& renderer, RasterPipelineBuildInfo pipelineInfo, bool assignDefaultDescriptors)
        :assignDefaultDescriptors(assignDefaultDescriptors),
        renderer(renderer)
    {
        //add reserved descriptors if set
        if(assignDefaultDescriptors)
        {
            //camera UBO
            pipelineInfo.descriptorSets[0].push_back({
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                .pImmutableSamplers = NULL
            });

            //model matrices SBO
            pipelineInfo.descriptorSets[pipelineInfo.drawDescriptorIndex].push_back({
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .pImmutableSamplers = NULL
            });
        }
        
        //build pipeline
        rasterPipeline = renderer.getPipelineBuilder().buildRasterPipeline(pipelineInfo);
    }

    Material::~Material()
    {
        //destroy descriptors
        for(const auto& [setIndex, set] : descriptorSets)
        {
            renderer.getDescriptorAllocator().freeDescriptorSet(set);
        }

        //destroy graphics pipeline
        rasterPipeline.reset();
    }

    void Material::bind(VkCommandBuffer cmdBuffer, const Camera& camera, std::unordered_map<uint32_t, DescriptorWrites>& descriptorWrites)
    {
        //bind
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterPipeline->getPipeline());

        //camera UBO if used
        if(assignDefaultDescriptors)
        {
            descriptorWrites[0].bufferWrites.push_back({
                .infos = { {
                    .buffer = camera.getCameraUBO().getBuffer(),
                    .offset = renderer.getBufferIndex() * sizeof(CameraUBOData),
                    .range = sizeof(CameraUBOData)
                } },
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .binding = 0
            });
        }
        
        //write descriptors
        for(const auto& [setIndex, writes] : descriptorWrites)
        {
            //make sure descriptor set exists
            if(!descriptorSets.count(setIndex))
            {
                descriptorSets[setIndex] = renderer.getDescriptorAllocator().getDescriptorSet(rasterPipeline->getDescriptorSetLayouts().at(setIndex));
            }

            //update descriptor set
            renderer.getDescriptorAllocator().updateDescriptorSet(descriptorSets[setIndex], writes);

            const DescriptorBind bindingInfo = {
                .bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .layout = rasterPipeline->getLayout(),
                .descriptorSetIndex = setIndex,
                .set = descriptorSets[setIndex]
            };
            
            renderer.getDescriptorAllocator().bindSet(cmdBuffer, bindingInfo);
        }
    }

    //----------MATERIAL INSTANCE DEFINITIONS----------//

    MaterialInstance::MaterialInstance(RenderEngine& renderer, const Material& baseMaterial)
        :baseMaterial(baseMaterial),
        renderer(renderer)
    {
    }

    MaterialInstance::~MaterialInstance()
    {
        //destroy descriptors
        for(const auto& [setIndex, set] : descriptorSets)
        {
            renderer.getDescriptorAllocator().freeDescriptorSet(set);
        }
    }

    void MaterialInstance::bind(VkCommandBuffer cmdBuffer, std::unordered_map<uint32_t, DescriptorWrites>& descriptorWrites)
    {
        //write descriptors
        for(const auto& [setIndex, writes] : descriptorWrites)
        {
            //make sure descriptor set exists
            if(!descriptorSets.count(setIndex))
            {
                descriptorSets[setIndex] = renderer.getDescriptorAllocator().getDescriptorSet(baseMaterial.getRasterPipeline().getDescriptorSetLayouts().at(setIndex));
            }

            //update descriptor set
            renderer.getDescriptorAllocator().updateDescriptorSet(descriptorSets[setIndex], writes);

            const DescriptorBind bindingInfo = {
                .bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                .layout = baseMaterial.getRasterPipeline().getLayout(),
                .descriptorSetIndex = setIndex,
                .set = descriptorSets[setIndex]
            };
            
            renderer.getDescriptorAllocator().bindSet(cmdBuffer, bindingInfo);
        }
    }

    //----------RT MATERIAL DEFINITIONS----------//

    PaperRenderer::RTMaterial::RTMaterial(RenderEngine& renderer, const ShaderHitGroup& hitGroup)
        :renderer(renderer)
    {
        if(hitGroup.chitShaderData.size())
        {
            shaderHitGroup.emplace(std::make_pair(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, std::make_unique<Shader>(renderer, hitGroup.chitShaderData)));
        }
        if(hitGroup.ahitShaderData.size())
        {
            shaderHitGroup.emplace(std::make_pair(VK_SHADER_STAGE_ANY_HIT_BIT_KHR, std::make_unique<Shader>(renderer, hitGroup.ahitShaderData)));
        }
        if(hitGroup.intShaderData.size())
        {
            shaderHitGroup.emplace(std::make_pair(VK_SHADER_STAGE_INTERSECTION_BIT_KHR, std::make_unique<Shader>(renderer, hitGroup.intShaderData)));
        }
    }

    PaperRenderer::RTMaterial::~RTMaterial()
    {
    }
}