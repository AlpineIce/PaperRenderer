#pragma once
#include "Pipeline.h"
#include "Descriptor.h"

#include <memory>

namespace PaperRenderer
{
    //material base
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
}