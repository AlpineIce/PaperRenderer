#include "RenderPasses.h"

//----------RENDER TARGETS----------//

HDRBuffer getHDRBuffer(PaperRenderer::RenderEngine &renderer, VkImageLayout startingLayout)
{
    //HDR buffer format
    const VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;

    //get new extent
    const VkExtent2D extent = renderer.getSwapchain().getExtent();

    //HDR buffer for rendering
    const PaperRenderer::ImageInfo hdrBufferInfo = {
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = { extent.width, extent.height, 1 },
        .maxMipLevels = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, //no MSAA used
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .imageAspect = VK_IMAGE_ASPECT_COLOR_BIT,
        .desiredLayout = startingLayout
    };

    std::unique_ptr<PaperRenderer::Image> hdrBuffer = std::make_unique<PaperRenderer::Image>(renderer, hdrBufferInfo);

    //HDR buffer view
    VkImageView view = hdrBuffer->getNewImageView(VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, format);

    //HDR buffer sampler (I've profiled this before and its much more efficient to use a render pass than a computer shader and blit)
    VkSampler sampler = hdrBuffer->getNewSampler();

    return { std::move(hdrBuffer), format, view, sampler };
}

DepthBuffer getDepthBuffer(PaperRenderer::RenderEngine &renderer)
{
    //depth buffer format
    VkFormat depthBufferFormat = VK_FORMAT_UNDEFINED;
    VkFormatProperties properties;

    //find format (prefer higher bits)
    vkGetPhysicalDeviceFormatProperties(renderer.getDevice().getGPU(), VK_FORMAT_D32_SFLOAT, &properties);
    if(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
    {
        depthBufferFormat = VK_FORMAT_D32_SFLOAT;
    }
    else
    {
        vkGetPhysicalDeviceFormatProperties(renderer.getDevice().getGPU(), VK_FORMAT_D24_UNORM_S8_UINT, &properties);
        if(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
        {
            depthBufferFormat = VK_FORMAT_D24_UNORM_S8_UINT;
        }
        else 
        {
            vkGetPhysicalDeviceFormatProperties(renderer.getDevice().getGPU(), VK_FORMAT_D16_UNORM_S8_UINT, &properties);
            if(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            {
                depthBufferFormat = VK_FORMAT_D16_UNORM_S8_UINT;
            }
        }
    }

    //get new extent
    const VkExtent2D extent = renderer.getSwapchain().getExtent();

    //depth buffer for rendering
    const PaperRenderer::ImageInfo depthBufferInfo = {
        .imageType = VK_IMAGE_TYPE_2D,
        .format = depthBufferFormat,
        .extent = { extent.width, extent.height, 1 },
        .maxMipLevels = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, //no MSAA used
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .imageAspect = VK_IMAGE_ASPECT_DEPTH_BIT,
        .desiredLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL
    };

    std::unique_ptr<PaperRenderer::Image> depthBuffer = std::make_unique<PaperRenderer::Image>(renderer, depthBufferInfo);

    //depth buffer view
    VkImageView view = depthBuffer->getNewImageView(VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_VIEW_TYPE_2D, depthBufferFormat);

    return { std::move(depthBuffer), depthBufferFormat, view };
}

//----------RAY TRACING----------//

ExampleRayTracing::ExampleRayTracing(PaperRenderer::RenderEngine& renderer, const PaperRenderer::Camera& camera, const HDRBuffer& hdrBuffer, const PaperRenderer::Buffer& lightBuffer, const PaperRenderer::Buffer& lightInfoUBO)
    :tlas(renderer),
    rgenShader(renderer, readFromFile("resources/shaders/raytrace_rgen.spv")),
    rmissShader(renderer, readFromFile("resources/shaders/raytrace_rmiss.spv")),
    rshadowShader(renderer, readFromFile("resources/shaders/raytraceShadow_rmiss.spv")),

    generalShaders({
        //rgen
        { VK_SHADER_STAGE_RAYGEN_BIT_KHR, &rgenShader },
        //miss
        { VK_SHADER_STAGE_MISS_BIT_KHR, &rmissShader },
        { VK_SHADER_STAGE_MISS_BIT_KHR, &rshadowShader }
    }),
    rayRecursionDepth(renderer.getDevice().getRTproperties().maxRayRecursionDepth),
    rtInfoUBO(renderer, {
        .size = sizeof(RayTraceInfo) * 2,
        .usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR,
        .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
    }),
    rtRenderPass(
        renderer,
        tlas,
        generalShaders[0],
        { generalShaders[1], generalShaders[2] },
        {},
        {
            { 0, {
                { //AS pointer
                    .binding = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
                },
                { //point lights (pbr.glsl)
                    .binding = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
                },
                { //light info (pbr.glsl)
                    .binding = 2,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
                },
                { //hdr buffer
                    .binding = 3,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR
                },
                { //RT input data
                    .binding = 4,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_INTERSECTION_BIT_KHR
                },
                { //instance descriptions
                    .binding = 5,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_INTERSECTION_BIT_KHR
                },
                { //material descriptions
                    .binding = 6,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR
                }
            }}
        },
        {
            .maxRecursionDepth = rayRecursionDepth
        },
        {}
    ),
    renderer(renderer),
    camera(camera),
    hdrBuffer(hdrBuffer),
    lightBuffer(lightBuffer),
    lightInfoUBO(lightInfoUBO)
{
}

ExampleRayTracing::~ExampleRayTracing()
{
}

const PaperRenderer::Queue& ExampleRayTracing::rayTraceRender(const PaperRenderer::SynchronizationInfo &syncInfo, const PaperRenderer::Buffer& materialDefinitionsBuffer)
{
    //pre-render barriers
    std::vector<VkImageMemoryBarrier2> preRenderImageBarriers = {
        { //HDR buffer undefined -> general layout; required for correct shader access
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
            .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = hdrBuffer.image->getImage(),
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        }
    };
    
    const VkDependencyInfo preRenderDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = NULL,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = NULL,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = NULL,
        .imageMemoryBarrierCount = (uint32_t)preRenderImageBarriers.size(),
        .pImageMemoryBarriers = preRenderImageBarriers.data()
    };

    //descriptor writes
    const PaperRenderer::DescriptorWrites descriptorWrites = {
        .bufferWrites = {
            {
                .infos = {{
                    .buffer = lightBuffer.getBuffer(),
                    .offset = 0,
                    .range = VK_WHOLE_SIZE
                }},
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .binding = 1
            },
            {
                .infos = {{
                    .buffer = lightInfoUBO.getBuffer(),
                    .offset = 0,
                    .range = VK_WHOLE_SIZE
                }},
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .binding = 2
            },
            {
                .infos = {{
                    .buffer = rtInfoUBO.getBuffer(),
                    .offset = sizeof(RayTraceInfo) * renderer.getBufferIndex(),
                    .range = sizeof(RayTraceInfo)
                }},
                .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .binding = 4
            },
            {
                .infos = {{
                    .buffer = rtRenderPass.getTLAS().getInstanceDescriptionsRange() ? rtRenderPass.getTLAS().getInstancesBuffer().getBuffer() : VK_NULL_HANDLE,
                    .offset = rtRenderPass.getTLAS().getInstanceDescriptionsOffset(),
                    .range = rtRenderPass.getTLAS().getInstanceDescriptionsRange()
                }},
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .binding = 5
            },
            {
                .infos = {{
                    .buffer = materialDefinitionsBuffer.getBuffer(),
                    .offset = 0,
                    .range = VK_WHOLE_SIZE
                }},
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .binding = 6
            }
        },
        .imageWrites = {
            {
                .infos = {{
                    .sampler = hdrBuffer.sampler,
                    .imageView = hdrBuffer.view,
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL
                }},
                .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .binding = 3
            }
        },
        .bufferViewWrites = {},
        .accelerationStructureWrites = {
            {
                .accelerationStructures = { &rtRenderPass.getTLAS() },
                .binding = 0
            },
        }
    };

    //rt render info
    const PaperRenderer::RayTraceRenderInfo rtRenderInfo = {
        .image = *hdrBuffer.image,
        .camera = camera,
        .preRenderBarriers = &preRenderDependency,
        .postRenderBarriers = NULL, //no post render barrier
        .rtDescriptorWrites = descriptorWrites
    };
    
    return rtRenderPass.render(rtRenderInfo, syncInfo);
}

void ExampleRayTracing::updateUBO()
{
    //update RT UBO
    RayTraceInfo rtInfo = {
        .projection = camera.getProjection(),
        .view = camera.getViewMatrix(),
        .modelDataReference = renderer.getModelDataBuffer().getBufferDeviceAddress(),
        .frameNumber = renderer.getFramesRenderedCount(),
        .recursionDepth = rayRecursionDepth,
        .aoSamples = 1,
        .aoRadius = 2.0f,
        .shadowSamples = 1,
        .reflectionSamples = 1
    };
    
    const PaperRenderer::BufferWrite write = {
        .offset = sizeof(RayTraceInfo) * renderer.getBufferIndex(),
        .size = sizeof(RayTraceInfo),
        .readData = &rtInfo
    };
    rtInfoUBO.writeToBuffer({ write });
}

//----------RASTER----------//

ExampleRaster::ExampleRaster(PaperRenderer::RenderEngine &renderer, const PaperRenderer::Camera& camera, const HDRBuffer& hdrBuffer, const DepthBuffer& depthBuffer, const PaperRenderer::Buffer& lightBuffer, const PaperRenderer::Buffer& lightInfoUBO)
    :baseMaterial(renderer, {
            .shaderInfo = {
                {
                    .stage = VK_SHADER_STAGE_VERTEX_BIT,
                    .data = readFromFile("resources/shaders/Default_vert.spv")
                },
                {
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .data = readFromFile("resources/shaders/Default_frag.spv")
                }
            },
            .descriptorSets = {
                { 0, {
                    {
                        .binding = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        .descriptorCount = 1,
                        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
                    },
                    {
                        .binding = 2,
                        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        .descriptorCount = 1,
                        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
                    }
                }},
                { 2, {
                    {
                        .binding = 0,
                        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        .descriptorCount = 1,
                        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
                    }
                }}
            },
            .pcRanges = {}, //no push constants
            .properties = {
                .vertexAttributes = {
                    {
                        .location = 0,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(Vertex, position)
                    },
                    {
                        .location = 1,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32B32_SFLOAT,
                        .offset = offsetof(Vertex, normal)
                    },
                    {
                        .location = 2,
                        .binding = 0,
                        .format = VK_FORMAT_R32G32_SFLOAT,
                        .offset = offsetof(Vertex, uv)
                    }
                },
                .vertexDescriptions = {
                    {
                        .binding = 0,
                        .stride = sizeof(Vertex),
                        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
                    }
                },
                .colorAttachments = {
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
                },
                .colorAttachmentFormats = {
                    { hdrBuffer.format }
                },
                .depthAttachmentFormat = depthBuffer.format,
                .rasterInfo = {
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
                }
            },
            .drawDescriptorIndex = 1
        },
        lightBuffer,
        lightInfoUBO
    ),
    defaultMaterialInstance(renderer, baseMaterial, {
        .baseColor = glm::vec4(1.0f, 0.5f, 1.0f, 1.0f),
        .emission = glm::vec4(0.0f),
        .roughness = 0.5f,
        .metallic = 0.0f
    }),
    renderPass(renderer, defaultMaterialInstance),
    renderer(renderer),
    camera(camera),
    hdrBuffer(hdrBuffer),
    depthBuffer(depthBuffer),
    lightBuffer(lightBuffer),
    lightInfoUBO(lightInfoUBO)
{
}

ExampleRaster::~ExampleRaster()
{
}

const PaperRenderer::Queue& ExampleRaster::rasterRender(PaperRenderer::SynchronizationInfo syncInfo)
{
    //transfer instance data
    renderPass.queueInstanceTransfers();
    PaperRenderer::SynchronizationInfo transferSyncInfo = {
        .queueType = PaperRenderer::QueueType::TRANSFER
    };
    renderer.getStagingBuffer().submitQueuedTransfers(transferSyncInfo);

    //pre-render barriers
    std::vector<VkImageMemoryBarrier2> preRenderImageBarriers;
    preRenderImageBarriers.push_back({ //HDR buffer undefined -> general layout; required for correct shader access
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = NULL,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = hdrBuffer.image->getImage(),
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    });
    
    const VkDependencyInfo preRenderDependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = NULL,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = NULL,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = NULL,
        .imageMemoryBarrierCount = (uint32_t)preRenderImageBarriers.size(),
        .pImageMemoryBarriers = preRenderImageBarriers.data()
    };

    //color attachment
    std::vector<VkRenderingAttachmentInfo> colorAttachments;
    colorAttachments.push_back({
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = NULL,
        .imageView = hdrBuffer.view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { 0.1f, 0.1f, 0.1f, 1.0f }
    });

    //depth attachment
    const VkRenderingAttachmentInfo depthAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = NULL,
        .imageView = depthBuffer.view,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .resolveImageView = VK_NULL_HANDLE,
        .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { 1.0f, 0 }
    };

    //viewport, scissors, render area
    const VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)(renderer.getSwapchain().getExtent().width),
        .height = (float)(renderer.getSwapchain().getExtent().height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };

    const VkRect2D scissors = {
        .offset = { 0, 0 },
        .extent = renderer.getSwapchain().getExtent()
    };

    const VkRect2D renderArea = {
        .offset = { 0, 0 },
        .extent = renderer.getSwapchain().getExtent()
    };

    //render pass info
    const PaperRenderer::RenderPassInfo renderPassInfo = {
        .camera = camera,
        .colorAttachments = colorAttachments,
        .depthAttachment = &depthAttachment,
        .stencilAttachment = NULL,
        .viewports = { viewport },
        .scissors = { scissors },
        .renderArea = renderArea,
        .sampleCount = VK_SAMPLE_COUNT_1_BIT,
        .preRenderBarriers = &preRenderDependency,
        .postRenderBarriers = NULL,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .sortMode = PaperRenderer::RenderPassSortMode::BACK_FIRST //back first for accurate translucency
    };

    //render
    syncInfo.timelineWaitPairs.push_back(renderer.getStagingBuffer().getTransferSemaphore());
    return renderer.getDevice().getCommands().submitToQueue(syncInfo, renderPass.render(renderPassInfo));
}

