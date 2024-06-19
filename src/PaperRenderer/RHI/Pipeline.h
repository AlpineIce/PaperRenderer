#pragma once
#include "Descriptor.h"
#include "Memory/VulkanResources.h"
#include "Swapchain.h"

#include <vector>

namespace PaperRenderer
{   
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

    struct RasterPipelineProperties
    {
        std::vector<VkVertexInputAttributeDescription> vertexAttributes; //a good start is vec3 position, vec3 normal, vec2 UVs. Attributes are assumed to be in order
        VkVertexInputBindingDescription vertexDescription = {};
        std::vector<VkPipelineColorBlendAttachmentState> colorAttachments = {
            {
                .blendEnable = VK_TRUE,
                .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
                .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                .colorBlendOp = VK_BLEND_OP_ADD,
                .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                .alphaBlendOp = VK_BLEND_OP_ADD,
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
            }
        };
        std::vector<VkFormat> colorAttachmentFormats;
        VkFormat depthAttachmentFormat = VK_FORMAT_UNDEFINED;
        VkFormat stencilAttachmentFormat = VK_FORMAT_UNDEFINED;
        VkPipelineTessellationStateCreateInfo tessellationInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .patchControlPoints = 1
        };
        VkPipelineRasterizationStateCreateInfo rasterInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = 0.0f,
            .lineWidth = 1.0f
        };
        VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = NULL,
            .flags = 0,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front = {},
            .back = {}
        };
    };

    struct RTPipelineProperties
    {
        uint32_t MAX_RT_RECURSION_DEPTH = 0;
    };

    struct PipelineCreationInfo
    {
        Device *device;
        DescriptorAllocator* descriptors;
        VkPipelineCache cache;
        std::unordered_map<VkShaderStageFlagBits, std::shared_ptr<Shader>> shaders;
        std::unordered_map<uint32_t, VkDescriptorSetLayout> setLayouts;
        VkPipelineLayout pipelineLayout;
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
        RasterPipelineProperties pipelineProperties;

    public:
        RasterPipeline(const PipelineCreationInfo& creationInfo, const RasterPipelineProperties& pipelineProperties, Swapchain* swapchain);
        ~RasterPipeline() override;
        
        const RasterPipelineProperties& getPipelineProperties() const { return pipelineProperties; }

    };

    class RTPipeline : public Pipeline
    {
    private:
        RTPipelineProperties pipelineProperties;
        VkDeferredOperationKHR deferredOperation;

    public:
        RTPipeline(const PipelineCreationInfo& creationInfo, const RTPipelineProperties& pipelineProperties);
        ~RTPipeline() override;
        
        const RTPipelineProperties& getPipelineProperties() const { return pipelineProperties; }

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
        std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> descriptorBindings; //CANNOT SKIP BINDINGS
    };

    struct ComputePipelineBuildInfo
    {
        ShaderPair* shaderInfo;
        std::unordered_map<uint32_t, DescriptorSet>* descriptors;
    };

    struct PipelineBuildInfo
    {
        std::vector<ShaderPair>* shaderInfo;
        std::unordered_map<uint32_t, DescriptorSet>* descriptors;
    };

    //"helper" struct to decrease parameters needed for construction later on
    struct PipelineRendererInfo
    {
        Device* devicePtr;
        DescriptorAllocator* descriptorsPtr;
        class PipelineBuilder* pipelineBuilderPtr; //epic forward declaration
    };

    class PipelineBuilder
    {
    private:
        VkPipelineCache cache;

        static PipelineRendererInfo rendererInfo;

        Device* devicePtr;
        DescriptorAllocator* descriptorsPtr;
        Swapchain* swapchainPtr;

        std::shared_ptr<Shader> createShader(const ShaderPair& pair) const;
        std::unordered_map<uint32_t, VkDescriptorSetLayout> createDescriptorLayouts(const std::unordered_map<uint32_t, DescriptorSet>& descriptorSets) const;
        VkPipelineLayout createPipelineLayout(const std::unordered_map<uint32_t, VkDescriptorSetLayout>& setLayouts) const;
        PipelineCreationInfo initPipelineInfo(PipelineBuildInfo info) const;

    public:
        PipelineBuilder(Device* device, DescriptorAllocator* descriptors, Swapchain* swapchain);
        ~PipelineBuilder();

        std::unique_ptr<ComputePipeline> buildComputePipeline(const ComputePipelineBuildInfo& info) const;
        std::unique_ptr<RasterPipeline> buildRasterPipeline(const PipelineBuildInfo& info, const RasterPipelineProperties& pipelineProperties) const;
        std::unique_ptr<RTPipeline> buildRTPipeline(const PipelineBuildInfo& info, const RTPipelineProperties& pipelineProperties) const;

        static PipelineRendererInfo getRendererInfo() { return rendererInfo; }
    };
}