#pragma once
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "Camera.h"

#include <memory>

namespace PaperRenderer
{
    //material base
    class Material
    {
    private:
        PipelineProperties pipelineProperties;
        std::unique_ptr<RasterPipeline> rasterPipeline;
        std::unique_ptr<RTPipeline> rtPipeline;

    protected:
        std::string matName;
        PipelineBuildInfo rasterInfo;
        PipelineBuildInfo rtInfo;
        
        std::vector<ShaderPair> shaderPairs;
        std::vector<ShaderPair> rtShaderPairs;
        DescriptorWrites rasterDescriptorWrites = {};
        std::unordered_map<uint32_t, DescriptorSet> rasterDescriptorSets;
        std::unordered_map<uint32_t, DescriptorSet> rtDescriptorSets;

        void buildPipelines(PipelineBuildInfo const* rasterInfo, PipelineBuildInfo const* rtInfo);

    public:
        Material(std::string materialName, const PipelineProperties& pipelineProperties);
        virtual ~Material();

        virtual void bind(VkCommandBuffer cmdBuffer, uint32_t currentImage); //used per pipeline bind and material instance
        
        std::string getMaterialName() const { return matName; }
        const PipelineProperties& getPipelineProperties() const { return pipelineProperties; }
        RasterPipeline const* getRasterPipeline() const { return rasterPipeline.get(); }
        RTPipeline const* getRTPipeline() const { return rtPipeline.get(); }
    };

    class MaterialInstance
    {
    protected:
        Material const* baseMaterial = NULL;
        DescriptorWrites descriptorWrites = {};

    public:
        MaterialInstance(Material const* baseMaterial);
        virtual ~MaterialInstance();
        
        virtual void bind(VkCommandBuffer cmdBuffer, uint32_t currentImage);

        Material const* getBaseMaterialPtr() const { return baseMaterial; }
    };
}