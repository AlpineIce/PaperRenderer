#pragma once
#include "vulkan/vulkan.hpp"
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
        TexturelessPBR = 2

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

    class ComputePipeline
    {
    private:
        std::shared_ptr<Shader> shader;

    public:
        ComputePipeline(Device *device, DescriptorAllocator* descriptors, std::string shaderLocation);
        ~ComputePipeline();
        
    };

    //Pipeline base class for raster and RT pipelines
    class Pipeline
    {
    protected:
        std::unordered_map<VkShaderStageFlagBits, std::shared_ptr<Shader>> shaders; //shared ptr because i give up lmao
        std::shared_ptr<UniformBuffer> materialUBO;
        VkPipeline pipeline;
        VkPipelineLayout pipelineLayout;
        VkPushConstantRange pushConstantRange;
        VkDescriptorSetLayout descriptorLayout;
        
        PipelineType pipelineType = UNDEFINED;
        
        static VkPipelineCache cache;
        static VkDescriptorSetLayout globalDescriptorLayout;
        static VkPipelineLayout globalPipelineLayout;

        DescriptorAllocator* descriptorsPtr;
        Device* devicePtr;
        CmdBufferAllocator* commandsPtr;

        void createShaders(std::vector<std::string>& shaderFiles);

    public:
        Pipeline(Device *device, CmdBufferAllocator* commands, std::vector<std::string>& shaderFiles, DescriptorAllocator* descriptors);
        virtual ~Pipeline();

        static void createCache(Device* device);
        static void destroyCache(Device* device);
        static void createGlobalDescriptorLayout(Device* device);
        static void destroyGlobalDescriptorLayout(Device* device);
        static VkDescriptorSetLayout getGlobalDescriptorLayout() { return globalDescriptorLayout; }

        VkPipeline getPipeline() const { return pipeline; }
        PipelineType getPipelineType() const { return pipelineType; }
        VkDescriptorSetLayout getDescriptorLayout() const { return descriptorLayout; }
        UniformBuffer* getUBO() const { return materialUBO.get(); }
        VkPipelineLayout getLayout() const { return pipelineLayout; }
        VkPushConstantRange getPushConstantRange() const { return pushConstantRange; }
    };

    //renders cool stuff using raster
    class RasterPipeline : public Pipeline
    {
    private:
        //vertex layout
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;
        VkVertexInputBindingDescription vertexDescription = {};
        VertexLayout vertexLayout;

        void createDescriptorLayout();
    public:
        RasterPipeline(Device* device, CmdBufferAllocator* commands, std::vector<std::string>& shaderFiles, DescriptorAllocator* descriptors, PipelineType pipelineType, Swapchain* swapchain);
        ~RasterPipeline() override;

    };

    //ray tracing pipeline
    class RTPipeline : public Pipeline
    {
    private:

    public:
        RTPipeline(Device *device, std::vector<std::string>& shaderFiles, DescriptorAllocator* descriptors);
        ~RTPipeline() override;
    };
}