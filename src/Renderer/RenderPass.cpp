#include "RenderPass.h"
#include "glm/gtc/matrix_transform.hpp"

namespace Renderer
{
    RenderPass::RenderPass(Swapchain* swapchain, Device* device, CmdBufferAllocator* commands, DescriptorAllocator* descriptors, PipelineBuilder* pipelineBuilder)
        :swapchainPtr(swapchain),
        devicePtr(device),
        commandsPtr(commands),
        descriptorsPtr(descriptors),
        pipelineBuilderPtr(pipelineBuilder),
        currentImage(0)
    {
        //synchronization objects
        cullingSemaphores.resize(commandsPtr->getFrameCount());
        imageSemaphores.resize(commandsPtr->getFrameCount());
        cullingFences.resize(commandsPtr->getFrameCount());
        renderFences.resize(commandsPtr->getFrameCount());

        //global uniforms
        globalBufferDatas.resize(CmdBufferAllocator::getFrameCount());
        for(uint32_t i = 0; i < CmdBufferAllocator::getFrameCount(); i++)
        {
            globalInfoBuffers.push_back(std::make_shared<UniformBuffer>(devicePtr, commandsPtr, (uint32_t)sizeof(CameraData)));
            pointLightsBuffers.push_back(std::make_shared<StorageBuffer>(devicePtr, commandsPtr, (uint32_t)sizeof(PointLight) * MAX_POINT_LIGHTS));
            lightingInfoBuffers.push_back(std::make_shared<UniformBuffer>(devicePtr, commandsPtr, (uint32_t)sizeof(ShaderLightingInformation)));
        }

        //----------PREPROCESS PIPELINE----------//

        std::vector<ShaderPair> shaderPairs = {{
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .directory = "resources/compute/IndirectDrawBuild.spv"
        }};
        std::vector<Renderer::DescriptorSet> descriptorSets;

        DescriptorSet set0;
        VkDescriptorSetLayoutBinding drawCountsDescriptor = {};
        drawCountsDescriptor.binding = 0;
        drawCountsDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        drawCountsDescriptor.descriptorCount = 1;
        drawCountsDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        set0.descriptorBindings.push_back(drawCountsDescriptor);
        descriptorSets.push_back(set0);

        DescriptorSet set1;
        VkDescriptorSetLayoutBinding inputDataDescriptor = {};
        inputDataDescriptor.binding = 0;
        inputDataDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        inputDataDescriptor.descriptorCount = 1;
        inputDataDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        set1.descriptorBindings.push_back(inputDataDescriptor);

        VkDescriptorSetLayoutBinding inputObjectsDescriptor = {};
        inputObjectsDescriptor.binding = 1;
        inputObjectsDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        inputObjectsDescriptor.descriptorCount = 1;
        inputObjectsDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        set1.descriptorBindings.push_back(inputObjectsDescriptor);
        descriptorSets.push_back(set1);

        PipelineBuildInfo pipelineInfo;
        pipelineInfo.descriptors = descriptorSets;
        pipelineInfo.useGlobalDescriptor = false;
        pipelineInfo.shaderInfo = shaderPairs;
        
        meshPreprocessPipeline = pipelineBuilderPtr->buildComputePipeline(pipelineInfo);
    }

    RenderPass::~RenderPass()
    {
        renderFences.at(currentImage).clear();
        cullingFences.at(currentImage).clear();

        for(VkSemaphore semaphore: imageSemaphores)
        {
            vkDestroySemaphore(devicePtr->getDevice(), semaphore, nullptr);
        }
        for(VkSemaphore semaphore: cullingSemaphores)
        {
            vkDestroySemaphore(devicePtr->getDevice(), semaphore, nullptr);
        }
    }

    void RenderPass::checkSwapchain(VkResult imageResult)
    {
        if(imageResult == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateFlag = true;
        }
    }
    
    glm::vec4 RenderPass::normalizePlane(glm::vec4 plane)
    {
        return plane / glm::length(glm::vec3(plane));
    }

