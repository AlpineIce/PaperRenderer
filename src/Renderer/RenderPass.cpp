#include "RenderPass.h"
#include "glm/gtc/matrix_transform.hpp"

namespace Renderer
{
    RenderPass::RenderPass(Swapchain* swapchain, Device* device, CmdBufferAllocator* commands, DescriptorAllocator* descriptors)
        :swapchainPtr(swapchain),
        devicePtr(device),
        commandsPtr(commands),
        descriptorsPtr(descriptors),
        currentImage(0)
    {
        //synchronization objects
        imageSemaphores.resize(commandsPtr->getFrameCount());
        
        preRenderFences.resize(commandsPtr->getFrameCount());
        renderFences.resize(commandsPtr->getFrameCount());

        //global uniforms
        uniformDatas.resize(CmdBufferAllocator::getFrameCount());
        for(uint32_t i = 0; i < CmdBufferAllocator::getFrameCount(); i++)
        {
            globalUBOs.push_back(std::make_shared<UniformBuffer>(device, commands, (uint32_t)sizeof(GlobalDescriptor)));
        }
    }

    void RenderPass::checkSwapchain(VkResult imageResult)
    {
        if(imageResult == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateFlag = true;
        }
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

    RenderPass::~RenderPass()
    {
        renderFences.at(currentImage).clear();
        renderFences.at(currentImage).clear();

        for(VkSemaphore semaphore: imageSemaphores)
        {
            vkDestroySemaphore(devicePtr->getDevice(), semaphore, nullptr);
        }
    }

    VkCommandBuffer RenderPass::startNewFrame()
    {
        for(auto result = preRenderFences.at(currentImage).begin(); result != preRenderFences.at(currentImage).end(); result++)
        {
            result->get()->waitForFence();
        }

        for(auto result = renderFences.at(currentImage).begin(); result != renderFences.at(currentImage).end(); result++)
        {
            result->get()->waitForFence();
        }

        vkDestroySemaphore(devicePtr->getDevice(), imageSemaphores.at(currentImage), nullptr);
        imageSemaphores.at(currentImage) = commandsPtr->getSemaphore();
        
        //get available image
        checkSwapchain(vkAcquireNextImageKHR(devicePtr->getDevice(),
            *(swapchainPtr->getSwapchainPtr()),
            UINT32_MAX,
            imageSemaphores.at(currentImage),
            VK_NULL_HANDLE, &currentImage));
        
        descriptorsPtr->refreshPools(currentImage);
        //update uniform data

        double time = glfwGetTime();

        AmbientLight ambient = {};
        ambient.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.02f);

        DirectLight sun = {};
        sun.direction = glm::vec4(0.0f, -1.0f, 0.0f, 1.0f);
        sun.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);

        LightInfo lightInfo = {};
        lightInfo.ambient = ambient;
        lightInfo.camPos = glm::vec4(cameraPtr->getTranslation().position, 0.0f);
        lightInfo.sun = sun;

        lightInfo.pointLights[0] = {
            .position = glm::vec4(-cos(time) * 10.0f, -5.0f, -sin(time) * 10.0f, 1.0f),
            .color = glm::vec4(1.0f, 1.0f, 1.0f, 10.0f)
        };
        lightInfo.pointLights[1] = {
            .position = glm::vec4(-sin(time) * 10.0f, -5.0f, -cos(time) * 10.0f, 1.0f),
            .color = glm::vec4(1.0f, 1.0f, 1.0f, 10.0f)
        };
        lightInfo.pointLights[2] = {
            .position = glm::vec4(sin(time) * 10.0f, -5.0f, cos(time) * 10.0f, 1.0f),
            .color = glm::vec4(1.0f, 1.0f, 1.0f, 10.0f)
        };
        lightInfo.pointLights[3] = {
            .position = glm::vec4(cos(time) * 10.0f, -5.0f, sin(time) * 10.0f, 1.0f),
            .color = glm::vec4(1.0f, 1.0f, 1.0f, 10.0f)
        };

