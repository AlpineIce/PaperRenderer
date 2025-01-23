#pragma once
#include "Pipeline.h"
#include "Descriptor.h"

#include <memory>

namespace PaperRenderer
{
    //----------RASTER MATERIAL ABSTRACTIONS----------//

    class Material
    {
    private:
        const DescriptorGroup descriptorGroup;
        std::unique_ptr<RasterPipeline> rasterPipeline;
    
    protected:
        class RenderEngine& renderer;

    public:
        //materialDescriptorSets refers to the descriptor sets that will be updated/bound in the scope of this material only
        Material(class RenderEngine& renderer, const RasterPipelineBuildInfo& pipelineInfo, const std::unordered_map<uint32_t, VkDescriptorSetLayout>& materialDescriptorSets);
        virtual ~Material();
        Material(const Material&) = delete;

        virtual void updateDescriptors(std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites> descriptorWrites) const;
        void bind(VkCommandBuffer cmdBuffer) const;
        
        const RasterPipeline& getRasterPipeline() const { return *rasterPipeline; }
        const DescriptorGroup& getDescriptorGroup() const { return descriptorGroup; }
    };

    class MaterialInstance
    {
    protected:
        const DescriptorGroup descriptorGroup;

        const Material& baseMaterial;
        class RenderEngine& renderer;

    public:
        //instanceDescriptorSets refers to the descriptor sets that will be updated/bound in the scope of this material instance only
        MaterialInstance(class RenderEngine& renderer, const Material& baseMaterial, const std::unordered_map<uint32_t, VkDescriptorSetLayout>& instanceDescriptorSets);
        virtual ~MaterialInstance();
        MaterialInstance(const MaterialInstance&) = delete;

        virtual void updateDescriptors(std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites> descriptorWrites) const;
        void bind(VkCommandBuffer cmdBuffer) const;
        
        const DescriptorGroup& getDescriptorGroup() const { return descriptorGroup; }
        const Material& getBaseMaterial() const { return baseMaterial; }
    };

    //----------RT MATERIAL ABSTRACTIONS----------//

    struct ShaderHitGroup
    {
        const std::vector<uint32_t>& chitShaderData; //optional if int shader is valid, leave empty and shader will be ignored
        const std::vector<uint32_t>& ahitShaderData; //optional, leave empty and shader will be ignored
        const std::vector<uint32_t>& intShaderData; //optional if chit shader is valid, leave empty and shader will be ignored
    };

    class RTMaterial
    {
    private:
        std::unordered_map<VkShaderStageFlagBits, std::shared_ptr<Shader>> shaderHitGroup;

        class RenderEngine& renderer;

    public:
        //closest hit shader is required, but any hit and intersection shaders are optional
        RTMaterial(class RenderEngine& renderer, const ShaderHitGroup& hitGroup);
        ~RTMaterial();
        RTMaterial(const RTMaterial&) = delete;

        const std::unordered_map<VkShaderStageFlagBits, std::shared_ptr<Shader>>& getShaderHitGroup() const { return shaderHitGroup; }
    };
}