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
        std::unique_ptr<RasterPipeline> rasterPipeline;

        struct GlobalInputData
        {
            glm::mat4 projection;
            glm::mat4 view;
        };

    protected:
        std::string matName;
        RasterPipelineBuildInfo rasterInfo;
        
        std::vector<ShaderPair> shaderPairs;
        DescriptorWrites rasterDescriptorWrites = {};
        std::unordered_map<uint32_t, DescriptorSet> rasterDescriptorSets;
        std::vector<VkPushConstantRange> pcRanges;
        RasterPipelineProperties rasterPipelineProperties = {};

        void buildRasterPipeline(RasterPipelineBuildInfo const* rasterInfo, const RasterPipelineProperties& rasterProperties);

        class RenderEngine* rendererPtr;

        //TODO IMPLEMENT A CHIT SHADER AND HAVE RT PIPELINE RETURN THE SBT OFFSET WHEN ADDED TO IT.

    public:
        Material(class RenderEngine* renderer, std::string materialName);
        virtual ~Material();
        Material(const Material&) = delete;

        virtual void bind(VkCommandBuffer cmdBuffer, class Camera* camera); //used per pipeline bind and material instance; camera is optional
        
        std::string getMaterialName() const { return matName; }
        const RasterPipelineProperties& getRasterPipelineProperties() const { return rasterPipelineProperties; }
        RasterPipeline const* getRasterPipeline() const { return rasterPipeline.get(); }
    };

    class MaterialInstance
    {
    protected:
        Material const* baseMaterial = NULL;
        DescriptorWrites descriptorWrites = {};

        class RenderEngine* rendererPtr;

    public:
        MaterialInstance(class RenderEngine* renderer, Material const* baseMaterial);
        virtual ~MaterialInstance();
        MaterialInstance(const MaterialInstance&) = delete;
        
        virtual void bind(VkCommandBuffer cmdBuffer);

        Material const* getBaseMaterialPtr() const { return baseMaterial; }
    };

    //----------RT MATERIAL ABSTRACTIONS----------//

    struct ShaderHitGroup
    {
        std::string chitShaderDir; //optional if int shader is valid, leave empty and shader will be ignored
        std::string ahitShaderDir; //optional, leave empty and shader will be ignored
        std::string intShaderDir; //optional if chit shader is valid, leave empty and shader will be ignored
    };

    class RTMaterial
    {
    private:
        std::unordered_map<VkShaderStageFlagBits, std::shared_ptr<Shader>> shaderHitGroup;
        std::unordered_map<RTPipeline const*, uint32_t> sbtOffsets;

        class RenderEngine* rendererPtr;

    public:
        //closest hit shader is required, but any hit and intersection shaders are optional
        RTMaterial(class RenderEngine* renderer, const ShaderHitGroup& hitGroup);
        ~RTMaterial();

        const std::unordered_map<VkShaderStageFlagBits, std::shared_ptr<Shader>>& getShaderHitGroup() const { return shaderHitGroup; }
    };
}