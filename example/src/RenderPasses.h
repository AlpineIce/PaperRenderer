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

    //general shaders
    const PaperRenderer::Shader rgenShader;
    const PaperRenderer::Shader rmissShader;
    const PaperRenderer::Shader rshadowShader;
    const std::vector<PaperRenderer::ShaderDescription> generalShaders;

    //ubo
    struct RayTraceInfo
    {
        glm::mat4 projection;
        glm::mat4 view;
        uint64_t modelDataReference;
        uint64_t frameNumber;
    };
    PaperRenderer::Buffer rtInfoUBO;
    
    //render pass
    PaperRenderer::RayTraceRender rtRenderPass;

    PaperRenderer::RenderEngine& renderer;
    const PaperRenderer::Camera& camera;
    const HDRBuffer& hdrBuffer;
    const PaperRenderer::Buffer& lightBuffer;
    const PaperRenderer::Buffer& lightInfoUBO;
public:
    ExampleRayTracing(PaperRenderer::RenderEngine& renderer, const PaperRenderer::Camera& camera, const HDRBuffer& hdrBuffer, const PaperRenderer::Buffer& lightBuffer, const PaperRenderer::Buffer& lightInfoUBO);
    ~ExampleRayTracing();

    void rayTraceRender(const PaperRenderer::SynchronizationInfo& syncInfo);
    void updateUBO();

    PaperRenderer::RayTraceRender& getRTRender() { return rtRenderPass; }
};

//----------RASTER----------//

class ExampleRaster
{
private:
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
    const PaperRenderer::Buffer& lightBuffer;
    const PaperRenderer::Buffer& lightInfoUBO;
public:
    ExampleRaster(PaperRenderer::RenderEngine& renderer, const PaperRenderer::Camera& camera, const HDRBuffer& hdrBuffer, const DepthBuffer& depthBuffer, const PaperRenderer::Buffer& lightBuffer, const PaperRenderer::Buffer& lightInfoUBO);
    ~ExampleRaster();

    void rasterRender(const PaperRenderer::SynchronizationInfo& syncInfo);
    
    const DefaultMaterial& getDefaultMaterial() const { return baseMaterial; }
    PaperRenderer::RenderPass& getRenderPass() { return renderPass; }
};

//----------BUFFER COPY PASS----------//

//Buffer copy render pass
class BufferCopyPass
{
private:
    //buffer copy material
    class BufferCopyMaterial : public PaperRenderer::Material
    {
    private:
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
        };
        PaperRenderer::Buffer uniformBuffer;

        const HDRBuffer& hdrBuffer;
        
    public:
        BufferCopyMaterial(PaperRenderer::RenderEngine& renderer, const HDRBuffer& hdrBuffer);
        ~BufferCopyMaterial() override;

        void bind(VkCommandBuffer cmdBuffer, const PaperRenderer::Camera& camera, std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites>& descriptorWrites) override;
    } material;

    PaperRenderer::RenderEngine& renderer;
    const PaperRenderer::Camera& camera;
    const HDRBuffer& hdrBuffer;

public:
    BufferCopyPass(PaperRenderer::RenderEngine &renderer, const PaperRenderer::Camera &camera, const HDRBuffer &hdrBuffer);
    ~BufferCopyPass();

    //to render function
    void render(const PaperRenderer::SynchronizationInfo &syncInfo, bool fromRaster);
};
