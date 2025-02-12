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
    std::unique_ptr<PaperRenderer::TLAS> primaryTLAS; //note that multiple TLAS' can be used strategically, but this example uses 1

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
    PaperRenderer::TLAS& getTLAS() { return *primaryTLAS; }
};

//----------RASTER----------//

class ExampleRaster
{
private:
    //descriptors for base material
    const VkDescriptorSetLayout parametersDescriptorSetLayout;
    const PaperRenderer::ResourceDescriptor parametersDescriptor;

    //default material shaders
    const PaperRenderer::Shader defaultVertShader;
    const PaperRenderer::Shader defaultFragShader;

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
    const PaperRenderer::Shader& getDefaultVertShader() const { return defaultVertShader; }
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
        const PaperRenderer::Shader vertShader;
        const PaperRenderer::Shader fragShader;
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

        void updateHDRBuffer() const;
        void updateUBO() const;

        PaperRenderer::Material& getMaterial() { return material; }
    } material;

    PaperRenderer::RenderEngine& renderer;
    const PaperRenderer::Camera& camera;
    const HDRBuffer& hdrBuffer;

public:
    BufferCopyPass(PaperRenderer::RenderEngine &renderer, const PaperRenderer::Camera &camera, const HDRBuffer &hdrBuffer);
    ~BufferCopyPass();

    void updateHDRBuffer() const { material.updateHDRBuffer(); }
    void updateUBO() const { material.updateUBO(); }

    //to render function
    const PaperRenderer::Queue& render(const PaperRenderer::SynchronizationInfo &syncInfo, bool fromRaster);
};
