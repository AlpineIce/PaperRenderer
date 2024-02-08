#pragma once
#include "Renderer/RHI/Device.h"
#include "Renderer/RHI/Pipeline.h"
#include "Renderer/Light.h"
#include "Renderer/Camera.h"

#include <memory>

namespace Renderer
{
    //"helper" struct to decrease parameters needed for construction
    struct MaterialRendererInfo
    {
        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;
        DescriptorAllocator* descriptorsPtr;
        PipelineBuilder* pipelineBuilderPtr;
    };

    struct GlobalUniforms
    {
        UniformBuffer* globalUBO;
        StorageBuffer* pointLightsBuffer;
        UniformBuffer* lightingInfoBuffer;
        uint32_t maxPointLights;
        glm::vec3 camPos;
    };

    //material base
    class Material
    {
    protected:
        std::shared_ptr<RasterPipeline> rasterPipeline;
        std::shared_ptr<RTPipeline> rtPipeline;
        std::vector<std::shared_ptr<UniformBuffer>> materialUBOs;
        std::string matName;

        static MaterialRendererInfo rendererInfo;

        void buildPipelines(const PipelineBuildInfo& rasterInfo, const PipelineBuildInfo& rtInfo);

    public:
        Material(std::string materialName, uint32_t matUBOsize);
        virtual ~Material();

        struct BaseUniforms //uniforms that all materials within renderer will use (inherited)
        {
            float gamma;
        };

        void bindPipeline(const VkCommandBuffer& cmdBuffer, GlobalUniforms& uniforms, uint32_t currentImage) const;
        virtual void updateUniforms(void const* uniforms, VkDeviceSize uniformsSize, const std::vector<Renderer::Texture const*>& textures, const VkCommandBuffer& cmdBuffer, uint32_t currentImage) const;
        static void initRendererInfo(Device* device, CmdBufferAllocator* commands, DescriptorAllocator* descriptors, PipelineBuilder* pipelineBuilder);

        std::string getMaterialName() const { return matName; }
        RasterPipeline const* getRasterPipeline() const { return rasterPipeline.get(); }
        RTPipeline const* getRTPipeline() const { return rtPipeline.get(); }
    };

    struct MaterialInstance
    {
        Material const* parentMaterial = NULL;
        void const* uniformData = NULL;
        VkDeviceSize uniformSize = 0;
        std::vector<Renderer::Texture const*> textures; //vector makes this optional

        void bind(const VkCommandBuffer& cmdBuffer, uint32_t currentImage) const
        {
            parentMaterial->updateUniforms(uniformData, uniformSize, textures, cmdBuffer, currentImage);
        }
    };

    //default material
    class DefaultMaterial : public Material
    {
    public:
        struct Uniforms : public BaseUniforms
        {
            glm::vec4 testVec = glm::vec4(0.0f);
        };

    private:
        MaterialInstance defaultInstance; //default instance for default material since its automatically specified when no instance is specified
        Uniforms defaultUniforms;

    public:
        DefaultMaterial(std::string vertexShaderPath, std::string fragmentShaderPath);
        ~DefaultMaterial() override;

        const MaterialInstance& getDefaultInstance() const { return defaultInstance; }
    };
}