//----------BUFFER COPY PASS----------//

BufferCopyPass::BufferCopyPass(PaperRenderer::RenderEngine &renderer, const PaperRenderer::Camera &camera, const HDRBuffer &hdrBuffer)
    :material(renderer, hdrBuffer),
    renderer(renderer),
    camera(camera),
    hdrBuffer(hdrBuffer)
{
}

BufferCopyPass::~BufferCopyPass()
{
}

const PaperRenderer::Queue& BufferCopyPass::render(const PaperRenderer::SynchronizationInfo &syncInfo, bool fromRaster)
{
    //----------PRE-RENDER BARRIER----------//

    //swapchain transition from undefined to color attachment
    std::vector<VkImageMemoryBarrier2> preRenderImageBarriers;
    preRenderImageBarriers.push_back({
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = NULL,
        .srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = renderer.getSwapchain().getCurrentImage(),
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        }
    });
    preRenderImageBarriers.push_back({
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .oldLayout = fromRaster ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL,
            .newLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = hdrBuffer.image->getImage(),
            .subresourceRange =  {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            }
        });

    VkDependencyInfo preRenderBarriers = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = NULL,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = NULL,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = NULL,
        .imageMemoryBarrierCount = (uint32_t)preRenderImageBarriers.size(),
        .pImageMemoryBarriers = preRenderImageBarriers.data()
    };

    //----------POST-RENDER BARRIER----------//

    //swapchain transition from color attachment to presentation optimal
    std::vector<VkImageMemoryBarrier2> postRenderImageBarriers;
    postRenderImageBarriers.push_back({
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .pNext = NULL,
        .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = renderer.getSwapchain().getCurrentImage(),
        .subresourceRange =  {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        }
    });

    VkDependencyInfo postRenderBarriers = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = NULL,
        .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = NULL,
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = NULL,
        .imageMemoryBarrierCount = (uint32_t)postRenderImageBarriers.size(),
        .pImageMemoryBarriers = postRenderImageBarriers.data()
    };

    //----------ATTACHMENTS----------//
    
    //color attachment
    std::vector<VkRenderingAttachmentInfo> colorAttachments;
    colorAttachments.push_back({    //output
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .pNext = NULL,
        .imageView = renderer.getSwapchain().getCurrentImageView(),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { 0.0f, 0.0f, 0.0f, 0.0f } 
    });


    //----------VIEWPORT SCISSORS AND RENDER AREA----------//

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)(renderer.getSwapchain().getExtent().width);
    viewport.height = (float)(renderer.getSwapchain().getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissors = {};
    scissors.extent = renderer.getSwapchain().getExtent();
    scissors.offset = { 0, 0 };

    VkRect2D renderArea = {};
    renderArea.offset = {0, 0};
    renderArea.extent = renderer.getSwapchain().getExtent();

    //----------RENDER----------//

    VkCommandBuffer cmdBuffer = renderer.getDevice().getCommands().getCommandBuffer(PaperRenderer::QueueType::GRAPHICS);

    VkCommandBufferBeginInfo cmdBufferBeginInfo = {};
    cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBufferBeginInfo.pNext = NULL;
    cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    cmdBufferBeginInfo.pInheritanceInfo = NULL;
    
    vkBeginCommandBuffer(cmdBuffer, &cmdBufferBeginInfo);

    vkCmdPipelineBarrier2(cmdBuffer, &preRenderBarriers);

    //rendering
    VkRenderingInfo renderInfo = {};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    renderInfo.pNext = NULL;
    renderInfo.flags = 0;
    renderInfo.renderArea = renderArea;
    renderInfo.layerCount = 1;
    renderInfo.viewMask = 0;
    renderInfo.colorAttachmentCount = colorAttachments.size();
    renderInfo.pColorAttachments = colorAttachments.data();
    renderInfo.pDepthAttachment = NULL;
    renderInfo.pStencilAttachment = NULL;

    vkCmdBeginRendering(cmdBuffer, &renderInfo);

    //scissors (plural) and viewports
    vkCmdSetViewportWithCount(cmdBuffer, 1, &viewport);
    vkCmdSetScissorWithCount(cmdBuffer, 1, &scissors);

    //MSAA samples
    vkCmdSetRasterizationSamplesEXT(cmdBuffer, VK_SAMPLE_COUNT_1_BIT);

    //compare op
    vkCmdSetDepthCompareOp(cmdBuffer, VK_COMPARE_OP_NEVER);

    //bind (camera isn't actually used in this implementation, but thats fine)
    std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites> descriptorWrites;
    material.bind(cmdBuffer, camera, descriptorWrites);

    //draw command
    vkCmdDraw(cmdBuffer, 3, 1, 0, 0);

    //end rendering
    vkCmdEndRendering(cmdBuffer);

    vkCmdPipelineBarrier2(cmdBuffer, &postRenderBarriers);

    vkEndCommandBuffer(cmdBuffer);

    renderer.getDevice().getCommands().unlockCommandBuffer(cmdBuffer);

    return renderer.getDevice().getCommands().submitToQueue(syncInfo, { cmdBuffer });
}

