#pragma once
#include "VulkanResources.h"

#include <vector>
#include <unordered_map>
#include <list>

namespace PaperRenderer
{   
    //----------SHADER DECLARATIONS----------//

    struct ShaderPair
    {
        VkShaderStageFlagBits stage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
        std::vector<uint32_t> data;
    };

    struct ShaderDescription
    {
        VkShaderStageFlagBits stage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
        class Shader const* shader = NULL;
    };

    class Shader
    {
    private:
        VkShaderModule program;
        
        class RenderEngine& renderer;

    public:
        Shader(class RenderEngine& renderer, const std::vector<uint32_t>& data);
        ~Shader();
        Shader(const Shader&) = delete;
        
        VkShaderModule getModule() const { return program; }
    };

    //----------PIPELINE DECLARATIONS----------//

    //Pipeline base class
    struct PipelineCreationInfo
    {
        class RenderEngine& renderer;
        VkPipelineCache cache;
        std::unordered_map<uint32_t, VkDescriptorSetLayout> setLayouts;
        std::vector<VkPushConstantRange> pcRanges;
        VkPipelineLayout pipelineLayout;
    };

    class Pipeline
    {
    protected:
        VkPipeline pipeline;
        std::unordered_map<uint32_t, VkDescriptorSetLayout> setLayouts;
        VkPipelineLayout pipelineLayout;

        class RenderEngine& renderer;
        
    public:
        Pipeline(const PipelineCreationInfo& creationInfo);
        virtual ~Pipeline();
        Pipeline(const Pipeline&) = delete;
        
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
        const ShaderPair& shaderInfo;
        const std::unordered_map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>>& descriptors; //set, bindings
        const std::vector<VkPushConstantRange>& pcRanges;
    };

    class ComputePipeline : public Pipeline
    {
    private:

    public:
        ComputePipeline(const ComputePipelineCreationInfo& creationInfo);
        ~ComputePipeline() override;
        ComputePipeline(const ComputePipeline&) = delete;
    };

    //raster pipeline
    struct RasterPipelineCreationInfo : public PipelineCreationInfo
    {
        std::unordered_map<VkShaderStageFlagBits, std::shared_ptr<Shader>> shaders;
        const uint32_t drawDescriptorIndex;
    };

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

    struct RasterPipelineBuildInfo
    {
        std::vector<ShaderPair> shaderInfo;
        //SET 0 BINDING 0 AND SET 2 BINDING 0 ARE RESERVED (camera matrices and model matrices respectively)
        std::unordered_map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>> descriptorSets;
        std::vector<VkPushConstantRange> pcRanges;
        RasterPipelineProperties properties = {};
        //Lowest level descriptor, used for drawing. Should be mat4[] at binding 0, which will be filled
        //in with model matrix data by the renderer. Default value is 1, which means it will use set 1, binding 0 unless otherwise specified
        const uint32_t drawDescriptorIndex = 1;
    };

    class RasterPipeline : public Pipeline
    {
    private:
        const RasterPipelineProperties pipelineProperties;
        const uint32_t drawDescriptorIndex;

    public:
        RasterPipeline(const RasterPipelineCreationInfo& creationInfo, const RasterPipelineProperties& pipelineProperties);
        ~RasterPipeline() override;
        RasterPipeline(const RasterPipeline&) = delete;

        const RasterPipelineProperties& getPipelineProperties() const { return pipelineProperties; }
        const uint32_t& getDrawDescriptorIndex() const { return drawDescriptorIndex; }
    };

    //ray tracing pipeline
    struct RTPipelineCreationInfo : public PipelineCreationInfo
    {
        const std::vector<class RTMaterial*>& materials;
        const ShaderDescription& raygenShader;
        const std::vector<ShaderDescription>& missShaders;
        const std::vector<ShaderDescription>& callableShaders;
    };

    struct RTPipelineProperties
    {
        uint32_t maxRecursionDepth = 1;
    };

    struct RTPipelineBuildInfo
    {
        const std::vector<class RTMaterial*>& materials;
        const ShaderDescription& raygenShader;
        const std::vector<ShaderDescription>& missShaders;
        const std::vector<ShaderDescription>& callableShaders;
        const std::unordered_map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>>& descriptorSets;
        const std::vector<VkPushConstantRange>& pcRanges;
        const RTPipelineProperties& properties;
    };
    
    struct RTShaderBindingTableOffsets
    {
        std::unordered_map<class RTMaterial const*, uint32_t> materialShaderGroupOffsets; //aka hit group offsets
        std::unordered_map<Shader const*, uint32_t> raygenGroupOffsets;
        std::unordered_map<Shader const*, uint32_t> missGroupOffsets;
        std::unordered_map<Shader const*, uint32_t> callableGroupOffsets;
    };

    struct RTShaderBindingTableData
    {
        VkStridedDeviceAddressRegionKHR raygenShaderBindingTable = {};
        VkStridedDeviceAddressRegionKHR missShaderBindingTable = {};
        VkStridedDeviceAddressRegionKHR hitShaderBindingTable = {};
        VkStridedDeviceAddressRegionKHR callableShaderBindingTable = {};
        RTShaderBindingTableOffsets shaderBindingTableOffsets = {};
    };

    class RTPipeline : public Pipeline
    {
    private:
        const RTPipelineProperties pipelineProperties;
        RTShaderBindingTableData shaderBindingTableData = {};
        std::vector<char> sbtRawData;
        std::unique_ptr<Buffer> sbtBuffer;

        void enumerateShaders(
            const std::vector<ShaderDescription>& shaders,
            std::unordered_map<Shader const*, uint32_t>& offsets,
            std::vector<VkRayTracingShaderGroupCreateInfoKHR>& shaderGroups,
            std::vector<VkPipelineShaderStageCreateInfo>& shaderStages,
            VkShaderStageFlagBits stage);
        uint32_t insertGroupSBTData(std::vector<char>& toInsertData, uint32_t groupOffset, uint32_t handleCount) const;
        void rebuildSBTBuffer(RenderEngine& renderer);

    public:
        RTPipeline(const RTPipelineCreationInfo& creationInfo, const RTPipelineProperties& properties);
        ~RTPipeline() override;
        RTPipeline(const RTPipeline&) = delete;

        const RTPipelineProperties& getPipelineProperties() const { return pipelineProperties; }
        const RTShaderBindingTableData& getShaderBindingTableData() const { return shaderBindingTableData; }
    };

    //----------PIPELINE BUILDER DECLARATIONS----------//

    class PipelineBuilder
    {
    private:
        VkPipelineCache cache = VK_NULL_HANDLE;

        std::unordered_map<uint32_t, VkDescriptorSetLayout> createDescriptorLayouts(const std::unordered_map<uint32_t, std::vector<VkDescriptorSetLayoutBinding>>& descriptorSets) const;
        VkPipelineLayout createPipelineLayout(const std::unordered_map<uint32_t, VkDescriptorSetLayout>& setLayouts, const std::vector<VkPushConstantRange>& pcRanges) const;

        class RenderEngine& renderer;

    public:
        PipelineBuilder(RenderEngine& renderer);
        ~PipelineBuilder();
        PipelineBuilder(const PipelineBuilder&) = delete;
        
        const VkPipelineCache& getPipelineCache() const { return cache; }
        std::unique_ptr<ComputePipeline> buildComputePipeline(const ComputePipelineBuildInfo& info) const;
        std::unique_ptr<RasterPipeline> buildRasterPipeline(const RasterPipelineBuildInfo& info) const;
        std::unique_ptr<RTPipeline> buildRTPipeline(const RTPipelineBuildInfo& info) const;
    };
}