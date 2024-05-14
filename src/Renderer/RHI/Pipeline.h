#pragma once
#include "Descriptor.h"
#include "Memory/VulkanResources.h"
#include "Swapchain.h"

#include <vector>

namespace PaperRenderer
{   
    enum VertexLayout
    {
        VEC3VEC3VEC2 = 0
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
        std::unordered_map<uint32_t, VkDescriptorSetLayout> setLayouts;
        VkPipelineLayout pipelineLayout;
    };

    struct RTPipelineInfo
    {
        uint32_t MAX_RT_RECURSION_DEPTH = 0;
    };

    enum RasterDescriptorScopes
    {
        MATERIAL = 0,
        MATERIAL_INSTANCE = 1,
        OBJECT = 2
    };

    //Pipeline base class
    class Pipeline
    {
    protected:
        std::unordered_map<VkShaderStageFlagBits, std::shared_ptr<Shader>> shaders;
        VkPipeline pipeline;
        std::unordered_map<uint32_t, VkDescriptorSetLayout> setLayouts;
        VkPipelineLayout pipelineLayout;

        Device* devicePtr;
        DescriptorAllocator* descriptorsPtr;
        
    public:
        Pipeline(const PipelineCreationInfo& creationInfo);
        virtual ~Pipeline();
        
        VkPipeline getPipeline() const { return pipeline; }
        const std::unordered_map<uint32_t, VkDescriptorSetLayout>& getDescriptorSetLayouts() const { return setLayouts; } //TODO USE AN ENUM FOR USABLE SET LAYOUT INDICES
        VkPipelineLayout getLayout() const { return pipelineLayout; }
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
        uint32_t setNumber;
        std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> descriptorBindings; //CANNOT SKIP BINDINGS
    };

    struct PipelineBuildInfo
    {
        std::vector<ShaderPair>* shaderInfo;
        std::unordered_map<uint32_t, DescriptorSet*>* descriptors;
    };

    class PipelineBuilder
    {
    private:
        VkPipelineCache cache;

        Device* devicePtr;
        DescriptorAllocator* descriptorsPtr;
        Swapchain* swapchainPtr;

        std::shared_ptr<Shader> createShader(const ShaderPair& pair) const;
        std::unordered_map<uint32_t, VkDescriptorSetLayout> createDescriptorLayouts(const std::unordered_map<uint32_t, DescriptorSet*>& descriptorSets) const;
        VkPipelineLayout createPipelineLayout(const std::unordered_map<uint32_t, VkDescriptorSetLayout>& setLayouts) const;
        PipelineCreationInfo initPipelineInfo(PipelineBuildInfo info) const;
        RTPipelineInfo initRTinfo() const;

    public:
        PipelineBuilder(Device* device, DescriptorAllocator* descriptors, Swapchain* swapchain);
        ~PipelineBuilder();

        std::unique_ptr<ComputePipeline> buildComputePipeline(const PipelineBuildInfo& info) const;
        std::unique_ptr<RasterPipeline> buildRasterPipeline(const PipelineBuildInfo& info) const;
        std::unique_ptr<RTPipeline> buildRTPipeline(const PipelineBuildInfo& info) const;
    };
}