    ImageAttachment RenderPass::createImageAttachment(VkFormat imageFormat)
    {
        VkImage returnImage;
        VkImageView returnView;
        VmaAllocation returnAllocation;

        VkExtent3D swapchainExtent = {
            .width = swapchainPtr->getExtent().width,
            .height = swapchainPtr->getExtent().height,
            .depth = 1
        };
        
        VkImageCreateInfo imageInfo = {};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.pNext = NULL;
        imageInfo.flags = 0;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = imageFormat;
        imageInfo.extent = swapchainExtent;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; //no MSAA for now
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; //likely wont be transfered or anything
        imageInfo.queueFamilyIndexCount = 0;
        imageInfo.pQueueFamilyIndices = NULL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        VmaAllocationCreateInfo allocCreateInfo = {};
        allocCreateInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VmaAllocationInfo allocInfo;
        VkResult allocResult = vmaCreateImage(devicePtr->getAllocator(), &imageInfo, &allocCreateInfo, &returnImage, &returnAllocation, &allocInfo);

        //create the image view
        VkImageSubresourceRange subresourceRange;
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = 1;

        VkImageViewCreateInfo viewInfo = {};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.pNext = NULL;
        viewInfo.flags = 0;
        viewInfo.image = returnImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = imageFormat;
        viewInfo.subresourceRange = subresourceRange;

        if (vkCreateImageView(devicePtr->getDevice(), &viewInfo, nullptr, &returnView) != VK_SUCCESS) 
        {
            throw std::runtime_error("Failed to create a render target image view");
        }

        return { returnImage, returnView, returnAllocation };
    }

    VkCommandBuffer RenderPass::preProcessing(const LightingInformation& lightingInfo)
    {
        //end last frame
        for(auto result = renderFences.at(currentImage).begin(); result != renderFences.at(currentImage).end(); result++)
        {
            result->get()->waitForFence();
        }
        for(auto result = cullingFences.at(currentImage).begin(); result != cullingFences.at(currentImage).end(); result++)
        {
            result->get()->waitForFence();
        }

        vkDestroySemaphore(devicePtr->getDevice(), imageSemaphores.at(currentImage), nullptr);
        imageSemaphores.at(currentImage) = commandsPtr->getSemaphore();

        renderFences.at(currentImage).clear();
        cullingFences.at(currentImage).clear();
        
        //get available image
        checkSwapchain(vkAcquireNextImageKHR(devicePtr->getDevice(),
            *(swapchainPtr->getSwapchainPtr()),
            UINT32_MAX,
            imageSemaphores.at(currentImage),
            VK_NULL_HANDLE, &currentImage));
        
        descriptorsPtr->refreshPools(currentImage);

        //update global uniform
        globalBufferDatas.at(currentImage).view = cameraPtr->getViewMatrix();
        globalBufferDatas.at(currentImage).projection = cameraPtr->getProjection();
        globalInfoBuffers.at(currentImage)->updateUniformBuffer(&globalBufferDatas.at(currentImage), sizeof(CameraData));

        //fill in point light buffer
        std::vector<PointLight> pointLights;
        lightBufferCopySemaphores.resize(0);
        for(auto light = lightingInfo.pointLights.begin(); light != lightingInfo.pointLights.end(); light++)
        {
            pointLights.push_back(**light);
        }

        StagingBuffer pointLightInfoStaging(devicePtr, commandsPtr, sizeof(PointLight) * MAX_POINT_LIGHTS);
        pointLightInfoStaging.mapData(pointLights.data(), 0, sizeof(PointLight) * pointLights.size());
        QueueReturn lightBufferCopyFence = pointLightsBuffers.at(currentImage)->copyFromBuffer(pointLightInfoStaging, false);

        lightBufferCopySemaphores = lightBufferCopyFence.getSemaphores();
        renderFences.at(currentImage).push_back(std::make_shared<QueueReturn>(std::move(lightBufferCopyFence)));

        //lighting information buffer
        ShaderLightingInformation shaderLightingInfo = {};
        if(lightingInfo.directLight) shaderLightingInfo.directLight = *lightingInfo.directLight;
        if(lightingInfo.ambientLight) shaderLightingInfo.ambientLight = *lightingInfo.ambientLight;
        shaderLightingInfo.pointLightCount = pointLights.size();
        shaderLightingInfo.camPos = cameraPtr->getTranslation().position;
        lightingInfoBuffers.at(currentImage)->updateUniformBuffer(&shaderLightingInfo, sizeof(ShaderLightingInformation));

        globalUniformData.globalUBO = globalInfoBuffers.at(currentImage).get();
        globalUniformData.pointLightsBuffer = pointLightsBuffers.at(currentImage).get();
        globalUniformData.lightingInfoBuffer = lightingInfoBuffers.at(currentImage).get();
        globalUniformData.maxPointLights = MAX_POINT_LIGHTS;

        //perform draw call culling
        VkCommandBufferBeginInfo commandInfo;
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandInfo.pNext = NULL;
        commandInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        commandInfo.pInheritanceInfo = NULL;

        VkCommandBuffer cullingCmdBuffer = commandsPtr->getCommandBuffer(CmdPoolType::COMPUTE);
        vkBeginCommandBuffer(cullingCmdBuffer, &commandInfo);

        //bind culling pipeline
        vkCmdBindPipeline(cullingCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, meshPreprocessPipeline->getPipeline());

        return cullingCmdBuffer;
    }