BufferCopyPass::BufferCopyMaterial::BufferCopyMaterial(PaperRenderer::RenderEngine &renderer, const HDRBuffer &hdrBuffer)
    :PaperRenderer::Material(
        renderer, 
        {
            .shaderInfo = {
                {
                    .stage = VK_SHADER_STAGE_VERTEX_BIT,
                    .data = readFromFile("resources/shaders/Quad.spv")
                },
                {
                    .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                    .data = readFromFile("resources/shaders/BufferCopy.spv")
                }
            },
            .descriptorSets = {
                { 0, {
                    { //UBO
                        .binding = 0,
                        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        .descriptorCount = 1,
                        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
                    },
                    { //HDR buffer
                        .binding = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .descriptorCount = 1,
                        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
                    }
                }}
            },
            .pcRanges = {},
            .properties = {
                .vertexAttributes = {}, //no vertex data
                .vertexDescriptions = {}, //no vertex data
                .colorAttachments = {
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
                },
                .colorAttachmentFormats = { 
                    { renderer.getSwapchain().getFormat() }
                },
                .rasterInfo = {
                    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                    .pNext = NULL,
                    .flags = 0,
                    .depthClampEnable = VK_FALSE,
                    .rasterizerDiscardEnable = VK_FALSE,
                    .polygonMode = VK_POLYGON_MODE_FILL,
                    .cullMode = VK_CULL_MODE_NONE,
                    .frontFace = VK_FRONT_FACE_CLOCKWISE,
                    .depthBiasEnable = VK_FALSE,
                    .depthBiasConstantFactor = 0.0f,
                    .depthBiasClamp = 0.0f,
                    .depthBiasSlopeFactor = 0.0f,
                    .lineWidth = 1.0f
                }
            }
        },
    false
    ),
    uniformBuffer(renderer, {
        .size = sizeof(UBOInputData),
        .usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR,
        .allocationFlags=  VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
    }),
    hdrBuffer(hdrBuffer)
{
}

