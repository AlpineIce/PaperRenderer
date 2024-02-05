#pragma once
#include "Descriptor.h"
#include "Buffer.h"
#include "Swapchain.h"

#include <vector>
#include <memory>

namespace Renderer
{   
    enum VertexLayout
    {
        VEC3VEC3VEC2 = 0
    };

    enum PipelineType
    {
        UNDEFINED = 0,
        PBR = 1,
        TexturelessPBR = 2,
        PathTracing = 3

    };

    struct CameraData
    {
        glm::mat4 view;
        glm::mat4 projection;
    };

    struct AmbientLight
    {
        glm::vec4 color = glm::vec4(1.0f);
    };
    
    struct DirectLight
    {
        glm::vec4 direction = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
        glm::vec4 color = glm::vec4(1.0f);
        //float softness = 0.0f;
    };

    struct PointLight
    {
        glm::vec4 position = glm::vec4(0.0f);
        glm::vec4 color = glm::vec4(1.0f);
        //float radius = 0.0f;
    };

    struct LightInfo
    {
        glm::vec4 camPos;
        AmbientLight ambient;
        DirectLight sun;
        PointLight pointLights[MAX_POINT_LIGHTS];
    };

    struct GlobalDescriptor
    {
        CameraData cameraData;
        LightInfo lightInfo;
    };

    struct PBRpipelineUniforms
    {
        glm::vec4 bruh;
    };

    struct TexturelessPBRpipelineUniforms
    {
        glm::vec4 inColors[TEXTURE_ARRAY_SIZE] = {glm::vec4(0.0f)};
    };

    //----------SHADER DECLARATIONS----------//

    class Shader
    {
    private:
        VkShaderModule program;
        std::vector<uint32_t> compiledShader;
        
        Device* devicePtr;

        std::vector<uint32_t> getShaderData(std::string location);

    public:
        Shader(Device* device, std::string location);
        ~Shader();
        
        VkShaderModule getModule() const { return program; }
    };

    //----------PIPELINE DECLARATIONS----------//

    struct PipelineCreationInfo
    {
        Device *device;
        DescriptorAllocator* descriptors;
        VkPipelineCache cache;
        std::unordered_map<VkShaderStageFlagBits, std::shared_ptr<Shader>> shaders;
        std::vector<VkDescriptorSetLayout> setLayouts;
        VkDescriptorSetLayout const* globalDescriptorLayoutPtr;
        VkPipelineLayout pipelineLayout;
    };

    struct RTPipelineInfo
    {
        uint32_t MAX_RT_RECURSION_DEPTH = 0;
    };

    //Pipeline base class
    class Pipeline
    {
    protected:
        std::unordered_map<VkShaderStageFlagBits, std::shared_ptr<Shader>> shaders;
        VkPipeline pipeline;
        std::vector<VkDescriptorSetLayout> setLayouts;
        VkDescriptorSetLayout const* globalDescriptorLayoutPtr;
        VkPipelineLayout pipelineLayout;
        PipelineType pipelineType;

        Device* devicePtr;
        DescriptorAllocator* descriptorsPtr;
        
    public:
        Pipeline(const PipelineCreationInfo& creationInfo);
        virtual ~Pipeline();
        
        VkPipeline getPipeline() const { return pipeline; }
        std::vector<VkDescriptorSetLayout> getDescriptorSetLayouts() const { return setLayouts; }
        VkDescriptorSetLayout const* getGlobalDescriptorLayoutPtr() const { return globalDescriptorLayoutPtr; }
        VkPipelineLayout getLayout() const { return pipelineLayout; }
        PipelineType getPipelineType() const { return pipelineType; }
    };

    class ComputePipeline : public Pipeline
    {
    private:

    public:
        ComputePipeline(const PipelineCreationInfo& creationInfo);
        ~ComputePipeline() override;
        
    };

    class RasterPipeline : public Pipeline
    {
    private:
        //vertex layout
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;
        VkVertexInputBindingDescription vertexDescription = {};
        VertexLayout vertexLayout;

    public:
        RasterPipeline(const PipelineCreationInfo& creationInfo, Swapchain* swapchain);
        ~RasterPipeline() override;

    };

    class RTPipeline : public Pipeline
    {
    private:
        RTPipelineInfo rtInfo;
        VkDeferredOperationKHR deferredOperation;

    public:
        RTPipeline(const PipelineCreationInfo& creationInfo, const RTPipelineInfo& rtInfo);
        ~RTPipeline() override;

        bool isBuilt();
    };

    //----------PIPELINE BUILDER DECLARATIONS----------//

    struct ShaderPair
    {
        VkShaderStageFlagBits stage;
        std::string directory;
    };

    struct DescriptorSet
    {
        std::vector<VkDescriptorSetLayoutBinding> descriptorBindings;
    };

    struct PipelineBuildInfo
    {
        std::vector<ShaderPair> shaderInfo;
        //IMPORTANT descriptor sets must be in order of set binding in this vector
        bool useGlobalDescriptor = true;
        std::vector<DescriptorSet> descriptors;
    };

    class PipelineBuilder
    {
    private:
        Device* devicePtr;
        DescriptorAllocator* descriptorsPtr;
        Swapchain* swapchainPtr;

        VkPipelineCache cache;
        VkDescriptorSetLayout globalDescriptorLayout;
        VkPipelineLayout globalPipelineLayout;

        std::shared_ptr<Shader> createShader(const ShaderPair& pair) const;
        std::vector<VkDescriptorSetLayout> createDescriptorLayouts(const std::vector<DescriptorSet>& descriptorSets) const;
        VkPipelineLayout createPipelineLayout(const std::vector<VkDescriptorSetLayout>& setLayouts) const;
        PipelineCreationInfo initPipelineInfo(PipelineBuildInfo info) const;
        RTPipelineInfo initRTinfo() const;

    public:
        PipelineBuilder(Device* device, DescriptorAllocator* descriptors, Swapchain* swapchain);
        ~PipelineBuilder();

        std::shared_ptr<ComputePipeline> buildComputePipeline(const PipelineBuildInfo& info) const;
        std::shared_ptr<RasterPipeline> buildRasterPipeline(const PipelineBuildInfo& info) const;
        std::shared_ptr<RTPipeline> buildRTPipeline(const PipelineBuildInfo& info) const;

        VkDescriptorSetLayout getGlobalDescriptorLayout() const { return globalDescriptorLayout; }
    };
}