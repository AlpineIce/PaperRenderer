#pragma once
#include "RHI/Pipeline.h"
#include "RHI/Descriptor.h"

#include <memory>

namespace PaperRenderer
{
    //material base
    class Material
    {
    private:
        std::unique_ptr<RasterPipeline> rasterPipeline;

    protected:
        std::string matName;
        RasterPipelineBuildInfo rasterInfo;
        
        std::vector<ShaderPair> shaderPairs;
        DescriptorWrites rasterDescriptorWrites = {};
        std::unordered_map<uint32_t, DescriptorSet> rasterDescriptorSets;
        RasterPipelineProperties rasterPipelineProperties = {};

        void buildRasterPipeline(RasterPipelineBuildInfo const* rasterInfo, const RasterPipelineProperties& rasterProperties);

        class RenderEngine* rendererPtr;

    public:
        Material(class RenderEngine* renderer, std::string materialName);
        virtual ~Material();

        virtual void bind(VkCommandBuffer cmdBuffer, uint32_t currentImage); //used per pipeline bind and material instance
        
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
        
        virtual void bind(VkCommandBuffer cmdBuffer, uint32_t currentImage);

        Material const* getBaseMaterialPtr() const { return baseMaterial; }
    };
}