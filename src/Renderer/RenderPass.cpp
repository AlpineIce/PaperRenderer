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
        //THE UBER-BUFFER
        renderingData.resize(commandsPtr->getFrameCount());
        lightingInfoBuffers.resize(commandsPtr->getFrameCount());
        dedicatedStagingData.resize(commandsPtr->getFrameCount());

        //synchronization objects
        imageSemaphores.resize(commandsPtr->getFrameCount());
        stagingCopySemaphores.resize(commandsPtr->getFrameCount());
        bufferCopyFences.resize(commandsPtr->getFrameCount());
        stagingCopyFences.resize(commandsPtr->getFrameCount());
        bufferCopySemaphores.resize(commandsPtr->getFrameCount());
        cullingFences.resize(commandsPtr->getFrameCount());
        cullingSemaphores.resize(commandsPtr->getFrameCount());
        renderSemaphores.resize(commandsPtr->getFrameCount());
        renderFences.resize(commandsPtr->getFrameCount());

        for(uint32_t i = 0; i < commandsPtr->getFrameCount(); i++)
        {
            descriptorsPtr->refreshPools(i);
            renderingData.at(i) = std::make_shared<IndirectRenderingData>();
            renderingData.at(i)->bufferData = std::make_shared<StorageBuffer>(devicePtr, commandsPtr, 0);
            dedicatedStagingData.at(i) = std::make_shared<StorageBuffer>(devicePtr, commandsPtr, 0);

            imageSemaphores.at(i) = commandsPtr->getSemaphore();
            stagingCopySemaphores.at(i) = commandsPtr->getSemaphore();
            bufferCopyFences.at(i) = commandsPtr->getSignaledFence();
            stagingCopyFences.at(i) = commandsPtr->getSignaledFence();
            bufferCopySemaphores.at(i) = commandsPtr->getSemaphore();
            cullingFences.at(i) = commandsPtr->getUnsignaledFence();
            cullingSemaphores.at(i) = commandsPtr->getSemaphore();
            renderSemaphores.at(i) = commandsPtr->getSemaphore();
            renderFences.at(i) = commandsPtr->getSignaledFence();
        }

        for(auto& buffer : lightingInfoBuffers)
        {
            buffer = std::make_shared<UniformBuffer>(devicePtr, commandsPtr, sizeof(ShaderLightingInformation));
        }

        //----------PREPROCESS PIPELINE----------//

        std::vector<ShaderPair> shaderPairs = {{
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .directory = "resources/compute/IndirectDrawBuild.spv"
        }};
        std::unordered_map<uint32_t, DescriptorSet> descriptorSets;

        DescriptorSet set0;
        VkDescriptorSetLayoutBinding drawCountsDescriptor = {};
        drawCountsDescriptor.binding = 0;
        drawCountsDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        drawCountsDescriptor.descriptorCount = 1;
        drawCountsDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        set0.descriptorBindings[0] = drawCountsDescriptor;
        set0.setNumber = 0;
        descriptorSets[0] = (set0);

        DescriptorSet set1;
        VkDescriptorSetLayoutBinding inputDataDescriptor = {};
        inputDataDescriptor.binding = 0;
        inputDataDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        inputDataDescriptor.descriptorCount = 1;
        inputDataDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        set1.setNumber = 1;
        set1.descriptorBindings[0] = inputDataDescriptor;

        VkDescriptorSetLayoutBinding inputObjectsDescriptor = {};
        inputObjectsDescriptor.binding = 1;
        inputObjectsDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        inputObjectsDescriptor.descriptorCount = 1;
        inputObjectsDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        set1.descriptorBindings[1] = inputObjectsDescriptor;
        descriptorSets[1] = set1;

        PipelineBuildInfo pipelineInfo;
        pipelineInfo.descriptors = descriptorSets;
        pipelineInfo.shaderInfo = shaderPairs;
        
        meshPreprocessPipeline = pipelineBuilderPtr->buildComputePipeline(pipelineInfo);
    }

    RenderPass::~RenderPass()
    {
        for(uint32_t i = 0; i < commandsPtr->getFrameCount(); i++)
        {
            vkDestroySemaphore(devicePtr->getDevice(), imageSemaphores.at(i), nullptr);
            vkDestroySemaphore(devicePtr->getDevice(), stagingCopySemaphores.at(i), nullptr);
            vkDestroyFence(devicePtr->getDevice(), bufferCopyFences.at(i), nullptr);
            vkDestroyFence(devicePtr->getDevice(), stagingCopyFences.at(i), nullptr);
            vkDestroyFence(devicePtr->getDevice(), cullingFences.at(i), nullptr);
            vkDestroySemaphore(devicePtr->getDevice(), bufferCopySemaphores.at(i), nullptr);
            vkDestroySemaphore(devicePtr->getDevice(), cullingSemaphores.at(i), nullptr);
            vkDestroySemaphore(devicePtr->getDevice(), renderSemaphores.at(i), nullptr);
            vkDestroyFence(devicePtr->getDevice(), renderFences.at(i), nullptr);
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

    void RenderPass::preProcessing(const std::unordered_map<Material*, MaterialNode>& renderTree, const LightingInformation& lightingInfo)
    {
        //----------FILL IN THE UBER-BUFFER, PRE-PROCESS THE BUFFER WITH ASYNC COMPUTE----------//

        std::vector<char>& stagingData = renderingData.at(currentImage)->stagingData;
        uint32_t oldSize = stagingData.size();
        stagingData.clear();

        //fill in point light buffer
        std::vector<PointLight> pointLights;
        for(auto light = lightingInfo.pointLights.begin(); light != lightingInfo.pointLights.end(); light++)
        {
            pointLights.push_back(**light);
        }

        uint32_t stagingEnd = stagingData.size();
        renderingData.at(currentImage)->lightsOffset = stagingEnd;
        stagingData.resize(stagingData.size() + sizeof(PointLight) * pointLights.size());
        memcpy(stagingData.data() + stagingEnd, pointLights.data(), sizeof(PointLight) * pointLights.size());

        //put lighting into staging data
        ShaderLightingInformation shaderLightingInfo = {};
        if(lightingInfo.directLight) shaderLightingInfo.directLight = *lightingInfo.directLight;
        if(lightingInfo.ambientLight) shaderLightingInfo.ambientLight = *lightingInfo.ambientLight;
        shaderLightingInfo.pointLightCount = pointLights.size();
        shaderLightingInfo.camPos = cameraPtr->getTranslation().position;
        lightingInfoBuffers.at(currentImage)->updateUniformBuffer(&shaderLightingInfo, sizeof(ShaderLightingInformation));
        renderingData.at(currentImage)->lightCount = pointLights.size();

        //pad buffer
        renderingData.at(currentImage)->stagingData.resize(renderingData.at(currentImage)->stagingData.size() % 128 + renderingData.at(currentImage)->stagingData.size());

        //get draw call counts
        for(const auto& [material, materialNode] : renderTree) //material
        {
            for(const auto& [materialInstance, instanceNode] : materialNode.instances) //material instances
            {
                appendDrawCallCounts(instanceNode.objectBuffer.get());
            }
        }

        //pad buffer
        renderingData.at(currentImage)->stagingData.resize(renderingData.at(currentImage)->stagingData.size() % 128 + renderingData.at(currentImage)->stagingData.size());

        //put objects into staging data
        for(const auto& [material, materialNode] : renderTree) //material
        {
            for(const auto& [materialInstance, instanceNode] : materialNode.instances) //material instances
            {
                appendDrawCallGroup(instanceNode.objectBuffer.get());
            }
        }

        //pad buffer (not really needed on this line though but good practice anyways)
        renderingData.at(currentImage)->stagingData.resize(renderingData.at(currentImage)->stagingData.size() % 128 + renderingData.at(currentImage)->stagingData.size());

        //allocate a new buffer before the wait fence if needed
        StagingBuffer newDataStaging(devicePtr, commandsPtr, renderingData.at(currentImage)->stagingData.size());
        newDataStaging.mapData(renderingData.at(currentImage)->stagingData.data(), 0, renderingData.at(currentImage)->stagingData.size());

        bool updateBufferSize = false;
        if(stagingData.size() > oldSize)
        {
            //rebuild and add an extra 512 kilobytes to avoid frequent reallocations (this causes a tiny micro stutter from super long allocation times of a few ms)
            dedicatedStagingData.at(currentImage) = std::make_shared<StorageBuffer>(devicePtr, commandsPtr, renderingData.at(currentImage)->stagingData.size() + 524288);
            updateBufferSize = true;
        }

        std::vector<SemaphorePair> waitPairs = {};
        std::vector<SemaphorePair> signalPairs = {
            { bufferCopySemaphores.at(currentImage), VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT }
        };
        
        vkWaitForFences(devicePtr->getDevice(), 1, &bufferCopyFences.at(currentImage), VK_TRUE, UINT64_MAX);
        vkResetFences(devicePtr->getDevice(), 1, &bufferCopyFences.at(currentImage));
        dedicatedStagingData.at(currentImage)->copyFromBuffer(newDataStaging, waitPairs, signalPairs, bufferCopyFences.at(currentImage)); 

        //perform draw call culling
        VkCommandBufferBeginInfo commandInfo;
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandInfo.pNext = NULL;
        commandInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        commandInfo.pInheritanceInfo = NULL;

        VkCommandBuffer cullingCmdBuffer = commandsPtr->getCommandBuffer(CmdPoolType::COMPUTE);
        vkBeginCommandBuffer(cullingCmdBuffer, &commandInfo);

        //bind culling pipeline and dispatch
        vkCmdBindPipeline(cullingCmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, meshPreprocessPipeline->getPipeline());
        CullingFrustum cullingFrustum = createCullingFrustum();
        for(const auto& [material, materialNode] : renderTree) //material
        {
            for(const auto& [materialInstance, instanceNode] : materialNode.instances) //material instances
            {
                drawCallCull(cullingCmdBuffer, cullingFrustum, dedicatedStagingData.at(currentImage).get(), instanceNode.objectBuffer.get());
            }
        }

        submitCulling(cullingCmdBuffer);

        //----------FINISH RENDERING THE LAST FRAME AND SWAP DATA BUFFER----------//

        //wait for rendering to finish
        std::vector<VkFence> waitFences = {
            renderFences.at(currentImage),
            cullingFences.at(currentImage)
        };
        vkWaitForFences(devicePtr->getDevice(), waitFences.size(), waitFences.data(), VK_TRUE, UINT64_MAX);
        vkResetFences(devicePtr->getDevice(), waitFences.size(), waitFences.data());

         //transfer dedicated staging buffer into rendering dedicated buffer
        if(updateBufferSize)
        {
            renderingData.at(currentImage)->bufferData = std::make_shared<StorageBuffer>(devicePtr, commandsPtr, renderingData.at(currentImage)->stagingData.size() + 524288);
        }
        std::vector<SemaphorePair> waitPairs2 = {
            { cullingSemaphores.at(currentImage), VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT }
        };

        std::vector<SemaphorePair> signalPairs2 = {
            { stagingCopySemaphores.at(currentImage), VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT }
        };

        vkWaitForFences(devicePtr->getDevice(), 1, &stagingCopyFences.at(currentImage), VK_TRUE, UINT64_MAX);
        vkResetFences(devicePtr->getDevice(), 1, &stagingCopyFences.at(currentImage));
        renderingData.at(currentImage)->bufferData->copyFromBuffer(newDataStaging, waitPairs2, signalPairs2, stagingCopyFences.at(currentImage)); 

        //get available image
        checkSwapchain(vkAcquireNextImageKHR(devicePtr->getDevice(),
            *(swapchainPtr->getSwapchainPtr()),
            500000000, //ive never been more ready to work in the gaming industry moment
            imageSemaphores.at(currentImage),
            VK_NULL_HANDLE, &currentImage));
        
        descriptorsPtr->refreshPools(currentImage);
    }

    void RenderPass::raster(const std::unordered_map<Material*, MaterialNode>& renderTree)
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

        //color attacment
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
        depthAttachment.imageView = swapchainPtr->getDepthViews().at(currentImage);
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

        VkImageMemoryBarrier2 colorImageBarrier = {};
        colorImageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        colorImageBarrier.pNext = NULL;
        colorImageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        colorImageBarrier.srcAccessMask = VK_ACCESS_NONE;
        colorImageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        colorImageBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        colorImageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorImageBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorImageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        colorImageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        colorImageBarrier.image = swapchainPtr->getImages()->at(currentImage);
        colorImageBarrier.subresourceRange =  {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        };

        VkDependencyInfo dependencyInfo = {};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.pNext = NULL;
        dependencyInfo.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dependencyInfo.memoryBarrierCount = 0;
        dependencyInfo.pMemoryBarriers = NULL;
        dependencyInfo.bufferMemoryBarrierCount = 0;
        dependencyInfo.pBufferMemoryBarriers = NULL;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &colorImageBarrier;
        
        vkCmdPipelineBarrier2(graphicsCmdBuffer, &dependencyInfo);

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

        //bind "global" descriptor

        for(const auto& [material, materialNode] : renderTree) //material
        {
            bindMaterial(material, graphicsCmdBuffer);
            
            for(const auto& [materialInstance, instanceNode] : materialNode.instances) //material instances
            {
                //if(materialNode.objectBuffer->getDrawCount() == 0) continue; potential for optimization
                
                bindMaterialInstance(materialInstance, graphicsCmdBuffer);
                drawIndexedIndirect(graphicsCmdBuffer, instanceNode.objectBuffer.get());
            }
        }

        composeAttachments(graphicsCmdBuffer);

        incrementFrameCounter(graphicsCmdBuffer);
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

    void RenderPass::appendDrawCallGroup(IndirectDrawContainer* drawBuffer)
    {
        std::vector<std::vector<ObjectPreprocessStride>> objectGroups = drawBuffer->getObjectSizes(renderingData.at(currentImage)->stagingData.size());
        for(std::vector<ObjectPreprocessStride>& objectGroup : objectGroups)
        {
            std::vector<char>& stagingData = renderingData.at(currentImage)->stagingData;
            uint32_t stagingEnd = stagingData.size();

            stagingData.resize(stagingEnd + sizeof(ObjectPreprocessStride) * objectGroup.size());
            memcpy(stagingData.data() + stagingEnd, objectGroup.data(), sizeof(ObjectPreprocessStride) * objectGroup.size());
        }
    }

    void RenderPass::appendDrawCallCounts(IndirectDrawContainer* drawBuffer)
    {
        std::vector<char>& stagingData = renderingData.at(currentImage)->stagingData;
        uint32_t stagingEnd = stagingData.size();

        uint32_t drawsCounts = drawBuffer->getDrawCountsSize(stagingEnd);
        std::vector<uint32_t> initialDrawCounts(drawsCounts);
        for(uint32_t& count : initialDrawCounts)
        {
            count = 0;
        }
        stagingData.resize(stagingEnd + sizeof(uint32_t) * drawsCounts);
        memcpy(stagingData.data() + stagingEnd, initialDrawCounts.data(), initialDrawCounts.size() * sizeof(uint32_t));
    }

    void RenderPass::drawCallCull(const VkCommandBuffer& cmdBuffer, const CullingFrustum& cullingFrustum, StorageBuffer const* newBufferData, IndirectDrawContainer* drawBuffer)
    {
        drawBuffer->dispatchCulling(cmdBuffer, meshPreprocessPipeline.get(), cullingFrustum, newBufferData, cameraPtr->getProjection(), cameraPtr->getViewMatrix(), currentImage);
    }

    void RenderPass::submitCulling(const VkCommandBuffer& cmdBuffer)
    {
        vkEndCommandBuffer(cmdBuffer);

        VkSemaphoreSubmitInfo semaphoreWaitInfo = {};
        semaphoreWaitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        semaphoreWaitInfo.pNext = NULL;
        semaphoreWaitInfo.semaphore = bufferCopySemaphores.at(currentImage);
        semaphoreWaitInfo.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        semaphoreWaitInfo.deviceIndex = 0;

        VkCommandBufferSubmitInfo cmdBufferSubmitInfo = {};
        cmdBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdBufferSubmitInfo.pNext = NULL;
        cmdBufferSubmitInfo.commandBuffer = cmdBuffer;
        cmdBufferSubmitInfo.deviceMask = 0;

        VkSemaphoreSubmitInfo semaphoreSignalInfo = {};
        semaphoreSignalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        semaphoreSignalInfo.pNext = NULL;
        semaphoreSignalInfo.semaphore = cullingSemaphores.at(currentImage);;
        semaphoreSignalInfo.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        semaphoreSignalInfo.deviceIndex = 0;

        VkSubmitInfo2 submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.pNext = NULL;
        submitInfo.flags = 0;
        submitInfo.waitSemaphoreInfoCount = 1;
        submitInfo.pWaitSemaphoreInfos = &semaphoreWaitInfo;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &cmdBufferSubmitInfo;
        submitInfo.signalSemaphoreInfoCount = 1;
        submitInfo.pSignalSemaphoreInfos = &semaphoreSignalInfo;

        vkQueueSubmit2(devicePtr->getQueues().compute.at(0), 1, &submitInfo, cullingFences.at(currentImage));
    }

    void RenderPass::composeAttachments(const VkCommandBuffer &cmdBuffer)
    {

    }

    void RenderPass::drawIndexedIndirect(const VkCommandBuffer& cmdBuffer, IndirectDrawContainer* drawBuffer)
    {
        drawBuffer->draw(cmdBuffer, renderingData.at(currentImage).get(), currentImage);
    }

    void RenderPass::bindMaterial(Material const* material, const VkCommandBuffer &cmdBuffer)
    {
        StorageBuffer const* lightsBuffer = renderingData.at(currentImage)->bufferData.get();
        uint32_t lightsOffset = renderingData.at(currentImage)->lightsOffset;
        material->bindPipeline(cmdBuffer, *lightsBuffer, lightsOffset, renderingData.at(currentImage)->lightCount, *lightingInfoBuffers.at(currentImage).get(), currentImage);
    }

    void RenderPass::bindMaterialInstance(MaterialInstance const* materialInstance, const VkCommandBuffer& cmdBuffer)
    {
        materialInstance->bind(cmdBuffer, currentImage);
    }

    void RenderPass::incrementFrameCounter(const VkCommandBuffer& cmdBuffer)
    {
        //end render "pass"
        vkCmdEndRendering(cmdBuffer);

        VkImageMemoryBarrier2 imageBarrier = {};
        imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imageBarrier.pNext = NULL;
        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_NONE;
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

        VkDependencyInfo dependencyInfo = {};
        dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo.pNext = NULL;
        dependencyInfo.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dependencyInfo.memoryBarrierCount = 0;
        dependencyInfo.pMemoryBarriers = NULL;
        dependencyInfo.bufferMemoryBarrierCount = 0;
        dependencyInfo.pBufferMemoryBarriers = NULL;
        dependencyInfo.imageMemoryBarrierCount = 1;
        dependencyInfo.pImageMemoryBarriers = &imageBarrier;
        
        vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);

        vkEndCommandBuffer(cmdBuffer);

        //submit rendering to GPU
        std::vector<VkSemaphoreSubmitInfo> graphicsWaitSemaphores;

        VkSemaphoreSubmitInfo graphicsSemaphoreWaitInfo0 = {};
        graphicsSemaphoreWaitInfo0.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        graphicsSemaphoreWaitInfo0.pNext = NULL;
        graphicsSemaphoreWaitInfo0.semaphore = imageSemaphores.at(currentImage);
        graphicsSemaphoreWaitInfo0.stageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        graphicsSemaphoreWaitInfo0.deviceIndex = 0;
        graphicsWaitSemaphores.push_back(graphicsSemaphoreWaitInfo0);

        VkSemaphoreSubmitInfo graphicsSemaphoreWaitInfo1 = {};
        graphicsSemaphoreWaitInfo1.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        graphicsSemaphoreWaitInfo1.pNext = NULL;
        graphicsSemaphoreWaitInfo1.semaphore = stagingCopySemaphores.at(currentImage);
        graphicsSemaphoreWaitInfo1.stageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        graphicsSemaphoreWaitInfo1.deviceIndex = 0;
        graphicsWaitSemaphores.push_back(graphicsSemaphoreWaitInfo1);

        VkCommandBufferSubmitInfo graphicsCmdBufferSubmitInfo = {};
        graphicsCmdBufferSubmitInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        graphicsCmdBufferSubmitInfo.pNext = NULL;
        graphicsCmdBufferSubmitInfo.commandBuffer = cmdBuffer;
        graphicsCmdBufferSubmitInfo.deviceMask = 0;

        VkSemaphoreSubmitInfo graphicsSemaphoreSignalInfo = {};
        graphicsSemaphoreSignalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        graphicsSemaphoreSignalInfo.pNext = NULL;
        graphicsSemaphoreSignalInfo.semaphore = renderSemaphores.at(currentImage);
        graphicsSemaphoreSignalInfo.stageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT; //???
        graphicsSemaphoreSignalInfo.deviceIndex = 0;
        
        VkSubmitInfo2 graphicsSubmitInfo = {};
        graphicsSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        graphicsSubmitInfo.pNext = NULL;
        graphicsSubmitInfo.flags = 0;
        graphicsSubmitInfo.waitSemaphoreInfoCount = graphicsWaitSemaphores.size();
        graphicsSubmitInfo.pWaitSemaphoreInfos = graphicsWaitSemaphores.data();
        graphicsSubmitInfo.commandBufferInfoCount = 1;
        graphicsSubmitInfo.pCommandBufferInfos = &graphicsCmdBufferSubmitInfo;
        graphicsSubmitInfo.signalSemaphoreInfoCount = 1;
        graphicsSubmitInfo.pSignalSemaphoreInfos = &graphicsSemaphoreSignalInfo;

        vkQueueSubmit2(devicePtr->getQueues().graphics.at(0), 1, &graphicsSubmitInfo, renderFences.at(currentImage));

        VkResult returnResult;
        VkPresentInfoKHR presentSubmitInfo = {};
        presentSubmitInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentSubmitInfo.pNext = NULL;
        presentSubmitInfo.waitSemaphoreCount = 1;
        presentSubmitInfo.pWaitSemaphores = &renderSemaphores.at(currentImage);
        presentSubmitInfo.swapchainCount = 1;
        presentSubmitInfo.pSwapchains = swapchainPtr->getSwapchainPtr();
        presentSubmitInfo.pImageIndices = &currentImage;
        presentSubmitInfo.pResults = &returnResult;

        VkResult presentResult = vkQueuePresentKHR(devicePtr->getQueues().present.at(0), &presentSubmitInfo);

        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR || recreateFlag) 
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