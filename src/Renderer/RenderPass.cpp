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

    void RenderPass::checkSwapchain(VkResult imageResult)
    {
        if(imageResult == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateFlag = true;
        }
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

    void RenderPass::bindPipeline(Pipeline const *pipeline, const VkCommandBuffer &cmdBuffer)
    {
        if(pipeline != NULL && this->pipeline != pipeline) //bind new pipeline if the new one is valid and isnt the same
        {
            this->pipeline = pipeline;
            vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipeline());

            //update global uniform
            globalUBOs.at(currentImage)->updateUniformBuffer(&uniformDatas.at(currentImage), sizeof(GlobalDescriptor));

            VkDescriptorSet globalDescriptorSet = descriptorsPtr->allocateDescriptorSet(*pipeline->getGlobalDescriptorLayoutPtr(), currentImage);
            descriptorsPtr->writeUniform(globalUBOs.at(currentImage)->getBuffer(), sizeof(GlobalDescriptor), 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, globalDescriptorSet);

            vkCmdBindDescriptorSets(cmdBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                this->pipeline->getLayout(),
                0, //material bind point
                1,
                &globalDescriptorSet,
                0,
                0);

        }
        else //continue with next binding
        {
            this->pipeline = pipeline;
        }
    }

    void RenderPass::bindMaterial(Material const* material, const VkCommandBuffer& cmdBuffer)
    {
        material->updateUniforms(descriptorsPtr, cmdBuffer, currentImage);
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