        uniformDatas.at(currentImage).cameraData.view = cameraPtr->getViewMatrix();
        uniformDatas.at(currentImage).cameraData.projection = cameraPtr->getProjection();
        uniformDatas.at(currentImage).lightInfo = lightInfo;

        //command buffer
        VkCommandBufferBeginInfo commandInfo;
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandInfo.pNext = NULL;
        commandInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        commandInfo.pInheritanceInfo = NULL;

        //begin recording
        VkCommandBuffer graphicsCmdBuffer = commandsPtr->getCommandBuffer(CmdPoolType::GRAPHICS);
        vkBeginCommandBuffer(graphicsCmdBuffer, &commandInfo);

        //dynamic rendering info
        VkClearValue clearValue = {};
        clearValue.color = {0.0f, 0.0f, 0.0, 0.0f};

        VkClearValue depthClear = {};
        depthClear.depthStencil = {1.0f, 0};

        VkRect2D renderArea = {};
        renderArea.offset = {0, 0};
        renderArea.extent = swapchainPtr->getExtent();

        //----------RENDER TARGETS----------//

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

        renderFences.at(currentImage).clear();
        preRenderFences.at(currentImage).clear();

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

    void RenderPass::composeAttachments(const VkCommandBuffer &cmdBuffer)
    {

    }

    void RenderPass::drawIndexed(const DrawBufferObject& objectData, const VkCommandBuffer& cmdBuffer)
    {
        VkDeviceSize offset[1] = {0};

        //update push constants
        std::vector<glm::mat4> pushData(1);
        pushData[0] = *(objectData.modelMatrix);

        vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &objectData.mesh->getVertexBuffer().getBuffer(), offset);
        vkCmdBindIndexBuffer(cmdBuffer, objectData.mesh->getIndexBuffer().getBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmdBuffer, objectData.mesh->getIndexBuffer().getLength(), 1, 0, 0, 0);
    }

    void RenderPass::drawIndexedIndirect(const VkCommandBuffer& cmdBuffer, IndirectDrawBuffer* drawBuffer)
    {
        std::vector<QueueReturn> pushFences = std::move(drawBuffer->draw(cmdBuffer, currentImage));
        for(int i = 0; i < pushFences.size(); i++)
        {
            preRenderFences.at(currentImage).push_back(std::make_shared<QueueReturn>(std::move(pushFences[i])));
        }
    }

    void RenderPass::bindMaterial(Material const* material, const VkCommandBuffer &cmdBuffer)
    {
        material->bindPipeline(cmdBuffer, *globalUBOs.at(currentImage), uniformDatas.at(currentImage), currentImage);
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
        std::vector<VkPipelineStageFlags> pipelineStages = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        };

        std::vector<VkSemaphore> waitSemaphores;
        
        waitSemaphores.push_back(imageSemaphores.at(currentImage));
        if(preRenderFences.size())
        {
            for(auto fence = preRenderFences.at(currentImage).begin(); fence != preRenderFences.at(currentImage).end(); fence++)
            {
                for(VkSemaphore& semaphore : fence->get()->getSemaphores())
                {
                    waitSemaphores.push_back(semaphore);
                    pipelineStages.push_back(VK_PIPELINE_STAGE_TRANSFER_BIT);
                }
            }
        }
        
        VkSemaphore graphicsSemapore = commandsPtr->getSemaphore();
        VkSubmitInfo queueSubmitInfo = {};
        queueSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        queueSubmitInfo.pNext = NULL;
        queueSubmitInfo.waitSemaphoreCount = waitSemaphores.size();
        queueSubmitInfo.pWaitSemaphores = waitSemaphores.data();
        queueSubmitInfo.pWaitDstStageMask = pipelineStages.data(); //only one semaphore, one stage
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