    VkCommandBuffer RenderPass::beginRendering()
    {
        //command buffer
        VkCommandBufferBeginInfo commandInfo;
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandInfo.pNext = NULL;
        commandInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        commandInfo.pInheritanceInfo = NULL;

        //begin recording
        VkCommandBuffer graphicsCmdBuffer = commandsPtr->getCommandBuffer(CmdPoolType::GRAPHICS);
        vkBeginCommandBuffer(graphicsCmdBuffer, &commandInfo);

        //----------RENDER TARGETS----------//

        VkClearValue clearValue = {};
        clearValue.color = {0.0f, 0.0f, 0.0, 0.0f};

        VkClearValue depthClear = {};
        depthClear.depthStencil = {1.0f, 0};

        VkRect2D renderArea = {};
        renderArea.offset = {0, 0};
        renderArea.extent = swapchainPtr->getExtent();

        std::vector<VkRenderingAttachmentInfo> renderingAttachments;

        VkRenderingAttachmentInfo colorAttachment = {};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.pNext = NULL;
        colorAttachment.imageView = swapchainPtr->getImageViews()->at(currentImage);
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue = clearValue;
        renderingAttachments.push_back(colorAttachment);

        //depth buffer attachment
        VkRenderingAttachmentInfo depthAttachment = {};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.pNext = NULL;
        depthAttachment.imageView = swapchainPtr->getDepthView();
        depthAttachment.imageLayout = swapchainPtr->getDepthLayout();
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.clearValue = depthClear;
        
        VkRenderingAttachmentInfo stencilAttachment = {};
        stencilAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        stencilAttachment.pNext = NULL;

        VkRenderingInfoKHR renderInfo = {};
        renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
        renderInfo.pNext = NULL;
        renderInfo.flags = 0;
        renderInfo.renderArea = renderArea;
        renderInfo.layerCount = 1;
        renderInfo.viewMask = 0;
        renderInfo.colorAttachmentCount = renderingAttachments.size();
        renderInfo.pColorAttachments = renderingAttachments.data();
        renderInfo.pDepthAttachment = &depthAttachment;
        renderInfo.pStencilAttachment = &stencilAttachment;
        
        VkImageMemoryBarrier imageBarrier = {};
        imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageBarrier.pNext = NULL;
        imageBarrier.srcAccessMask = VK_ACCESS_NONE;
        imageBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.image = swapchainPtr->getImages()->at(currentImage);
        imageBarrier.subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        vkCmdPipelineBarrier(
            graphicsCmdBuffer, 
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            0,
            NULL,
            0,
            NULL,
            1,
            &imageBarrier);

        vkCmdBeginRendering(graphicsCmdBuffer, &renderInfo);

        //dynamic viewport and scissor specified in pipelines
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)(swapchainPtr->getExtent().width);
        viewport.height = (float)(swapchainPtr->getExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vkCmdSetViewportWithCount(graphicsCmdBuffer, 1, &viewport);
        vkCmdSetScissorWithCount(graphicsCmdBuffer, 1, &renderArea);

        return graphicsCmdBuffer;
    }

    CullingFrustum RenderPass::createCullingFrustum()
    {
        glm::mat4 projectionT = transpose(cameraPtr->getProjection());
        
		glm::vec4 frustumX = normalizePlane(projectionT[3] + projectionT[0]);
		glm::vec4 frustumY = normalizePlane(projectionT[3] + projectionT[1]);

        CullingFrustum frustum;
        frustum.frustum[0] = frustumX.x;
		frustum.frustum[1] = frustumX.z;
		frustum.frustum[2] = frustumY.y;
		frustum.frustum[3] = frustumY.z;
        frustum.zPlanes = glm::vec2(cameraPtr->getClipNear(), cameraPtr->getClipFar());
        
        return frustum;
    }

    void RenderPass::drawCallCull(const VkCommandBuffer &cmdBuffer, const CullingFrustum& frustumData, IndirectDrawBuffer *drawBuffer)
    {
        std::vector<QueueReturn> pushFences = std::move(drawBuffer->performCulling(cmdBuffer, meshPreprocessPipeline.get(), frustumData, cameraPtr->getProjection(), cameraPtr->getViewMatrix(), currentImage));
        for(int i = 0; i < pushFences.size(); i++)
        {
            cullingFences.at(currentImage).push_back(std::make_shared<QueueReturn>(std::move(pushFences[i])));
        }
    }

