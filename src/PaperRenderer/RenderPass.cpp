#include "RenderPass.h"
#include "PaperRenderer.h"

namespace PaperRenderer
{
    //----------PREPROCESS PIPELINES DEFINITIONS----------//

    RasterPreprocessPipeline::RasterPreprocessPipeline(std::string fileDir)
    {
        //preprocess uniform buffers
        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            PaperMemory::BufferInfo preprocessBuffersInfo = {};
            preprocessBuffersInfo.queueFamilyIndices = { 
                PipelineBuilder::getRendererInfo().devicePtr->getQueues().at(PaperMemory::QueueType::GRAPHICS).queueFamilyIndex,
                PipelineBuilder::getRendererInfo().devicePtr->getQueues().at(PaperMemory::QueueType::COMPUTE).queueFamilyIndex};
            preprocessBuffersInfo.usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR;
            preprocessBuffersInfo.size = sizeof(UBOInputData);
            uniformBuffers.push_back(std::make_unique<PaperMemory::Buffer>(PipelineBuilder::getRendererInfo().devicePtr->getDevice(), preprocessBuffersInfo));
        }
        //uniform buffers allocation and assignment
        VkDeviceSize ubosAllocationSize = 0;
        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            ubosAllocationSize += PaperMemory::DeviceAllocation::padToMultiple(uniformBuffers.at(i)->getMemoryRequirements().size, 
                std::max(uniformBuffers.at(i)->getMemoryRequirements().alignment, PipelineBuilder::getRendererInfo().devicePtr->getGPUProperties().properties.limits.minMemoryMapAlignment));
        }
        PaperMemory::DeviceAllocationInfo uboAllocationInfo = {};
        uboAllocationInfo.allocationSize = ubosAllocationSize;
        uboAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT; //use coherent memory for UBOs
        uniformBuffersAllocation = std::make_unique<PaperMemory::DeviceAllocation>(PipelineBuilder::getRendererInfo().devicePtr->getDevice(), PipelineBuilder::getRendererInfo().devicePtr->getGPU(), uboAllocationInfo);
        
        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            uniformBuffers.at(i)->assignAllocation(uniformBuffersAllocation.get());
        }
        
        //pipeline info
        shader = { VK_SHADER_STAGE_COMPUTE_BIT, fileDir + fileName };

        VkDescriptorSetLayoutBinding inputDataDescriptor = {};
        inputDataDescriptor.binding = 0;
        inputDataDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        inputDataDescriptor.descriptorCount = 1;
        inputDataDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        descriptorSets[0].descriptorBindings[0] = inputDataDescriptor;

        VkDescriptorSetLayoutBinding inputObjectsDescriptor = {};
        inputObjectsDescriptor.binding = 1;
        inputObjectsDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        inputObjectsDescriptor.descriptorCount = 1;
        inputObjectsDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        descriptorSets[0].descriptorBindings[1] = inputObjectsDescriptor;

        buildPipeline();
    }
    
    RasterPreprocessPipeline::~RasterPreprocessPipeline()
    {
        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            uniformBuffers.at(i).reset();
        }
        uniformBuffersAllocation.reset();
    }

    PaperMemory::CommandBuffer RasterPreprocessPipeline::submit(Camera* camera, const IndirectRenderingData& renderingData, uint32_t currentImage, PaperMemory::SynchronizationInfo syncInfo)
    {
        UBOInputData uboInputData;
        uboInputData.bufferAddress = renderingData.bufferData->getBufferDeviceAddress();
        uboInputData.camPos = glm::vec4(camera->getTranslation().position, 1.0f);
        uboInputData.projection = camera->getProjection();
        uboInputData.view = camera->getViewMatrix();
        uboInputData.objectCount = renderingData.objectCount;
        uboInputData.frustumData = camera->getFrustum();

        PaperMemory::BufferWrite write = {};
        write.data = &uboInputData;
        write.size = sizeof(UBOInputData);
        write.offset = 0;

        uniformBuffers.at(currentImage)->writeToBuffer({ write });

        //set0 - binding 0: UBO input data
        VkDescriptorBufferInfo bufferWrite0Info = {};
        bufferWrite0Info.buffer = uniformBuffers.at(currentImage)->getBuffer();
        bufferWrite0Info.offset = 0;
        bufferWrite0Info.range = sizeof(UBOInputData);

        BuffersDescriptorWrites bufferWrite0 = {};
        bufferWrite0.binding = 0;
        bufferWrite0.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bufferWrite0.infos = { bufferWrite0Info };

        //set0 - binding 1: input objects
        VkDescriptorBufferInfo bufferWrite1Info = {};
        bufferWrite1Info.buffer = renderingData.bufferData->getBuffer();
        bufferWrite1Info.offset = renderingData.inputObjectsRegion.dstOffset;
        bufferWrite1Info.range = renderingData.inputObjectsRegion.size;

        BuffersDescriptorWrites bufferWrite1 = {};
        bufferWrite1.binding = 1;
        bufferWrite1.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bufferWrite1.infos = { bufferWrite1Info };

        VkCommandBufferBeginInfo commandInfo;
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandInfo.pNext = NULL;
        commandInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        commandInfo.pInheritanceInfo = NULL;

        VkCommandBuffer cullingCmdBuffer = PaperMemory::Commands::getCommandBuffer(PipelineBuilder::getRendererInfo().devicePtr->getDevice(), syncInfo.queueType);

        vkBeginCommandBuffer(cullingCmdBuffer, &commandInfo);
        bind(cullingCmdBuffer);

        DescriptorWrites descriptorWritesInfo = {};
        descriptorWritesInfo.bufferWrites = { bufferWrite0, bufferWrite1 };
        descriptorWrites[0] = descriptorWritesInfo;
        writeDescriptorSet(cullingCmdBuffer, currentImage, 0);

        //dispatch
        workGroupSizes.x = ((renderingData.objectCount) / 128) + 1;
        dispatch(cullingCmdBuffer);
        
        vkEndCommandBuffer(cullingCmdBuffer);

        //submit
        PaperMemory::Commands::submitToQueue(PipelineBuilder::getRendererInfo().devicePtr->getDevice(), syncInfo, { cullingCmdBuffer });

        return { cullingCmdBuffer, syncInfo.queueType };
    }

    //----------RENDER PASS DEFINITIONS----------//
    
    OldRenderPass::RenderPass(Swapchain* swapchain, Device* device, DescriptorAllocator* descriptors, PipelineBuilder* pipelineBuilder)
        :swapchainPtr(swapchain),
        devicePtr(device),
        descriptorsPtr(descriptors),
        pipelineBuilderPtr(pipelineBuilder)
    {
        //THE UBER-BUFFER
        newDataStagingBuffers.resize(PaperMemory::Commands::getFrameCount());
        stagingAllocations.resize(PaperMemory::Commands::getFrameCount());
        renderingData.resize(PaperMemory::Commands::getFrameCount());

        //synchronization objects
        bufferCopySemaphores.resize(PaperMemory::Commands::getFrameCount());
        bufferCopyFences.resize(PaperMemory::Commands::getFrameCount());
        rasterPreprocessSemaphores.resize(PaperMemory::Commands::getFrameCount());
        preprocessTLASSignalSemaphores.resize(PaperMemory::Commands::getFrameCount());
        renderSemaphores.resize(PaperMemory::Commands::getFrameCount());
        renderFences.resize(PaperMemory::Commands::getFrameCount());

        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            //descriptors
            descriptorsPtr->refreshPools(i);

            //synchronization stuff
            bufferCopySemaphores.at(i) = PaperMemory::Commands::getSemaphore(devicePtr->getDevice());
            bufferCopyFences.at(i) = PaperMemory::Commands::getSignaledFence(devicePtr->getDevice());
            rasterPreprocessSemaphores.at(i) = PaperMemory::Commands::getSemaphore(devicePtr->getDevice());
            preprocessTLASSignalSemaphores.at(i) = PaperMemory::Commands::getSemaphore(devicePtr->getDevice());
            renderSemaphores.at(i) = PaperMemory::Commands::getSemaphore(devicePtr->getDevice());
            renderFences.at(i) = PaperMemory::Commands::getSignaledFence(devicePtr->getDevice());

            //rendering staging data buffer
            PaperMemory::BufferInfo renderingDataBuffersInfo = {};
            renderingDataBuffersInfo.queueFamilyIndices = { 
                devicePtr->getQueues().at(PaperMemory::QueueType::GRAPHICS).queueFamilyIndex,
                devicePtr->getQueues().at(PaperMemory::QueueType::COMPUTE).queueFamilyIndex};
            renderingDataBuffersInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT ;
            renderingDataBuffersInfo.size = 256; //arbitrary starting size
            renderingData.at(i).bufferData = std::make_unique<PaperMemory::Buffer>(devicePtr->getDevice(), renderingDataBuffersInfo);

            //dedicated allocation for rendering data
            rebuildRenderDataAllocation(i);

            //create staging buffers
            PaperMemory::BufferInfo newDataStagingBufferInfo = {};
            newDataStagingBufferInfo.queueFamilyIndices = {}; //only uses transfer
            newDataStagingBufferInfo.size = 256;
            newDataStagingBufferInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            newDataStagingBuffers.at(i) = std::make_unique<PaperMemory::Buffer>(devicePtr->getDevice(), newDataStagingBufferInfo);

            //create staging allocations
            PaperMemory::DeviceAllocationInfo stagingAllocationInfo = {};
            stagingAllocationInfo.allocationSize = newDataStagingBuffers.at(i)->getMemoryRequirements().size;
            stagingAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            stagingAllocations.at(i) = std::make_unique<PaperMemory::DeviceAllocation>(devicePtr->getDevice(), devicePtr->getGPU(), stagingAllocationInfo);

            //assign allocation
            if(newDataStagingBuffers.at(i)->assignAllocation(stagingAllocations.at(i).get()) != 0)
            {
                throw std::runtime_error("Failed to assign allocation to staging buffer for rendering data");
            }
        }

        //----------PREPROCESS PIPELINES----------//

        rasterPreprocessPipeline = std::make_unique<RasterPreprocessPipeline>("resources/shaders/");
    }

    OldRenderPass::~RenderPass()
    {
        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            vkDestroySemaphore(devicePtr->getDevice(), bufferCopySemaphores.at(i), nullptr);
            vkDestroyFence(devicePtr->getDevice(), bufferCopyFences.at(i), nullptr);
            vkDestroySemaphore(devicePtr->getDevice(), rasterPreprocessSemaphores.at(i), nullptr);
            vkDestroySemaphore(devicePtr->getDevice(), preprocessTLASSignalSemaphores.at(i), nullptr);
            vkDestroySemaphore(devicePtr->getDevice(), renderSemaphores.at(i), nullptr);
            vkDestroyFence(devicePtr->getDevice(), renderFences.at(i), nullptr);
        }
    }

    void OldRenderPass::rebuildRenderDataAllocation(uint32_t currentFrame)
    {
        //dedicated allocation for rendering data
        PaperMemory::DeviceAllocationInfo dedicatedAllocationInfo = {};
        dedicatedAllocationInfo.allocationSize = renderingData.at(currentFrame).bufferData->getMemoryRequirements().size;
        dedicatedAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        dedicatedAllocationInfo.allocFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        renderingData.at(currentFrame).bufferAllocation = std::make_unique<PaperMemory::DeviceAllocation>(devicePtr->getDevice(), devicePtr->getGPU(), dedicatedAllocationInfo);

        //assign allocation
        int errorCheck = 0;
        errorCheck += renderingData.at(currentFrame).bufferData->assignAllocation(renderingData.at(currentFrame).bufferAllocation.get());

        if(errorCheck != 0)
        {
            throw std::runtime_error("Render data allocation rebuild failed"); //programmer error
        }
    }

    void OldRenderPass::setRasterStagingData(const std::unordered_map<Material*, MaterialNode> &renderTree)
    {
        //clear old
        std::vector<char>& stagingData = renderingData.at(currentImage).stagingData;
        stagingData.clear();

        //mesh data
        for(const auto& [material, materialNode] : renderTree)
        {
            for(const auto& [materialInstance, instanceNode] : materialNode.instances)
            {
                std::vector<char> insertionData = instanceNode.meshGroups->getPreprocessData(stagingData.size());
                stagingData.insert(stagingData.end(), insertionData.begin(), insertionData.end());
            }
        }

        //model instances mesh data
        std::vector<ModelInstance::ShaderInputObject> shaderInputObjects;
        for(ModelInstance* inputObject : renderingModels)
        {
            if(inputObject->getVisibility())
            {
                std::vector<char> insertionData = inputObject->getRasterPreprocessData(stagingData.size());
                stagingData.insert(stagingData.end(), insertionData.begin(), insertionData.end());

                shaderInputObjects.push_back(inputObject->getShaderInputObject());
            }
        }

        //shader input objects
        uint32_t inputObjectsCopyLocation = PaperMemory::DeviceAllocation::padToMultiple(stagingData.size(), devicePtr->getGPUProperties().properties.limits.minStorageBufferOffsetAlignment);
        stagingData.resize(inputObjectsCopyLocation + shaderInputObjects.size() * sizeof(ModelInstance::ShaderInputObject));
        memcpy(stagingData.data() + inputObjectsCopyLocation, shaderInputObjects.data(), shaderInputObjects.size() * sizeof(ModelInstance::ShaderInputObject));
        
        renderingData.at(currentImage).inputObjectsRegion.dstOffset = inputObjectsCopyLocation;
        renderingData.at(currentImage).inputObjectsRegion.size = shaderInputObjects.size() * sizeof(ModelInstance::ShaderInputObject);
        renderingData.at(currentImage).objectCount = shaderInputObjects.size();
    }

    void OldRenderPass::rasterPreProcess(const std::unordered_map<Material*, MaterialNode> &renderTree)
    {
        //set staging data (comes after fence for latency reasons), and copy
        setRasterStagingData(renderTree);
        copyStagingData();

        //submit compute shader
        PaperMemory::SynchronizationInfo syncInfo2 = {};
        syncInfo2.queueType = PaperMemory::QueueType::COMPUTE;
        syncInfo2.waitPairs = { { bufferCopySemaphores.at(currentImage), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT } };
        syncInfo2.signalPairs = { { rasterPreprocessSemaphores.at(currentImage), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT } };
        syncInfo2.fence = VK_NULL_HANDLE;

        usedCmdBuffers.at(currentImage).push_back(rasterPreprocessPipeline->submit(cameraPtr, renderingData.at(currentImage), currentImage, syncInfo2));
    }

    void OldRenderPass::rayTracePreProcess(const std::unordered_map<Material *, MaterialNode> &renderTree)
    {
        //set staging data (comes after fence for latency reasons), and copy
        setRTStagingData(renderTree);
        copyStagingData();

        PaperMemory::SynchronizationInfo blasUpdateSyncInfo = {};
        blasUpdateSyncInfo.queueType = PaperMemory::QueueType::COMPUTE;
        blasUpdateSyncInfo.waitPairs = { { bufferCopySemaphores.at(currentImage), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT } };
        blasUpdateSyncInfo.signalPairs = { { BLASBuildSemaphores.at(currentImage), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT } }; //signal for TLAS
        blasUpdateSyncInfo.fence = VK_NULL_HANDLE;
        usedCmdBuffers.at(currentImage).push_back(rtAccelStructure.updateBLAS(renderingModels, blasUpdateSyncInfo, currentImage)); //update BLAS

        //update TLAS
        PaperMemory::SynchronizationInfo tlasSyncInfo;
        tlasSyncInfo.queueType = PaperMemory::QueueType::COMPUTE;
        tlasSyncInfo.waitPairs = { { BLASBuildSemaphores.at(currentImage), VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR } };
        tlasSyncInfo.signalPairs = { { TLASBuildSemaphores.at(currentImage), VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR } };
        tlasSyncInfo.fence = RTFences.at(currentImage);
        rtAccelStructure.updateTLAS(tlasSyncInfo, currentImage);

        //TODO DISPATCH
        throw std::runtime_error("RT incomplete, please dont use");
    }

    void RenderPass::raster(std::unordered_map<Material*, MaterialNode>& renderTree)
    {
        //command buffer
        VkCommandBufferBeginInfo commandInfo;
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandInfo.pNext = NULL;
        commandInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        commandInfo.pInheritanceInfo = NULL;

        //begin recording
        VkCommandBuffer graphicsCmdBuffer = PaperMemory::Commands::getCommandBuffer(devicePtr->getDevice(), PaperMemory::QueueType::GRAPHICS);
        vkBeginCommandBuffer(graphicsCmdBuffer, &commandInfo);

        //transition image layout from undefined to color optimal
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
        colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
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

        //record draw commands
        for(auto& [material, materialNode] : renderTree) //material
        {
            material->bind(graphicsCmdBuffer, currentImage);

            for(auto& [materialInstance, instanceNode] : materialNode.instances) //material instances
            {
                materialInstance->bind(graphicsCmdBuffer, currentImage);
                instanceNode.meshGroups->draw(graphicsCmdBuffer, renderingData.at(currentImage).bufferData->getBuffer(), currentImage);
            }
        }

        //end render "pass"
        vkCmdEndRendering(graphicsCmdBuffer);

        //transition image layout from color optimal to present src
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

        VkDependencyInfo dependencyInfo2 = {};
        dependencyInfo2.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dependencyInfo2.pNext = NULL;
        dependencyInfo2.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        dependencyInfo2.memoryBarrierCount = 0;
        dependencyInfo2.pMemoryBarriers = NULL;
        dependencyInfo2.bufferMemoryBarrierCount = 0;
        dependencyInfo2.pBufferMemoryBarriers = NULL;
        dependencyInfo2.imageMemoryBarrierCount = 1;
        dependencyInfo2.pImageMemoryBarriers = &imageBarrier;
        
        vkCmdPipelineBarrier2(graphicsCmdBuffer, &dependencyInfo2);

        vkEndCommandBuffer(graphicsCmdBuffer);

        //submit rendering to GPU   
        PaperMemory::SynchronizationInfo syncInfo = {};
        syncInfo.queueType = PaperMemory::QueueType::GRAPHICS;
        syncInfo.waitPairs = { 
            { imageSemaphores.at(currentImage), VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT },
            { rasterPreprocessSemaphores.at(currentImage), VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT }
        };
        syncInfo.signalPairs = { 
            { renderSemaphores.at(currentImage), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT }
        };
        syncInfo.fence = renderFences.at(currentImage);

        PaperMemory::Commands::submitToQueue(devicePtr->getDevice(), syncInfo, { graphicsCmdBuffer });

        usedCmdBuffers.at(currentImage).push_back({ graphicsCmdBuffer, PaperMemory::QueueType::GRAPHICS });
    }
}