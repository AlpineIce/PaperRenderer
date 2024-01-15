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
        
        renderFences.resize(commandsPtr->getFrameCount());
        
        //setup global (per frame) descriptor set
        Pipeline::createGlobalDescriptorLayout(devicePtr);

        //global uniforms
        uniformDatas.resize(CmdBufferAllocator::getFrameCount());
        for(uint32_t i = 0; i < CmdBufferAllocator::getFrameCount(); i++)
        {
            globalUBOs.push_back(std::make_shared<UniformBuffer>(device, commands, (uint32_t)sizeof(GlobalDescriptor)));
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

        for(std::list<QueueReturn>& queueReturns : renderFences)
        {
            for(std::list<Renderer::QueueReturn>::iterator result = queueReturns.begin(); result != queueReturns.end(); result++)
            {
                commandsPtr->waitForQueue(*result);
            }
            renderFences.at(currentImage).clear();
        }
        

        Pipeline::destroyGlobalDescriptorLayout(devicePtr);
    }

    VkCommandBuffer RenderPass::startNewFrame()
    {
        std::list<QueueReturn>& queueReturns = renderFences.at(currentImage);
        for(std::list<Renderer::QueueReturn>::iterator result = queueReturns.begin(); result != queueReturns.end(); result++)
        {
            commandsPtr->waitForQueue(*result);
        }
        renderFences.at(currentImage).clear();

        descriptorsPtr->refreshPools(currentImage);

        //get available image
        checkSwapchain(vkAcquireNextImageKHR(devicePtr->getDevice(),
            *(swapchainPtr->getSwapchainPtr()),
            UINT16_MAX,
            imageSemaphores.at(currentImage),
            VK_NULL_HANDLE, &currentImage));
    
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
        commandInfo.flags = 0;
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

    void RenderPass::drawIndexed(const ObjectParameters& objectData, const VkCommandBuffer& cmdBuffer)
    {
        VkDeviceSize offset[1] = {0};

        //update push constants
        float time = glfwGetTime();
        
        std::vector<glm::mat4> pushData(1);
        pushData[0] = *(objectData.modelMatrix);

        vkCmdPushConstants(cmdBuffer,
            pipeline->getLayout(),
            VK_SHADER_STAGE_VERTEX_BIT,
            pipeline->getPushConstantRange().offset,
            pipeline->getPushConstantRange().size, 
            pushData.data());

        vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &objectData.mesh->getVertexBuffer(), offset);
        vkCmdBindIndexBuffer(cmdBuffer, objectData.mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmdBuffer, objectData.mesh->getIndexBufferSize(), 1, 0, 0, 0);
    }

    void RenderPass::bindPipeline(Pipeline const* pipeline, const VkCommandBuffer& cmdBuffer)
    {
        if(pipeline != NULL && this->pipeline != pipeline) //bind new pipeline if the new one is valid and isnt the same
        {
            this->pipeline = pipeline;
            vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipeline());

            //update global uniform
            globalUBOs.at(currentImage)->updateUniformBuffer(&uniformDatas.at(currentImage), 0, sizeof(GlobalDescriptor));

            VkDescriptorSet globalDescriptorSet = descriptorsPtr->allocateDescriptorSet(Pipeline::getGlobalDescriptorLayout(), currentImage);
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

        VkSubmitInfo queueSubmitInfo = {};
        queueSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        queueSubmitInfo.pNext = NULL;
        queueSubmitInfo.waitSemaphoreCount = 1;
        queueSubmitInfo.pWaitSemaphores = &(imageSemaphores.at(currentImage));
        queueSubmitInfo.pWaitDstStageMask = pipelineStages.data(); //only one semaphore, one stage
        queueSubmitInfo.commandBufferCount = 1;
        queueSubmitInfo.pCommandBuffers = &cmdBuffer;
        queueSubmitInfo.signalSemaphoreCount = 1;
        queueSubmitInfo.pSignalSemaphores = &(renderSemaphores.at(currentImage));

        renderFences.at(currentImage).push_back(commandsPtr->submitQueue(queueSubmitInfo, CmdPoolType::GRAPHICS));

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

        QueueReturn presentResult = commandsPtr->submitPresentQueue(presentInfo);
        //renderFences.at(currentImage).push_back(presentResult);  DONT NEED

        if (presentResult.result == VK_ERROR_OUT_OF_DATE_KHR || presentResult.result == VK_SUBOPTIMAL_KHR || recreateFlag) 
        {
            std::list<QueueReturn>& queueReturns = renderFences.at(currentImage);
            for(std::list<Renderer::QueueReturn>::iterator result = queueReturns.begin(); result != queueReturns.end(); result++)
            {
                commandsPtr->waitForQueue(*result);
            }
            renderFences.at(currentImage).clear();

            recreateFlag = false;
            swapchainPtr->recreate();
            cameraPtr->updateCameraProjection();

            for(VkSemaphore& semaphore : imageSemaphores)
            {
                vkDestroySemaphore(devicePtr->getDevice(), semaphore, nullptr);
            }
            for(VkSemaphore& semaphore : renderSemaphores)
            {
                vkDestroySemaphore(devicePtr->getDevice(), semaphore, nullptr);
            }

            for(VkSemaphore& semaphore : imageSemaphores)
            {
                VkSemaphoreCreateInfo semaphoreInfo;
                semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
                semaphoreInfo.pNext = NULL;
                semaphoreInfo.flags = 0;

                vkCreateSemaphore(devicePtr->getDevice(), &semaphoreInfo, nullptr, &semaphore);
            }
            for(VkSemaphore& semaphore : renderSemaphores)
            {
                VkSemaphoreCreateInfo semaphoreInfo;
                semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
                semaphoreInfo.pNext = NULL;
                semaphoreInfo.flags = 0;

                vkCreateSemaphore(devicePtr->getDevice(), &semaphoreInfo, nullptr, &semaphore);
            }
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