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
        const RasterPipelineProperties rasterPipelineProperties = {};
        std::unique_ptr<RasterPipeline> rasterPipeline;
    
    protected:
        class RenderEngine& renderer;

    public:
        Material(class RenderEngine& renderer, const RasterPipelineBuildInfo& pipelineInfo);
        virtual ~Material();
        Material(const Material&) = delete;

        //additional descriptor writes can be added with parameter. SET 0, BINDING 0 IS RESERVED FOR CAMERA MATRICES
        virtual void bind(VkCommandBuffer cmdBuffer, const class Camera& camera, std::unordered_map<uint32_t, DescriptorWrites>& descriptorWrites); //used per pipeline bind and material instance; camera is optional
        
        const RasterPipelineProperties& getRasterPipelineProperties() const { return rasterPipelineProperties; }
        const RasterPipeline& getRasterPipeline() const { return *rasterPipeline; }
    };

    class MaterialInstance
    {
    protected:
        const Material& baseMaterial;
        class RenderEngine& renderer;

    public:
        MaterialInstance(class RenderEngine& renderer, const Material& baseMaterial);
        virtual ~MaterialInstance();
        MaterialInstance(const MaterialInstance&) = delete;
        
        //additional descriptor writes can be added with parameter
        virtual void bind(VkCommandBuffer cmdBuffer, std::unordered_map<uint32_t, DescriptorWrites>& descriptorWrites);

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