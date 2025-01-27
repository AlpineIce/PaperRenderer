#pragma once
#include "Common.h"
#include "Materials.h"

//----------RENDER TARGETS----------//

//HDR buffer creation
struct HDRBuffer
{
    std::unique_ptr<PaperRenderer::Image> image = NULL;
    VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
};

HDRBuffer getHDRBuffer(PaperRenderer::RenderEngine& renderer, VkImageLayout startingLayout);

struct DepthBuffer
{
    std::unique_ptr<PaperRenderer::Image> image = NULL;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkImageView view = VK_NULL_HANDLE;
};

DepthBuffer getDepthBuffer(PaperRenderer::RenderEngine& renderer);

//----------RAY TRACING----------//

class ExampleRayTracing
{
private:
    //tlas
    PaperRenderer::TLAS tlas;

    //descriptors
    VkDescriptorSetLayout rtDescriptorLayout;
    PaperRenderer::ResourceDescriptor rtDescriptor;

    //general shaders
    const PaperRenderer::Shader rgenShader;
    const PaperRenderer::Shader rmissShader;
    const PaperRenderer::Shader rshadowShader;
    const std::vector<PaperRenderer::ShaderDescription> generalShaders;
    const uint32_t rayRecursionDepth;

    //ubo
    struct RayTraceInfo
    {
        uint64_t tlasAddress;
        uint64_t modelDataReference;
        uint64_t frameNumber;
        uint32_t recursionDepth;
        uint32_t aoSamples;
        float aoRadius;
        uint32_t shadowSamples;
        uint32_t reflectionSamples;
        float padding[4];
    };
    PaperRenderer::Buffer rtInfoUBO;
    
    //render pass
    PaperRenderer::RayTraceRender rtRenderPass;

    PaperRenderer::RenderEngine& renderer;
    const PaperRenderer::Camera& camera;
    const HDRBuffer& hdrBuffer;
    PaperRenderer::Buffer const* materialBuffer = NULL;
    const LightingData& lightingData;
public:
    ExampleRayTracing(PaperRenderer::RenderEngine& renderer, const PaperRenderer::Camera& camera, const HDRBuffer& hdrBuffer, const LightingData& lightingData);
    ~ExampleRayTracing();

    const PaperRenderer::Queue& rayTraceRender(const PaperRenderer::SynchronizationInfo& syncInfo, const PaperRenderer::Buffer& materialDefinitionsBuffer);
    void updateUBO() const;
    void updateHDRBuffer() const;
    void updateMaterialBuffer(const PaperRenderer::Buffer& materialDataBuffer);

    PaperRenderer::RayTraceRender& getRTRender() { return rtRenderPass; }
};

//----------RASTER----------//

class ExampleRaster
{
private:
    //descriptors for base material
    const VkDescriptorSetLayout parametersDescriptorSetLayout;
    const PaperRenderer::ResourceDescriptor parametersDescriptor;

    //base raster material
    DefaultMaterial baseMaterial;

    //default material instance
    DefaultMaterialInstance defaultMaterialInstance;

    //raster render pass
    PaperRenderer::RenderPass renderPass;

    PaperRenderer::RenderEngine& renderer;
    const PaperRenderer::Camera& camera;
    const HDRBuffer& hdrBuffer;
    const DepthBuffer& depthBuffer;
    const LightingData& lightingData;

public:
    ExampleRaster(PaperRenderer::RenderEngine& renderer, const PaperRenderer::Camera& camera, const HDRBuffer& hdrBuffer, const DepthBuffer& depthBuffer, const LightingData& lightingData);
    ~ExampleRaster();

    const PaperRenderer::Queue& rasterRender(PaperRenderer::SynchronizationInfo syncInfo);
    
    const VkDescriptorSetLayout& getParametersDescriptorSetLayout() const { return parametersDescriptorSetLayout; }
    const PaperRenderer::ResourceDescriptor& getParametersDescriptor() const { return parametersDescriptor; }
    DefaultMaterial& getDefaultMaterial() { return baseMaterial; }
    PaperRenderer::RenderPass& getRenderPass() { return renderPass; }
};

//----------BUFFER COPY PASS----------//

//Buffer copy render pass
class BufferCopyPass
{
private:
    //descriptor layout
    const VkDescriptorSetLayout setLayout;

    //buffer copy material
    class BufferCopyMaterial
    {
    private:
        //descriptor
        const PaperRenderer::ResourceDescriptor descriptor;

        //UBO
        struct UBOInputData
        {
            glm::vec4 colorFilter;
            float exposure;
            float WBtemp;
            float WBtint;
            float contrast;
            float brightness;
            float saturation;
            float gammaCorrection;
            float padding[5];
        };
        PaperRenderer::Buffer uniformBuffer;
        PaperRenderer::Material material;

        //binding function
        void bind(VkCommandBuffer cmdBuffer, const PaperRenderer::Camera& camera);

        const HDRBuffer& hdrBuffer;
        PaperRenderer::RenderEngine& renderer;
        
    public:
        BufferCopyMaterial(PaperRenderer::RenderEngine& renderer, const HDRBuffer& hdrBuffer, VkDescriptorSetLayout setLayout);
        ~BufferCopyMaterial();

        PaperRenderer::Material& getMaterial() { return material; }
        void updateUBO() const;
    } material;

    PaperRenderer::RenderEngine& renderer;
    const PaperRenderer::Camera& camera;
    const HDRBuffer& hdrBuffer;

public:
    BufferCopyPass(PaperRenderer::RenderEngine &renderer, const PaperRenderer::Camera &camera, const HDRBuffer &hdrBuffer);
    ~BufferCopyPass();

    //to render function
    const PaperRenderer::Queue& render(const PaperRenderer::SynchronizationInfo &syncInfo, bool fromRaster);
    void updateUBO() const { material.updateUBO(); }
};
