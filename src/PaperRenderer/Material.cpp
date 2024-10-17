#include "Material.h"
#include "Camera.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
    //----------MATERIAL DEFINITIONS----------//

    Material::Material(class RenderEngine* renderer, std::string materialName)
        :rendererPtr(renderer),
        matName(materialName),
        rasterInfo({
            .shaderInfo = shaderPairs,
            .descriptors = rasterDescriptorSets,
            .pcRanges = pcRanges
        })
    {
        rasterDescriptorSets[DescriptorScopes::RASTER_MATERIAL];
        rasterDescriptorSets[DescriptorScopes::RASTER_MATERIAL_INSTANCE];
        rasterDescriptorSets[DescriptorScopes::RASTER_OBJECT];

        pcRanges.push_back({
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(GlobalInputData)
        });
    }

    Material::~Material()
    {
        rasterPipeline.reset();
    }

    void Material::buildRasterPipeline(RasterPipelineBuildInfo const* rasterInfo, const RasterPipelineProperties& rasterProperties)
    {
        if(rasterInfo) this->rasterPipeline = rendererPtr->getPipelineBuilder()->buildRasterPipeline(*rasterInfo, rasterProperties);
    }

    void Material::bind(VkCommandBuffer cmdBuffer, Camera* camera)
    {
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterPipeline->getPipeline());

        if(camera)
        {
            GlobalInputData inputData = {};
            inputData.projection = camera->getProjection();
            inputData.view = camera->getViewMatrix();

            vkCmdPushConstants(cmdBuffer, rasterPipeline->getLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GlobalInputData), &inputData);
        }
        
        if(rasterDescriptorWrites.bufferViewWrites.size() || rasterDescriptorWrites.bufferWrites.size() || rasterDescriptorWrites.imageWrites.size())
        {
            VkDescriptorSet materialDescriptorSet = rendererPtr->getDescriptorAllocator()->allocateDescriptorSet(rasterPipeline->getDescriptorSetLayouts().at(RASTER_MATERIAL));
            DescriptorAllocator::writeUniforms(rendererPtr, materialDescriptorSet, rasterDescriptorWrites);

            DescriptorBind bindingInfo = {};
            bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            bindingInfo.set = materialDescriptorSet;
            bindingInfo.descriptorScope = RASTER_MATERIAL;
            bindingInfo.layout = rasterPipeline->getLayout();
            
            DescriptorAllocator::bindSet(cmdBuffer, bindingInfo);
        }
    }

    //----------MATERIAL INSTANCE DEFINITIONS----------//

    MaterialInstance::MaterialInstance(class RenderEngine* renderer, Material const *baseMaterial)
        :rendererPtr(renderer),
        baseMaterial(baseMaterial)
    {
    }

    MaterialInstance::~MaterialInstance()
    {
    }

    void MaterialInstance::bind(VkCommandBuffer cmdBuffer)
    {
        if(descriptorWrites.bufferViewWrites.size() || descriptorWrites.bufferWrites.size() || descriptorWrites.imageWrites.size())
        {
            VkDescriptorSet instDescriptorSet = rendererPtr->getDescriptorAllocator()->allocateDescriptorSet(baseMaterial->getRasterPipeline()->getDescriptorSetLayouts().at(RASTER_MATERIAL_INSTANCE));
            DescriptorAllocator::writeUniforms(rendererPtr, instDescriptorSet, descriptorWrites);

            DescriptorBind bindingInfo = {};
            bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            bindingInfo.set = instDescriptorSet;
            bindingInfo.descriptorScope = RASTER_MATERIAL_INSTANCE;
            bindingInfo.layout = baseMaterial->getRasterPipeline()->getLayout();
            
            DescriptorAllocator::bindSet(cmdBuffer, bindingInfo);
        }
    }

    //----------RT MATERIAL DEFINITIONS----------//

    PaperRenderer::RTMaterial::RTMaterial(RenderEngine* renderer, const ShaderHitGroup& hitGroup)
        :rendererPtr(renderer)
    {
        //chit is guaranteed to be valid
        shaderHitGroup.emplace(std::make_pair(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, std::make_unique<Shader>(rendererPtr->getDevice(), hitGroup.chitShaderDir)));

        //ahit and int are optional
        if(hitGroup.ahitShaderDir.size())
        {
            shaderHitGroup.emplace(std::make_pair(VK_SHADER_STAGE_ANY_HIT_BIT_KHR, std::make_unique<Shader>(rendererPtr->getDevice(), hitGroup.ahitShaderDir)));
        }
        if(hitGroup.intShaderDir.size())
        {
            shaderHitGroup.emplace(std::make_pair(VK_SHADER_STAGE_INTERSECTION_BIT_KHR, std::make_unique<Shader>(rendererPtr->getDevice(), hitGroup.intShaderDir)));
        }
    }

    PaperRenderer::RTMaterial::~RTMaterial()
    {
    }
}