#include "Material.h"
#include "Camera.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
    //----------MATERIAL DEFINITIONS----------//

    Material::Material(class RenderEngine& renderer, std::string materialName)
        :renderer(renderer),
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

        rasterDescriptorSets[DescriptorScopes::RASTER_MATERIAL].descriptorBindings[0] = {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .pImmutableSamplers = nullptr
        };
    }

    Material::~Material()
    {
        rasterPipeline.reset();
    }

    void Material::buildRasterPipeline(RasterPipelineBuildInfo const* rasterInfo, const RasterPipelineProperties& rasterProperties)
    {
        if(rasterInfo) this->rasterPipeline = renderer.getPipelineBuilder().buildRasterPipeline(*rasterInfo, rasterProperties);
    }

    void Material::bind(VkCommandBuffer cmdBuffer, Camera* camera)
    {
        vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rasterPipeline->getPipeline());

        if(camera)
        {
            //uniform buffer
            VkDescriptorBufferInfo uniformInfo = {};
            uniformInfo.buffer = camera->getCameraUBO().getBuffer();
            uniformInfo.offset = 0;
            uniformInfo.range = VK_WHOLE_SIZE;

            PaperRenderer::BuffersDescriptorWrites uniformWrite;
            uniformWrite.binding = 0;
            uniformWrite.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uniformWrite.infos = { uniformInfo };

            rasterDescriptorWrites.bufferWrites.push_back(uniformWrite);
        }
        
        if(rasterDescriptorWrites.bufferViewWrites.size() || rasterDescriptorWrites.bufferWrites.size() || rasterDescriptorWrites.imageWrites.size())
        {
            VkDescriptorSet materialDescriptorSet = renderer.getDescriptorAllocator().allocateDescriptorSet(rasterPipeline->getDescriptorSetLayouts().at(RASTER_MATERIAL));
            DescriptorAllocator::writeUniforms(renderer, materialDescriptorSet, rasterDescriptorWrites);

            DescriptorBind bindingInfo = {};
            bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            bindingInfo.set = materialDescriptorSet;
            bindingInfo.descriptorScope = RASTER_MATERIAL;
            bindingInfo.layout = rasterPipeline->getLayout();
            
            DescriptorAllocator::bindSet(cmdBuffer, bindingInfo);
        }
    }

    //----------MATERIAL INSTANCE DEFINITIONS----------//

    MaterialInstance::MaterialInstance(class RenderEngine& renderer, Material const *baseMaterial)
        :renderer(renderer),
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
            VkDescriptorSet instDescriptorSet = renderer.getDescriptorAllocator().allocateDescriptorSet(baseMaterial->getRasterPipeline()->getDescriptorSetLayouts().at(RASTER_MATERIAL_INSTANCE));
            DescriptorAllocator::writeUniforms(renderer, instDescriptorSet, descriptorWrites);

            DescriptorBind bindingInfo = {};
            bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            bindingInfo.set = instDescriptorSet;
            bindingInfo.descriptorScope = RASTER_MATERIAL_INSTANCE;
            bindingInfo.layout = baseMaterial->getRasterPipeline()->getLayout();
            
            DescriptorAllocator::bindSet(cmdBuffer, bindingInfo);
        }
    }

    //----------RT MATERIAL DEFINITIONS----------//

    PaperRenderer::RTMaterial::RTMaterial(RenderEngine& renderer, const ShaderHitGroup& hitGroup)
        :renderer(renderer)
    {
        //chit is guaranteed to be valid
        shaderHitGroup.emplace(std::make_pair(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, std::make_unique<Shader>(renderer.getDevice(), hitGroup.chitShaderDir)));

        //ahit and int are optional
        if(hitGroup.ahitShaderDir.size())
        {
            shaderHitGroup.emplace(std::make_pair(VK_SHADER_STAGE_ANY_HIT_BIT_KHR, std::make_unique<Shader>(renderer.getDevice(), hitGroup.ahitShaderDir)));
        }
        if(hitGroup.intShaderDir.size())
        {
            shaderHitGroup.emplace(std::make_pair(VK_SHADER_STAGE_INTERSECTION_BIT_KHR, std::make_unique<Shader>(renderer.getDevice(), hitGroup.intShaderDir)));
        }
    }

    PaperRenderer::RTMaterial::~RTMaterial()
    {
    }
}