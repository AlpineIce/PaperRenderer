#include "RenderPass.h"
#include "glm/gtc/matrix_transform.hpp"

namespace Renderer
{
    RenderPass::RenderPass(Swapchain* swapchain, Device* device, Commands* commands, Descriptors* descriptors)
        :swapchainPtr(swapchain),
        devicePtr(device),
        commandsPtr(commands),
        descriptorsPtr(descriptors),
        currentImage(0)
    {
        //synchronization objects
        imageSemaphores.resize(commandsPtr->getFrameCount());
        for(VkSemaphore& semaphore : imageSemaphores)
        {
            VkSemaphoreCreateInfo semaphoreInfo;
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            semaphoreInfo.pNext = NULL;
            semaphoreInfo.flags = 0;

            vkCreateSemaphore(devicePtr->getDevice(), &semaphoreInfo, nullptr, &semaphore);
        }

        renderSemaphores.resize(commandsPtr->getFrameCount());
        for(VkSemaphore& semaphore : renderSemaphores)
        {
            VkSemaphoreCreateInfo semaphoreInfo;
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            semaphoreInfo.pNext = NULL;
            semaphoreInfo.flags = 0;

            vkCreateSemaphore(devicePtr->getDevice(), &semaphoreInfo, nullptr, &semaphore);
        }
        
        renderingFences.resize(commandsPtr->getFrameCount());
        for(VkFence& fence : renderingFences)
        {
            VkFenceCreateInfo fenceInfo;
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.pNext = NULL;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

            vkCreateFence(devicePtr->getDevice(), &fenceInfo, nullptr, &fence);
        }
    }

    RenderPass::~RenderPass()
    {
        for(VkSemaphore& semaphore : imageSemaphores)
        {
            vkDestroySemaphore(devicePtr->getDevice(), semaphore, nullptr);
        }

        for(VkSemaphore& semaphore : renderSemaphores)
        {
            vkDestroySemaphore(devicePtr->getDevice(), semaphore, nullptr);
        }

        for(VkFence& fence : renderingFences)
        {
            vkDestroyFence(devicePtr->getDevice(), fence, nullptr);
        }
    }

    void RenderPass::startNewFrame()
    {
        //update uniform buffer for next frame
        UniformBufferObject uniformData;
        uniformData.view = cameraPtr->getViewMatrix();
        uniformData.projection = cameraPtr->getProjection();
        descriptorsPtr->updateUniforms(&uniformData);

        //begin dynamic render "pass"
        VkCommandBufferBeginInfo commandInfo;
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandInfo.pNext = NULL;
        commandInfo.flags = 0;
        commandInfo.pInheritanceInfo = NULL;

        //begin recording
        vkResetCommandBuffer(commandsPtr->getCommandBuffersPtr()->graphics.at(currentImage), 0);
        vkBeginCommandBuffer(commandsPtr->getCommandBuffersPtr()->graphics.at(currentImage), &commandInfo);

        VkClearValue clearValue = {};
        clearValue.color = {0.0f, 0.0f, 0.0, 1.0f};

        VkClearValue depthClear = {};
        depthClear.depthStencil = {1.0f, 0};

        VkRect2D renderArea = {};
        renderArea.offset = {0, 0};
        renderArea.extent = swapchainPtr->getExtent();
        
        //swapchain images
        VkRenderingAttachmentInfo colorAttachment = {};
        colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorAttachment.pNext = NULL;
        colorAttachment.imageView = swapchainPtr->getImageViews()->at(currentImage);
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.clearValue = clearValue;

        //depth buffer attachment
        VkRenderingAttachmentInfo depthAttachment = {};
        depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachment.pNext = NULL;
        depthAttachment.imageView = swapchainPtr->getDepthView();
        depthAttachment.imageLayout = swapchainPtr->getDepthLayout();
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; //TODO this may not play nice when using multiple RPs
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
        renderInfo.colorAttachmentCount = 1;
        renderInfo.pColorAttachments = &colorAttachment;
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
            commandsPtr->getCommandBuffersPtr()->graphics.at(currentImage), 
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            0,
            0,
            NULL,
            0,
            NULL,
            1,
            &imageBarrier);

        vkCmdBeginRendering(commandsPtr->getCommandBuffersPtr()->graphics.at(currentImage), &renderInfo);

