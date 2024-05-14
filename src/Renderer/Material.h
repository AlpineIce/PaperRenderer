#pragma once
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "Camera.h"

#include <memory>

namespace PaperRenderer
{
    //"helper" struct to decrease parameters needed for construction
    struct MaterialRendererInfo
    {
        Device* devicePtr;
        DescriptorAllocator* descriptorsPtr;
        PipelineBuilder* pipelineBuilderPtr;
    };

    struct GlobalUniforms
    {
        PaperMemory::Buffer* globalUBO;
        PaperMemory::Buffer* lightingInfoBuffer;
        uint32_t maxPointLights;
        glm::vec3 camPos;
    };

    //material base
    class Material
    {
    private:
        std::unique_ptr<RasterPipeline> rasterPipeline;
        std::unique_ptr<RTPipeline> rtPipeline;

        static MaterialRendererInfo rendererInfo;

    protected:
        std::string matName;
        PipelineBuildInfo rasterInfo;
        PipelineBuildInfo rtInfo;
        
        std::vector<ShaderPair> shaderPairs;
        std::vector<ShaderPair> rtShaderPairs;
        DescriptorWrites descriptorWrites = {};
        std::unordered_map<uint32_t, DescriptorSet*> descriptorSets;
        std::unordered_map<uint32_t, DescriptorSet*> rtDescriptorSets;
        DescriptorSet set0Descriptors;
        DescriptorSet set1Descriptors;
        DescriptorSet set2Descriptors;

        void buildPipelines(PipelineBuildInfo const* rasterInfo, PipelineBuildInfo const* rtInfo);

    public:
        Material(std::string materialName);
        virtual ~Material();

        static void initRendererInfo(Device* device, DescriptorAllocator* descriptors, PipelineBuilder* pipelineBuilder);
        static MaterialRendererInfo getRendererInfo() { return rendererInfo; }

        virtual void bind(VkCommandBuffer cmdBuffer, uint32_t currentImage) const; //used per pipeline bind and material instance
        
        std::string getMaterialName() const { return matName; }
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
        
        virtual void bind(VkCommandBuffer cmdBuffer, uint32_t currentImage) const;

        Material const* getBaseMaterialPtr() const { return baseMaterial; }
    };

    //default material
    //it really is this basic, but a lot of power is given for other materials to have their own UBO's with their own descriptor sets, so long as they work with the "drawing process".
    //the last bit is referring to set layout 0 being the base material descriptor layout, and 1 being the material instance layout.
    class DefaultMaterial : public Material
    {
    private:

    public:
        DefaultMaterial(std::string vertexShaderPath, std::string fragmentShaderPath);
        ~DefaultMaterial() override;

        void bind(VkCommandBuffer cmdBuffer, uint32_t currentImage);
    };

    class DefaultMaterialInstance : public MaterialInstance
    {
    private:

    public:
        DefaultMaterialInstance(Material const* baseMaterial);
        ~DefaultMaterialInstance() override;

        void bind(VkCommandBuffer cmdBuffer, uint32_t currentImage);
    };
}