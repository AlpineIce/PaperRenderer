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
        PBR = 0
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
        ComputePipeline(Device *device, Descriptors* descriptors, std::string shaderLocation);
        ~ComputePipeline();
        
    };

    //Pipeline base class for raster and RT pipelines
    class Pipeline
    {
    protected:
        std::unordered_map<VkShaderStageFlagBits, std::shared_ptr<Shader>> shaders; //shared ptr because i give up lmao
        VkPipeline pipeline;
        VkPushConstantRange pushConstantRange;
        VkPipelineLayout pipelineLayout;
        
        static VkPipelineCache cache;

        Descriptors* descriptorsPtr;
        Device* devicePtr;

        void createShaders(std::vector<std::string>& shaderFiles);
        void createPipelineLayout();

    public:
        Pipeline(Device *device, std::vector<std::string>& shaderFiles, Descriptors* descriptors);
        virtual ~Pipeline();

        static void createCache(Device* device);
        static void destroyCache(Device* device);

        VkPipeline getPipeline() const { return pipeline; }
        VkPipelineLayout getLayout() const { return pipelineLayout; }
        VkPushConstantRange getPushConstantRange() const { return pushConstantRange; }
    };

    //renders cool stuff using raster
    class RasterPipeline : public Pipeline
    {
    private:
        //vertex layout
        VkVertexInputBindingDescription vertexDescription = {};
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;
        VertexLayout vertexLayout;

    public:
        RasterPipeline(Device* device, std::vector<std::string>& shaderFiles, Descriptors* descriptors, PipelineType pipelineType, Swapchain* swapchain);
        ~RasterPipeline() override;

    };

    //ray tracing pipeline
    class RTPipeline : public Pipeline
    {
    private:

    public:
        RTPipeline(Device *device, std::vector<std::string>& shaderFiles, Descriptors* descriptors);
        ~RTPipeline() override;
    };
}