    void RenderPass::submitCulling(const VkCommandBuffer& cmdBuffer)
    {
        vkEndCommandBuffer(cmdBuffer);

        std::vector<VkSemaphore> waitSemaphores;
        std::vector<VkPipelineStageFlags> pipelineStages;
        if(cullingFences.size())
        {
            for(auto fence = cullingFences.at(currentImage).begin(); fence != cullingFences.at(currentImage).end(); fence++)
            {
                for(VkSemaphore& semaphore : fence->get()->getSemaphores())
                {
                    waitSemaphores.push_back(semaphore);
                    pipelineStages.push_back(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT); //buffers need to finish transfering before compute shader can run
                }
            }
        }
        
        cullingSemaphores.at(currentImage) = commandsPtr->getSemaphore();

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = NULL;
        submitInfo.waitSemaphoreCount = waitSemaphores.size();
        submitInfo.pWaitSemaphores = waitSemaphores.data();
        submitInfo.pWaitDstStageMask = pipelineStages.data();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmdBuffer;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &cullingSemaphores.at(currentImage);

        cullingFences.at(currentImage).push_back(std::make_shared<QueueReturn>(std::move(commandsPtr->submitQueue(submitInfo, COMPUTE, false))));
    }

    void RenderPass::composeAttachments(const VkCommandBuffer &cmdBuffer)
    {

    }

    void RenderPass::drawIndexedIndirect(const VkCommandBuffer& cmdBuffer, IndirectDrawBuffer* drawBuffer)
    {
        drawBuffer->draw(cmdBuffer, currentImage);
    }

    void RenderPass::bindMaterial(Material const* material, const VkCommandBuffer &cmdBuffer)
    {
        material->bindPipeline(cmdBuffer, globalUniformData, currentImage);
    }

    void RenderPass::bindMaterialInstance(MaterialInstance const* materialInstance, const VkCommandBuffer& cmdBuffer)
    {
        materialInstance->bind(cmdBuffer, currentImage);
    }

    void RenderPass::incrementFrameCounter(const VkCommandBuffer& cmdBuffer)
    {
        //end render "pass"
        vkCmdEndRendering(cmdBuffer);

        VkImageMemoryBarrier imageBarrier = {};
        imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageBarrier.pNext = NULL;
        imageBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        imageBarrier.dstAccessMask = 0;
        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.image = swapchainPtr->getImages()->at(currentImage);
        imageBarrier.subresourceRange =  {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        vkCmdPipelineBarrier(
            cmdBuffer, 
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0,
            NULL,
            0,
            NULL,
            1,
            &imageBarrier);

        vkEndCommandBuffer(cmdBuffer);

        //submit rendering to GPU
        std::vector<VkSemaphore> waitSemaphores;
        std::vector<VkPipelineStageFlags> pipelineStages = {
            VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        };
        waitSemaphores.push_back(cullingSemaphores.at(currentImage));
        waitSemaphores.push_back(imageSemaphores.at(currentImage));

        for(VkSemaphore semaphore : lightBufferCopySemaphores)
        {
            pipelineStages.push_back(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT); //lighting information calculated in fragment shader
            waitSemaphores.push_back(semaphore);
        }
        
        VkSemaphore graphicsSemapore = commandsPtr->getSemaphore();
        VkSubmitInfo queueSubmitInfo = {};
        queueSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        queueSubmitInfo.pNext = NULL;
        queueSubmitInfo.waitSemaphoreCount = waitSemaphores.size();
        queueSubmitInfo.pWaitSemaphores = waitSemaphores.data();
        queueSubmitInfo.pWaitDstStageMask = pipelineStages.data();
        queueSubmitInfo.commandBufferCount = 1;
        queueSubmitInfo.pCommandBuffers = &cmdBuffer;
        queueSubmitInfo.signalSemaphoreCount = 1;
        queueSubmitInfo.pSignalSemaphores = &graphicsSemapore;

        QueueReturn graphicsResult = commandsPtr->submitQueue(queueSubmitInfo, CmdPoolType::GRAPHICS, true);
        renderFences.at(currentImage).push_back(std::make_shared<QueueReturn>(std::move(graphicsResult)));

        //submit rendered image to swapchain
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.pNext = NULL;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &graphicsSemapore;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchainPtr->getSwapchainPtr();
        presentInfo.pImageIndices = &currentImage;
        presentInfo.pResults = NULL;

        QueueReturn presentResult = commandsPtr->submitPresentQueue(presentInfo);
        renderFences.at(currentImage).push_back(std::make_shared<QueueReturn>(std::move(presentResult)));

        if (presentResult.getSubmitResult() == VK_ERROR_OUT_OF_DATE_KHR || presentResult.getSubmitResult() == VK_SUBOPTIMAL_KHR || recreateFlag) 
        {
            recreateFlag = false;
            swapchainPtr->recreate();
            cameraPtr->updateCameraProjection();
        }

        if(currentImage == 0)
        {
            currentImage = 1;
        }
        else
        {
            currentImage = 0;
        }
    }
}