BufferCopyPass::BufferCopyMaterial::~BufferCopyMaterial()
{
}

void BufferCopyPass::BufferCopyMaterial::bind(VkCommandBuffer cmdBuffer, const PaperRenderer::Camera &camera, std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites> &descriptorWrites)
{
    //----------UBO----------//

    UBOInputData uboInputData = {};
    uboInputData.exposure = 1.0f;
    uboInputData.WBtemp = 0.0f;
    uboInputData.WBtint = 0.0f;
    uboInputData.contrast = 1.0f;
    uboInputData.colorFilter = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    uboInputData.brightness = 0.0f;
    uboInputData.saturation = 1.0f;
    uboInputData.gammaCorrection = renderer.getSwapchain().getIsUsingHDR() ? 1.0f : 2.2f;

    PaperRenderer::BufferWrite uboWrite;
    uboWrite.readData = &uboInputData;
    uboWrite.offset = 0;
    uboWrite.size = sizeof(UBOInputData);
    uniformBuffer.writeToBuffer({ uboWrite });

    //uniform buffer
    VkDescriptorBufferInfo uniformInfo = {};
    uniformInfo.buffer = uniformBuffer.getBuffer();
    uniformInfo.offset = 0;
    uniformInfo.range = sizeof(UBOInputData);

    PaperRenderer::BuffersDescriptorWrites uniformWrite;
    uniformWrite.binding = 0;
    uniformWrite.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uniformWrite.infos = { uniformInfo };

    //hdr buffer
    VkDescriptorImageInfo hdrBufferInfo = {};
    hdrBufferInfo.imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
    hdrBufferInfo.imageView = hdrBuffer.view;
    hdrBufferInfo.sampler = hdrBuffer.sampler;

    PaperRenderer::ImagesDescriptorWrites hdrBufferWrite;
    hdrBufferWrite.binding = 1;
    hdrBufferWrite.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    hdrBufferWrite.infos = { hdrBufferInfo };

    //descriptor writes
    descriptorWrites[0].bufferWrites.push_back(uniformWrite);
    descriptorWrites[0].imageWrites.push_back(hdrBufferWrite);

    //next level bind
    PaperRenderer::Material::bind(cmdBuffer, camera, descriptorWrites);
}