        //dynamic viewport and scissor specified in pipelines
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)(swapchainPtr->getExtent().width);
        viewport.height = (float)(swapchainPtr->getExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        vkCmdSetViewportWithCount(commandsPtr->getCommandBuffersPtr()->graphics.at(currentImage), 1, &viewport);
        vkCmdSetScissorWithCount(commandsPtr->getCommandBuffersPtr()->graphics.at(currentImage), 1, &renderArea);
    }

    void RenderPass::checkSwapchain(VkResult imageResult)
    {
        if(imageResult == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateFlag = true;
        }
    }

    void RenderPass::drawIndexed(const ObjectParameters& objectData)
    {
        
        VkCommandBuffer drawBuffer = commandsPtr->getCommandBuffersPtr()->graphics.at(currentImage);
        VkDeviceSize offset[1] = {0};

        //update push constants
        float time = glfwGetTime();
        
        std::vector<glm::mat4> pushData(1);
        pushData[0] = *(objectData.modelMatrix);

        vkCmdPushConstants(commandsPtr->getCommandBuffersPtr()->graphics.at(currentImage),
            pipeline->getLayout(),
            VK_SHADER_STAGE_VERTEX_BIT,
            pipeline->getPushConstantRange().offset,
            pipeline->getPushConstantRange().size, 
            pushData.data());

        vkCmdBindVertexBuffers(drawBuffer, 0, 1, &objectData.mesh->getVertexBuffer(), offset);
        vkCmdBindIndexBuffer(drawBuffer, objectData.mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdBindDescriptorSets(drawBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline->getLayout(),
            0,
            1,
            descriptorsPtr->getDescriptorSetPtr(),
            descriptorsPtr->getOffsets().size(),
            descriptorsPtr->getOffsets().data());
        vkCmdDrawIndexed(drawBuffer, objectData.mesh->getIndexBufferSize(), 1, 0, 0, 0);
    }

    void RenderPass::bindPipeline(Pipeline const* pipeline)
    {
        if(pipeline != NULL && this->pipeline != pipeline) //bind new pipeline if the new one is valid and isnt the same
        {
            this->pipeline = pipeline;
            vkCmdBindPipeline(commandsPtr->getCommandBuffersPtr()->graphics.at(currentImage),
            VK_PIPELINE_BIND_POINT_GRAPHICS, 
            pipeline->getPipeline());
        }
        else //continue with next binding
        {
            this->pipeline = pipeline;
        }
    }

    void RenderPass::bindMaterial(Material const* material)
    {
        //descriptorsPtr->updateUniforms()
        descriptorsPtr->updateTextures(material->getTextures());
    }

    void RenderPass::incrementFrameCounter()
    {
        //end render "pass"
        vkCmdEndRendering(commandsPtr->getCommandBuffersPtr()->graphics.at(currentImage));

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
            commandsPtr->getCommandBuffersPtr()->graphics.at(currentImage), 
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0,
            NULL,
            0,
            NULL,
            1,
            &imageBarrier);

        vkEndCommandBuffer(commandsPtr->getCommandBuffersPtr()->graphics.at(currentImage));

        //submit rendering to GPU
        std::vector<VkPipelineStageFlags> pipelineStages = {
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        };

        VkSubmitInfo queueSubmitInfo = {};
        queueSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        queueSubmitInfo.pNext = NULL;
        queueSubmitInfo.waitSemaphoreCount = 1;
        queueSubmitInfo.pWaitSemaphores = &(imageSemaphores.at(currentImage));
        queueSubmitInfo.pWaitDstStageMask = pipelineStages.data(); //only one semaphore, one stage
        queueSubmitInfo.commandBufferCount = 1;
        queueSubmitInfo.pCommandBuffers = &(commandsPtr->getCommandBuffersPtr()->graphics.at(currentImage));
        queueSubmitInfo.signalSemaphoreCount = 1;
        queueSubmitInfo.pSignalSemaphores = &(renderSemaphores.at(currentImage));

        VkResult graphicsResult = vkQueueSubmit(devicePtr->getQueues().graphics[0], 1, &queueSubmitInfo, renderingFences.at(currentImage));

        //submit rendered image to swapchain
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.pNext = NULL;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &(renderSemaphores.at(currentImage));
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchainPtr->getSwapchainPtr();
        presentInfo.pImageIndices = &currentImage;
        presentInfo.pResults = NULL;

        VkResult queueResult = vkQueuePresentKHR(devicePtr->getQueues().present[0], &presentInfo);

        if(currentImage == 0)
        {
            currentImage = 1;
        }
        else
        {
            currentImage = 0;
        }

        //wait for the next frame
        vkWaitForFences(devicePtr->getDevice(), 1, &(renderingFences.at(currentImage)), VK_TRUE, UINT64_MAX);
        
        //get available image
        checkSwapchain(vkAcquireNextImageKHR(devicePtr->getDevice(),
            *(swapchainPtr->getSwapchainPtr()),
            UINT16_MAX,
            imageSemaphores.at(currentImage),
            VK_NULL_HANDLE, &currentImage));

        vkResetFences(devicePtr->getDevice(), 1, &(renderingFences.at(currentImage)));

        if (queueResult == VK_ERROR_OUT_OF_DATE_KHR || queueResult == VK_SUBOPTIMAL_KHR || recreateFlag) 
        {
            recreateFlag = false;
            swapchainPtr->recreate();
            cameraPtr->updateCameraProjection();
        }
    }
}