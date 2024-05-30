#pragma once
#include "RHI/Device.h"
#include "RHI/Pipeline.h"
#include "Camera.h"

#include <memory>

namespace PaperRenderer
{
    //material base
    enum MaterialFlagBits
    {
        doubleSided = 0x00000001, //TODO
        invertedFaces = 0x00000002, //TODO
        wireFrame = 0x00000004, //TODO
        alphaBlend = 0x00000008, //TODO
    };
    typedef uint32_t MaterialFlags;

    struct MaterialProperties
    {
        std::vector<VkVertexInputAttributeDescription> vertexAttributes; //a good start is vec3 position, vec3 normal, vec2 UVs. Attributes are assumed to be in order
        MaterialFlags materialFlags = 0; //values are false by default
    };

    class Material
    {
    private:
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
        Material(std::string materialName);
        virtual ~Material();

        virtual void bind(VkCommandBuffer cmdBuffer, uint32_t currentImage); //used per pipeline bind and material instance
        
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
        
        virtual void bind(VkCommandBuffer cmdBuffer, uint32_t currentImage);

        Material const* getBaseMaterialPtr() const { return baseMaterial; }
    };

    //default material
    //it really is this basic, but a lot of power is given for other materials to have their own UBO's with their own descriptor sets, so long as they work with the "drawing process".
    //the last bit is referring to set layout 0 being the base material descriptor layout, and 1 being the material instance layout.
    class DefaultMaterial : public Material
    {
    private:
        std::string vertexFileName = "Default_vert.spv";
        std::string fragFileName = "Default_frag.spv";
    public:
        DefaultMaterial(std::string fileDir);
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