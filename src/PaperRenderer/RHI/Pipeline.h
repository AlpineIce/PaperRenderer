#pragma once
#include "Memory/VulkanResources.h"

#include <vector>
#include <unordered_map>

namespace PaperRenderer
{   
    //----------SHADER DECLARATIONS----------//

    struct ShaderPair
    {
        VkShaderStageFlagBits stage;
        std::string directory;
    };

    struct DescriptorSet
    {
        std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> descriptorBindings; //CANNOT SKIP BINDINGS
    };

    class Shader
    {
    private:
        VkShaderModule program;
        std::vector<uint32_t> compiledShader;
        
        class Device* devicePtr;

        std::vector<uint32_t> getShaderData(std::string location);

    public:
        Shader(class Device* device, std::string location);
        ~Shader();
        
        VkShaderModule getModule() const { return program; }
    };

    //----------PIPELINE DECLARATIONS----------//

    //Pipeline base class
    struct PipelineCreationInfo
    {
        class RenderEngine* renderer;
        VkPipelineCache cache;
        std::unordered_map<uint32_t, VkDescriptorSetLayout> setLayouts;
        VkPipelineLayout pipelineLayout;
    };

    class Pipeline
    {
    protected:
        VkPipeline pipeline;
        std::unordered_map<uint32_t, VkDescriptorSetLayout> setLayouts;
        VkPipelineLayout pipelineLayout;

        class RenderEngine* rendererPtr;
        
    public:
        Pipeline(const PipelineCreationInfo& creationInfo);
        virtual ~Pipeline();
        
        VkPipeline getPipeline() const { return pipeline; }
        const std::unordered_map<uint32_t, VkDescriptorSetLayout>& getDescriptorSetLayouts() const { return setLayouts; } //TODO USE AN ENUM FOR USABLE SET LAYOUT INDICES
        VkPipelineLayout getLayout() const { return pipelineLayout; }
    };

    //compute pipeline
    struct ComputePipelineCreationInfo : public PipelineCreationInfo
    {
        std::shared_ptr<Shader> shader;
    };

    struct ComputePipelineBuildInfo
    {
        ShaderPair* shaderInfo;
        std::unordered_map<uint32_t, DescriptorSet>* descriptors;
    };

    class ComputePipeline : public Pipeline
    {
    private:

    public:
        ComputePipeline(const ComputePipelineCreationInfo& creationInfo);
        ~ComputePipeline() override;
        
    };

    //raster pipeline
    struct RasterPipelineCreationInfo : public PipelineCreationInfo
    {
        std::unordered_map<VkShaderStageFlagBits, std::shared_ptr<Shader>> shaders;
    };

    struct RasterPipelineBuildInfo
    {
        std::vector<ShaderPair>* shaderInfo;
        std::unordered_map<uint32_t, DescriptorSet>* descriptors;
    };

    struct RasterPipelineProperties
    {
        std::vector<VkVertexInputAttributeDescription> vertexAttributes; //a good start is vec3 position, vec3 normal, vec2 UVs. Attributes are assumed to be in order
        VkVertexInputBindingDescription vertexDescription = {};
        std::vector<VkPipelineColorBlendAttachmentState> colorAttachments = {
            {
                .blendEnable = VK_FALSE,
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
            .back = {},
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f
        };
    };

    class RasterPipeline : public Pipeline
    {
    private:
        RasterPipelineProperties pipelineProperties;

    public:
        RasterPipeline(const RasterPipelineCreationInfo& creationInfo, const RasterPipelineProperties& pipelineProperties);
        ~RasterPipeline() override;
        
        const RasterPipelineProperties& getPipelineProperties() const { return pipelineProperties; }

    };

    //ray tracing pipeline
    struct RTPipelineCreationInfo : public PipelineCreationInfo
    {
        std::unordered_map<VkShaderStageFlagBits, std::shared_ptr<Shader>> shaders;
    };

    struct RTPipelineBuildInfo
    {
        std::vector<class Material*> materials;
        std::unordered_map<uint32_t, DescriptorSet>* descriptors;
    };

    struct RTPipelineProperties
    {
        uint32_t MAX_RT_RECURSION_DEPTH = 0;
    };

    class RTPipeline : public Pipeline
    {
    private:
        RTPipelineProperties pipelineProperties;
        VkDeferredOperationKHR deferredOperation;

    public:
        RTPipeline(const RTPipelineCreationInfo& creationInfo, const RTPipelineProperties& pipelineProperties);
        ~RTPipeline() override;
        
        const RTPipelineProperties& getPipelineProperties() const { return pipelineProperties; }

        bool isBuilt();
    };

    //----------PIPELINE BUILDER DECLARATIONS----------//

    class PipelineBuilder
    {
    private:
        VkPipelineCache cache;

        std::shared_ptr<Shader> createShader(const ShaderPair& pair) const;
        std::unordered_map<uint32_t, VkDescriptorSetLayout> createDescriptorLayouts(const std::unordered_map<uint32_t, DescriptorSet>& descriptorSets) const;
        VkPipelineLayout createPipelineLayout(const std::unordered_map<uint32_t, VkDescriptorSetLayout>& setLayouts) const;

        class RenderEngine* rendererPtr;

    public:
        PipelineBuilder(RenderEngine* renderer);
        ~PipelineBuilder();

        std::unique_ptr<ComputePipeline> buildComputePipeline(const ComputePipelineBuildInfo& info) const;
        std::unique_ptr<RasterPipeline> buildRasterPipeline(const RasterPipelineBuildInfo& info, const RasterPipelineProperties& pipelineProperties) const;
        std::unique_ptr<RTPipeline> buildRTPipeline(const RTPipelineBuildInfo& info, const RTPipelineProperties& pipelineProperties) const;
    };
}