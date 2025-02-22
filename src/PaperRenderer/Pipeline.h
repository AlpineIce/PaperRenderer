#pragma once
#include "VulkanResources.h"
#include "Descriptor.h"

#include <vector>
#include <unordered_map>
#include <list>

namespace PaperRenderer
{   
    struct ShaderDescription
    {
        VkShaderStageFlagBits stage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
        const std::vector<uint32_t>& shaderData;
    };

    //----------PIPELINE BASE CLASS DECLARATIONS----------//

    class Pipeline
    {
    protected:
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

        VkPipelineLayout createPipelineLayout(class RenderEngine& renderer, const std::unordered_map<uint32_t, VkDescriptorSetLayout>& setLayouts, const std::vector<VkPushConstantRange>& pcRanges) const noexcept;

        class RenderEngine& renderer;
        
    public:
        Pipeline(class RenderEngine& renderer, const std::unordered_map<uint32_t, VkDescriptorSetLayout>& setLayouts, const std::vector<VkPushConstantRange>& pcRanges);
        virtual ~Pipeline();
        Pipeline(const Pipeline&) = delete;
        
        VkPipeline getPipeline() const { return pipeline; }
        VkPipelineLayout getLayout() const { return pipelineLayout; }
    };

    //----------COMPUTE PIPELINE DECLARATIONS----------//

    struct ComputePipelineInfo
    {
        const std::vector<uint32_t>& shaderData;
        std::unordered_map<uint32_t, VkDescriptorSetLayout> descriptorSets = {}; //set, bindings
        std::vector<VkPushConstantRange> pcRanges = {};
    };

    class ComputePipeline : public Pipeline
    {
    private:

    public:
        ComputePipeline(class RenderEngine& renderer, const ComputePipelineInfo& creationInfo);
        ~ComputePipeline() override;
        ComputePipeline(const ComputePipeline&) = delete;
    };

    //----------RASTER PIPELINE DECLARATIONS----------//

    struct RasterPipelineProperties
    {
        std::vector<VkVertexInputAttributeDescription> vertexAttributes = {}; //a good start is vec3 position, vec3 normal, vec2 UVs. Attributes are assumed to be in order
        std::vector<VkVertexInputBindingDescription> vertexDescriptions = {};
        std::vector<VkPipelineColorBlendAttachmentState> colorAttachments = {};
        std::vector<VkFormat> colorAttachmentFormats = {};
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

    struct RasterPipelineInfo
    {
        std::vector<ShaderDescription> shaders = {};
        std::unordered_map<uint32_t, VkDescriptorSetLayout> descriptorSets = {}; //includes all descriptors that will be used in the pipeline
        std::vector<VkPushConstantRange> pcRanges = {};
        RasterPipelineProperties properties = {};
    };

    class RasterPipeline : public Pipeline
    {
    private:
        const RasterPipelineProperties pipelineProperties;

    public:
        RasterPipeline(class RenderEngine& renderer, const RasterPipelineInfo& creationInfo);
        ~RasterPipeline() override;
        RasterPipeline(const RasterPipeline&) = delete;

        const RasterPipelineProperties& getPipelineProperties() const { return pipelineProperties; }
    };

    //----------RAY TRACING PIPELINE DECLARATIONS----------//

    struct RTPipelineProperties
    {
        uint32_t maxRecursionDepth = 1;
    };

    struct RTPipelineInfo
    {
        std::vector<struct ShaderHitGroup*> materials = {};
        std::vector<uint32_t> const* raygenShader = NULL;
        std::vector<std::vector<uint32_t>> const* missShaders = NULL;
        std::vector<std::vector<uint32_t>> const* callableShaders = NULL;
        std::unordered_map<uint32_t, VkDescriptorSetLayout> descriptorSets = {};
        std::vector<VkPushConstantRange> pcRanges = {};
        RTPipelineProperties properties = {};
    };

    struct RTShaderBindingTableData
    {
        VkStridedDeviceAddressRegionKHR raygenShaderBindingTable = {};
        VkStridedDeviceAddressRegionKHR missShaderBindingTable = {};
        VkStridedDeviceAddressRegionKHR hitShaderBindingTable = {};
        VkStridedDeviceAddressRegionKHR callableShaderBindingTable = {};
        std::unordered_map<struct ShaderHitGroup const*, uint32_t> materialShaderGroupOffsets; //aka hit group offsets
    };

    class RTPipeline : public Pipeline
    {
    private:
        const RTPipelineProperties pipelineProperties;
        RTShaderBindingTableData shaderBindingTableData = {};
        std::vector<char> sbtRawData;
        std::unique_ptr<Buffer> sbtBuffer;

        void enumerateShaders(
            const std::vector<std::vector<uint32_t>>& shaders,
            std::vector<VkRayTracingShaderGroupCreateInfoKHR>& shaderGroups,
            std::vector<VkShaderModuleCreateInfo>& shaderModuleInfos,
            std::vector<VkPipelineShaderStageCreateInfo>& shaderStages,
            VkShaderStageFlagBits stage);
        void insertGroupSBTData(std::vector<char>& toInsertData, uint32_t groupOffset, uint32_t handleCount) const;
        void rebuildSBTBuffer(RenderEngine& renderer);

    public:
        RTPipeline(class RenderEngine& renderer, const RTPipelineInfo& creationInfo);
        ~RTPipeline() override;
        RTPipeline(const RTPipeline&) = delete;

        void assignOwner(const Queue& queue) { sbtBuffer->addOwner(queue); }

        const RTPipelineProperties& getPipelineProperties() const { return pipelineProperties; }
        const RTShaderBindingTableData& getShaderBindingTableData() const { return shaderBindingTableData; }
    };
}