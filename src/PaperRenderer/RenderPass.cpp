#include "RenderPass.h"
#include "PaperRenderer.h"

#include <iostream>

namespace PaperRenderer
{
    //----------PREPROCESS PIPELINES DEFINITIONS----------//

    RasterPreprocessPipeline::RasterPreprocessPipeline(RenderEngine* renderer, std::string fileDir)
        :ComputeShader(renderer),
        rendererPtr(renderer)
    {
        //preprocess uniform buffers
        for(uint32_t i = 0; i < Commands::getFrameCount(); i++)
        {
            BufferInfo preprocessBuffersInfo = {};
            preprocessBuffersInfo.usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR;
            preprocessBuffersInfo.size = sizeof(UBOInputData);
            preprocessBuffersInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
            uniformBuffers.push_back(std::make_unique<Buffer>(rendererPtr->getDevice()->getDevice(), preprocessBuffersInfo));
        }
        //uniform buffers allocation and assignment
        VkDeviceSize ubosAllocationSize = 0;
        for(uint32_t i = 0; i < Commands::getFrameCount(); i++)
        {
            ubosAllocationSize += DeviceAllocation::padToMultiple(uniformBuffers.at(i)->getMemoryRequirements().size, 
                std::max(uniformBuffers.at(i)->getMemoryRequirements().alignment, rendererPtr->getDevice()->getGPUProperties().properties.limits.minMemoryMapAlignment));
        }
        DeviceAllocationInfo uboAllocationInfo = {};
        uboAllocationInfo.allocationSize = ubosAllocationSize;
        uboAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT; //use coherent memory for UBOs
        uniformBuffersAllocation = std::make_unique<DeviceAllocation>(rendererPtr->getDevice()->getDevice(), rendererPtr->getDevice()->getGPU(), uboAllocationInfo);
        
        for(uint32_t i = 0; i < Commands::getFrameCount(); i++)
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

        VkDescriptorSetLayoutBinding inputInstancesDescriptor = {};
        inputInstancesDescriptor.binding = 1;
        inputInstancesDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        inputInstancesDescriptor.descriptorCount = 1;
        inputInstancesDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        descriptorSets[0].descriptorBindings[1] = inputInstancesDescriptor;

        VkDescriptorSetLayoutBinding inputRenderPassInstancesDescriptor = {};
        inputRenderPassInstancesDescriptor.binding = 2;
        inputRenderPassInstancesDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        inputRenderPassInstancesDescriptor.descriptorCount = 1;
        inputRenderPassInstancesDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        descriptorSets[0].descriptorBindings[2] = inputRenderPassInstancesDescriptor;

        buildPipeline();
    }
    
    RasterPreprocessPipeline::~RasterPreprocessPipeline()
    {
        for(uint32_t i = 0; i < Commands::getFrameCount(); i++)
        {
            uniformBuffers.at(i).reset();
        }
        uniformBuffersAllocation.reset();
    }

    void RasterPreprocessPipeline::submit(const SynchronizationInfo& syncInfo, const RenderPass& renderPass)
    {
        UBOInputData uboInputData = {};
        uboInputData.camPos = glm::vec4(renderPass.cameraPtr->getTranslation().position, 1.0f);
        uboInputData.projection = renderPass.cameraPtr->getProjection();
        uboInputData.view = renderPass.cameraPtr->getViewMatrix();
        uboInputData.materialDataPtr = renderPass.deviceInstancesDataBuffer->getBufferDeviceAddress();
        uboInputData.modelDataPtr = rendererPtr->deviceModelDataBuffer->getBufferDeviceAddress();
        uboInputData.objectCount = renderPass.renderPassInstances.size();
        uboInputData.frameIndex = rendererPtr->getCurrentFrameIndex();

        BufferWrite write = {};
        write.data = &uboInputData;
        write.size = sizeof(UBOInputData);
        write.offset = 0;

        uniformBuffers.at(rendererPtr->getCurrentFrameIndex())->writeToBuffer({ write });

        //set0 - binding 0: UBO input data
        VkDescriptorBufferInfo bufferWrite0Info = {};
        bufferWrite0Info.buffer = uniformBuffers.at(rendererPtr->getCurrentFrameIndex())->getBuffer();
        bufferWrite0Info.offset = 0;
        bufferWrite0Info.range = sizeof(UBOInputData);

        BuffersDescriptorWrites bufferWrite0 = {};
        bufferWrite0.binding = 0;
        bufferWrite0.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bufferWrite0.infos = { bufferWrite0Info };

        //set0 - binding 1: model instances
        VkDescriptorBufferInfo bufferWrite1Info = {};
        bufferWrite1Info.buffer = rendererPtr->deviceInstancesDataBuffer->getBuffer();
        bufferWrite1Info.offset = 0;
        bufferWrite1Info.range = rendererPtr->renderingModelInstances.size() * sizeof(ModelInstance::ShaderModelInstance);

        BuffersDescriptorWrites bufferWrite1 = {};
        bufferWrite1.binding = 1;
        bufferWrite1.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bufferWrite1.infos = { bufferWrite1Info };

        //set0 - binding 2: input objects
        VkDescriptorBufferInfo bufferWrite2Info = {};
        bufferWrite2Info.buffer = renderPass.deviceInstancesBuffer->getBuffer();
        bufferWrite2Info.offset = 0;
        bufferWrite2Info.range = renderPass.renderPassInstances.size() * sizeof(ModelInstance::RenderPassInstance);

        BuffersDescriptorWrites bufferWrite2 = {};
        bufferWrite2.binding = 2;
        bufferWrite2.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bufferWrite2.infos = { bufferWrite2Info };

        VkCommandBufferBeginInfo commandInfo;
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandInfo.pNext = NULL;
        commandInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        commandInfo.pInheritanceInfo = NULL;

        VkCommandBuffer cullingCmdBuffer = Commands::getCommandBuffer(rendererPtr->getDevice()->getDevice(), syncInfo.queueType);

        vkBeginCommandBuffer(cullingCmdBuffer, &commandInfo);
        bind(cullingCmdBuffer);

        DescriptorWrites descriptorWritesInfo = {};
        descriptorWritesInfo.bufferWrites = { bufferWrite0, bufferWrite1, bufferWrite2 };
        descriptorWrites[0] = descriptorWritesInfo;
        writeDescriptorSet(cullingCmdBuffer, rendererPtr->getCurrentFrameIndex(), 0);

        //dispatch
        workGroupSizes.x = ((renderPass.renderPassInstances.size()) / 128) + 1;
        dispatch(cullingCmdBuffer);
        
        vkEndCommandBuffer(cullingCmdBuffer);

        //submit
        Commands::submitToQueue(rendererPtr->getDevice()->getDevice(), syncInfo, { cullingCmdBuffer });

        CommandBuffer commandBuffer = { cullingCmdBuffer, syncInfo.queueType };
        rendererPtr->recycleCommandBuffer(commandBuffer);
    }

    //----------RENDER PASS DEFINITIONS----------//

    std::unique_ptr<DeviceAllocation> RenderPass::hostInstancesAllocation;
    std::unique_ptr<DeviceAllocation> RenderPass::deviceInstancesAllocation;
    std::list<RenderPass*> RenderPass::renderPasses;

    RenderPass::RenderPass(RenderEngine* renderer, Camera* camera, MaterialInstance* defaultMaterialInstance, MaterialInstance* defaultPrepassMaterialInstance)
        :rendererPtr(renderer),
        cameraPtr(camera),
        defaultMaterialInstancePtr(defaultMaterialInstance),
        defaultPrepassMaterialInstancePtr(defaultPrepassMaterialInstance)
    {
        renderPasses.push_back(this);

        instancesBufferCopySemaphores.resize(Commands::getFrameCount());
        materialDataBufferCopySemaphores.resize(Commands::getFrameCount());
        preprocessSignalSemaphores.resize(Commands::getFrameCount());
        for(uint32_t i = 0; i < Commands::getFrameCount(); i++)
        {
            instancesBufferCopySemaphores.at(i) = Commands::getSemaphore(rendererPtr->getDevice()->getDevice());
            materialDataBufferCopySemaphores.at(i) = Commands::getSemaphore(rendererPtr->getDevice()->getDevice());
            preprocessSignalSemaphores.at(i) = Commands::getSemaphore(rendererPtr->getDevice()->getDevice());
        }

        preprocessFence = PaperRenderer::Commands::getUnsignaledFence(rendererPtr->getDevice()->getDevice());

        rebuildAllocationsAndBuffers(rendererPtr);
    }

    RenderPass::~RenderPass()
    {
        for(uint32_t i = 0; i < Commands::getFrameCount(); i++)
        {
            vkDestroySemaphore(rendererPtr->getDevice()->getDevice(), instancesBufferCopySemaphores.at(i), nullptr);
            vkDestroySemaphore(rendererPtr->getDevice()->getDevice(), materialDataBufferCopySemaphores.at(i), nullptr);
            vkDestroySemaphore(rendererPtr->getDevice()->getDevice(), preprocessSignalSemaphores.at(i), nullptr);
        }

        vkDestroyFence(rendererPtr->getDevice()->getDevice(), preprocessFence, nullptr);

        hostInstancesBuffer.reset();
        deviceInstancesBuffer.reset();
        hostInstancesDataBuffer.reset();
        deviceInstancesDataBuffer.reset();

        for(auto& [material, materialNode] : renderTree)
        {
            for(auto& [materialInstance, instanceNode] : materialNode.instances)
            {
                instanceNode.reset();
            }
        }

        renderPasses.remove(this);

        if(!renderPasses.size())
        {
            hostInstancesAllocation.reset();
            deviceInstancesAllocation.reset();
        }
    }

    void RenderPass::rebuildAllocationsAndBuffers(RenderEngine* renderer)
    {
        //copy old buffer data from all render passes, rebuild, and gather information for new size
        struct OldData
        {
            std::vector<char> oldInstanceDatas;
            std::vector<char> oldInstanceMaterialDatas;
        };
        std::unordered_map<RenderPass*, OldData> oldData;
        VkDeviceSize newHostSize = 0;
        VkDeviceSize newDeviceSize = 0;
        
        for(auto renderPass : renderPasses)
        {
            //instance data
            std::vector<char> oldInstanceData(renderPass->renderPassInstances.size() * sizeof(ModelInstance::RenderPassInstance));
            if(renderPass->hostInstancesBuffer)
            {
                memcpy(oldInstanceData.data(), renderPass->hostInstancesBuffer->getHostDataPtr(), oldInstanceData.size());
                renderPass->hostInstancesBuffer.reset();
            }
            oldData[renderPass].oldInstanceDatas = (oldInstanceData);

            //instance material data
            VkDeviceSize oldMaterialDataSize = 4096;
            std::vector<char> oldMaterialData;
            if(renderPass->hostInstancesDataBuffer)
            {
                renderPass->hostInstancesDataBuffer->compact();
                oldMaterialDataSize = renderPass->hostInstancesDataBuffer->getDesiredLocation();

                oldMaterialData.resize(renderPass->hostInstancesDataBuffer->getStackLocation());
                memcpy(oldMaterialData.data(), renderPass->hostInstancesDataBuffer->getBuffer()->getHostDataPtr(), oldMaterialData.size());
                renderPass->hostInstancesDataBuffer.reset();
            }
            oldData[renderPass].oldInstanceMaterialDatas = oldMaterialData;

            //rebuild buffers
            renderPass->rebuildBuffers(std::max(oldMaterialDataSize, (VkDeviceSize)4096));

            newHostSize += DeviceAllocation::padToMultiple(renderPass->hostInstancesBuffer->getMemoryRequirements().size, renderPass->hostInstancesBuffer->getMemoryRequirements().alignment);
            newHostSize += DeviceAllocation::padToMultiple(renderPass->hostInstancesDataBuffer->getBuffer()->getMemoryRequirements().size, renderPass->hostInstancesDataBuffer->getBuffer()->getMemoryRequirements().alignment);

            newDeviceSize += DeviceAllocation::padToMultiple(renderPass->deviceInstancesBuffer->getMemoryRequirements().size, renderPass->deviceInstancesBuffer->getMemoryRequirements().alignment);
            newDeviceSize += DeviceAllocation::padToMultiple(renderPass->deviceInstancesDataBuffer->getMemoryRequirements().size, renderPass->deviceInstancesDataBuffer->getMemoryRequirements().alignment);
        }

        //rebuild allocations
        DeviceAllocationInfo hostAllocationInfo = {};
        hostAllocationInfo.allocationSize = newHostSize;
        hostAllocationInfo.allocFlags = 0;
        hostAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        hostInstancesAllocation = std::make_unique<DeviceAllocation>(renderer->getDevice()->getDevice(), renderer->getDevice()->getGPU(), hostAllocationInfo);

        DeviceAllocationInfo deviceAllocationInfo = {};
        deviceAllocationInfo.allocationSize = newDeviceSize;
        deviceAllocationInfo.allocFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        deviceAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        deviceInstancesAllocation = std::make_unique<DeviceAllocation>(renderer->getDevice()->getDevice(), renderer->getDevice()->getGPU(), deviceAllocationInfo);

        //assign buffer memory and re-copy
        for(auto& renderPass : renderPasses)
        {
            //assign memory
            renderPass->hostInstancesBuffer->assignAllocation(hostInstancesAllocation.get());
            renderPass->hostInstancesDataBuffer->assignAllocation(hostInstancesAllocation.get());
            renderPass->deviceInstancesBuffer->assignAllocation(deviceInstancesAllocation.get());
            renderPass->deviceInstancesDataBuffer->assignAllocation(deviceInstancesAllocation.get());

            //re-copy
            memcpy(renderPass->hostInstancesBuffer->getHostDataPtr(), oldData.at(renderPass).oldInstanceDatas.data(), oldData.at(renderPass).oldInstanceDatas.size());
            renderPass->hostInstancesDataBuffer->newWrite(oldData.at(renderPass).oldInstanceMaterialDatas.data(), oldData.at(renderPass).oldInstanceMaterialDatas.size(), 0, NULL);
        }
    }

    void RenderPass::rebuildBuffers(VkDeviceSize newMaterialDataBufferSize)
    {
        VkDeviceSize newInstancesBufferSize = std::max((VkDeviceSize)(renderPassInstances.size() * sizeof(ModelInstance::RenderPassInstance) * instancesOverhead), (VkDeviceSize)(sizeof(ModelInstance::RenderPassInstance) * 64));
        VkDeviceSize newInstancesMaterialDataBufferSize = newMaterialDataBufferSize * instancesOverhead;

        BufferInfo hostInstancesBufferInfo = {};
        hostInstancesBufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
        hostInstancesBufferInfo.size = newInstancesBufferSize;
        hostInstancesBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
        hostInstancesBuffer = std::make_unique<Buffer>(rendererPtr->getDevice()->getDevice(), hostInstancesBufferInfo);

        BufferInfo hostInstancesMaterialDataBufferInfo = {};
        hostInstancesMaterialDataBufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
        hostInstancesMaterialDataBufferInfo.size = newInstancesMaterialDataBufferSize;
        hostInstancesMaterialDataBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
        hostInstancesDataBuffer = std::make_unique<FragmentableBuffer>(rendererPtr->getDevice()->getDevice(), hostInstancesMaterialDataBufferInfo);
        hostInstancesDataBuffer->setCompactionCallback([this](std::vector<CompactionResult> results){ handleMaterialDataCompaction(results); });

        BufferInfo deviceInstancesBufferInfo = {};
        deviceInstancesBufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
        deviceInstancesBufferInfo.size = newInstancesBufferSize;
        deviceInstancesBufferInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;
        deviceInstancesBuffer = std::make_unique<Buffer>(rendererPtr->getDevice()->getDevice(), deviceInstancesBufferInfo);

        BufferInfo deviceInstancesMaterialDataBufferInfo = {};
        deviceInstancesMaterialDataBufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
        deviceInstancesMaterialDataBufferInfo.size = newInstancesMaterialDataBufferSize;
        deviceInstancesMaterialDataBufferInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
        deviceInstancesDataBuffer = std::make_unique<Buffer>(rendererPtr->getDevice()->getDevice(), deviceInstancesMaterialDataBufferInfo);
    }

    void RenderPass::handleMaterialDataCompaction(std::vector<CompactionResult> results) //UNTESTED
    {
        for(const CompactionResult compactionResult : results)
        {
            for(ModelInstance* instance : renderPassInstances)
            {
                ModelInstance::RenderPassInstance& renderPassInstanceData = *((ModelInstance::RenderPassInstance*)hostInstancesBuffer->getHostDataPtr() + instance->renderPassSelfReferences.at(this).selfIndex);
                if(renderPassInstanceData.LODsMaterialDataOffset > compactionResult.location)
                {
                    renderPassInstanceData.LODsMaterialDataOffset -= compactionResult.shiftSize;
                }
            }
        }
    }

    void RenderPass::handleCommonMeshGroupResize(std::vector<ModelInstance*> invalidInstances)
    {
        for(ModelInstance* instance : invalidInstances)
        {
            //set new material data and copy it into the same location as the old data was (no size change so this works fine)
            instance->setRenderPassInstanceData(this);
            std::vector<char> materialData = instance->getRenderPassInstanceData(this);
            const ModelInstance::RenderPassInstance& renderPassInstanceData = *((ModelInstance::RenderPassInstance*)hostInstancesBuffer->getHostDataPtr() + instance->renderPassSelfReferences.at(this).selfIndex);
            memcpy((char*)(hostInstancesDataBuffer->getBuffer()->getHostDataPtr()) + renderPassInstanceData.LODsMaterialDataOffset, materialData.data(), materialData.size());
        }
    }

    void RenderPass::clearDrawCounts(VkCommandBuffer cmdBuffer)
    {
        //memory barrier
        VkMemoryBarrier2 preClearMemBarrier = {};
        preClearMemBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        preClearMemBarrier.pNext = NULL;
        preClearMemBarrier.srcAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
        preClearMemBarrier.srcStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
        preClearMemBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        preClearMemBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;

        VkDependencyInfo preClearDependency = {};
        preClearDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        preClearDependency.pNext = NULL;
        preClearDependency.memoryBarrierCount = 1;
        preClearDependency.pMemoryBarriers = &preClearMemBarrier;

        vkCmdPipelineBarrier2(cmdBuffer, &preClearDependency);

        //clear draw counts
        for(const auto& [material, materialInstanceNode] : renderTree) //material
        {
            for(const auto& [materialInstance, meshGroups] : materialInstanceNode.instances) //material instances
            {
                if(meshGroups)
                {
                    meshGroups->clearDrawCounts(cmdBuffer);
                }
            }
        }

        //memory barrier
        VkMemoryBarrier2 postClearMemBarrier = {};
        postClearMemBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        postClearMemBarrier.pNext = NULL;
        postClearMemBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        postClearMemBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        postClearMemBarrier.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        postClearMemBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

        VkDependencyInfo postClearDependency = {};
        postClearDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        postClearDependency.pNext = NULL;
        postClearDependency.memoryBarrierCount = 1;
        postClearDependency.pMemoryBarriers = &postClearMemBarrier;

        vkCmdPipelineBarrier2(cmdBuffer, &postClearDependency);
    }

    void RenderPass::render(const RenderPassSynchronizationInfo& syncInfo, const RenderPassInfo& renderPassInfo)
    {
        if(renderPassInstances.size())
        {
            //verify mesh group buffers
            handleCommonMeshGroupResize(CommonMeshGroup::verifyBuffersSize(rendererPtr));

            //----------PRE-PROCESS----------//

            //copy data
            VkBufferCopy instancesRegion = {};
            instancesRegion.dstOffset = 0;
            instancesRegion.srcOffset = 0;
            instancesRegion.size = renderPassInstances.size() * sizeof(ModelInstance::RenderPassInstance);
            
            SynchronizationInfo instancesBufferCopySync = {};
            instancesBufferCopySync.queueType = QueueType::TRANSFER;
            instancesBufferCopySync.waitPairs = {};
            instancesBufferCopySync.signalPairs = { { instancesBufferCopySemaphores.at(rendererPtr->getCurrentFrameIndex()), VK_PIPELINE_STAGE_2_TRANSFER_BIT }};
            instancesBufferCopySync.fence = VK_NULL_HANDLE;
            rendererPtr->recycleCommandBuffer(deviceInstancesBuffer->copyFromBufferRanges(*hostInstancesBuffer.get(), { instancesRegion }, instancesBufferCopySync));

            VkBufferCopy materialDataRegion = {};
            materialDataRegion.dstOffset = 0;
            materialDataRegion.srcOffset = 0;
            materialDataRegion.size = hostInstancesDataBuffer->getStackLocation();
            
            SynchronizationInfo materialDataCopySync = {};
            materialDataCopySync.queueType = QueueType::TRANSFER;
            materialDataCopySync.waitPairs = {};
            materialDataCopySync.signalPairs = { { materialDataBufferCopySemaphores.at(rendererPtr->getCurrentFrameIndex()), VK_PIPELINE_STAGE_2_TRANSFER_BIT }};
            materialDataCopySync.fence = VK_NULL_HANDLE;
            rendererPtr->recycleCommandBuffer(deviceInstancesDataBuffer->copyFromBufferRanges(*hostInstancesDataBuffer->getBuffer(), { materialDataRegion }, materialDataCopySync));

            //compute shader
            std::vector<SemaphorePair> waitPairs = syncInfo.preprocessWaitPairs;
            waitPairs.push_back({ instancesBufferCopySemaphores.at(rendererPtr->getCurrentFrameIndex()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT });
            waitPairs.push_back({ materialDataBufferCopySemaphores.at(rendererPtr->getCurrentFrameIndex()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT });

            SynchronizationInfo preprocessSyncInfo = {};
            preprocessSyncInfo.queueType = QueueType::COMPUTE;
            preprocessSyncInfo.waitPairs = waitPairs;
            preprocessSyncInfo.signalPairs = { { preprocessSignalSemaphores.at(rendererPtr->getCurrentFrameIndex()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT } };
            preprocessSyncInfo.fence = preprocessFence;

            rendererPtr->getRasterPreprocessPipeline()->submit(preprocessSyncInfo, *this);
            rendererPtr->preprocessFences.push_back(preprocessFence);

            //----------RENDER PASS----------//

            //command buffer
            VkCommandBufferBeginInfo commandInfo;
            commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            commandInfo.pNext = NULL;
            commandInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            commandInfo.pInheritanceInfo = NULL;

            VkCommandBuffer graphicsCmdBuffer = Commands::getCommandBuffer(rendererPtr->getDevice()->getDevice(), QueueType::GRAPHICS);
            vkBeginCommandBuffer(graphicsCmdBuffer, &commandInfo);

            //pre-render barriers
            if(renderPassInfo.preRenderBarriers)
            {
                vkCmdPipelineBarrier2(graphicsCmdBuffer, renderPassInfo.preRenderBarriers);
            }

            //rendering
            VkRenderingInfoKHR renderInfo = {};
            renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
            renderInfo.pNext = NULL;
            renderInfo.flags = 0;
            renderInfo.renderArea = renderPassInfo.renderArea;
            renderInfo.layerCount = 1;
            renderInfo.viewMask = 0;
            renderInfo.colorAttachmentCount = renderPassInfo.colorAttachments.size();
            renderInfo.pColorAttachments = renderPassInfo.colorAttachments.data();
            renderInfo.pDepthAttachment = renderPassInfo.depthAttachment;
            renderInfo.pStencilAttachment = renderPassInfo.stencilAttachment;

            vkCmdBeginRendering(graphicsCmdBuffer, &renderInfo);

            //scissors (plural) and viewports
            vkCmdSetViewportWithCount(graphicsCmdBuffer, renderPassInfo.viewports.size(), renderPassInfo.viewports.data());
            vkCmdSetScissorWithCount(graphicsCmdBuffer, renderPassInfo.scissors.size(), renderPassInfo.scissors.data());

            //MSAA samples
            vkCmdSetRasterizationSamplesEXT(graphicsCmdBuffer, rendererPtr->getRendererState()->msaaSamples);

            //----------PRE-PASS----------//

            if(renderPassInfo.renderPrepass && defaultPrepassMaterialInstancePtr)
            {
                vkCmdSetDepthCompareOp(graphicsCmdBuffer, VK_COMPARE_OP_LESS);

                //bind material
                Material* defaultPrepassMaterial = (Material*)defaultPrepassMaterialInstancePtr->getBaseMaterialPtr();
                defaultPrepassMaterial->bind(graphicsCmdBuffer, rendererPtr->getCurrentFrameIndex());
                defaultPrepassMaterialInstancePtr->bind(graphicsCmdBuffer, rendererPtr->getCurrentFrameIndex());
                
                //record draw commands
                for(const auto& [material, materialInstanceNode] : renderTree) //material
                {
                    for(const auto& [materialInstance, meshGroups] : materialInstanceNode.instances) //material instances
                    {
                        if(meshGroups)
                        {
                            meshGroups->draw(graphicsCmdBuffer, rendererPtr->getCurrentFrameIndex(), *defaultPrepassMaterial->getRasterPipeline());
                        }
                    }
                }

                vkCmdSetDepthCompareOp(graphicsCmdBuffer, VK_COMPARE_OP_EQUAL);
            }
            else
            {
                vkCmdSetDepthCompareOp(graphicsCmdBuffer, VK_COMPARE_OP_LESS);
            }

            //----------MAIN PASS----------//

            //record draw commands
            for(const auto& [material, materialInstanceNode] : renderTree) //material
            {
                material->bind(graphicsCmdBuffer, rendererPtr->getCurrentFrameIndex());
                for(const auto& [materialInstance, meshGroups] : materialInstanceNode.instances) //material instances
                {
                    if(meshGroups)
                    {
                        materialInstance->bind(graphicsCmdBuffer, rendererPtr->getCurrentFrameIndex());
                        meshGroups->draw(graphicsCmdBuffer, rendererPtr->getCurrentFrameIndex(), *material->getRasterPipeline());
                    }
                }
            }

            //end rendering
            vkCmdEndRendering(graphicsCmdBuffer);

            //clear draw counts
            clearDrawCounts(graphicsCmdBuffer);

            //post-render barriers
            if(renderPassInfo.postRenderBarriers)
            {
                vkCmdPipelineBarrier2(graphicsCmdBuffer, renderPassInfo.postRenderBarriers);
            }

            vkEndCommandBuffer(graphicsCmdBuffer);

            //submit rendering to GPU   
            SynchronizationInfo graphicsSyncInfo = {};
            graphicsSyncInfo.queueType = QueueType::GRAPHICS;
            graphicsSyncInfo.waitPairs = syncInfo.renderWaitPairs;
            graphicsSyncInfo.waitPairs.push_back({ preprocessSignalSemaphores.at(rendererPtr->getCurrentFrameIndex()), VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT });
            graphicsSyncInfo.signalPairs = syncInfo.renderSignalPairs;
            graphicsSyncInfo.fence = syncInfo.renderSignalFence;

            Commands::submitToQueue(rendererPtr->getDevice()->getDevice(), graphicsSyncInfo, { graphicsCmdBuffer });

            CommandBuffer commandBuffer = { graphicsCmdBuffer, QueueType::GRAPHICS };
            rendererPtr->recycleCommandBuffer(commandBuffer);
        }
    }

    void RenderPass::addInstance(ModelInstance *instance, std::vector<std::unordered_map<uint32_t, MaterialInstance *>> materials)
    {
        //material data
        materials.resize(instance->getParentModelPtr()->getLODs().size());
        for(uint32_t lodIndex = 0; lodIndex < instance->getParentModelPtr()->getLODs().size(); lodIndex++)
        {
            for(uint32_t matIndex = 0; matIndex < instance->getParentModelPtr()->getLODs().at(lodIndex).meshMaterialData.size(); matIndex++) //iterate materials in LOD
            {
                //----------MAIN MATERIALS----------//

                //get material instance
                MaterialInstance* materialInstance;
                if(materials.at(lodIndex).count(matIndex) && materials.at(lodIndex).at(matIndex)) //check if slot is initialized and not NULL
                {
                    materialInstance = materials.at(lodIndex).at(matIndex);
                }
                else //use default material if one isn't selected
                {
                    materialInstance = defaultMaterialInstancePtr;
                }

                //get meshes using same material
                std::vector<LODMesh const*> similarMeshes;
                for(uint32_t meshIndex = 0; meshIndex < instance->getParentModelPtr()->getLODs().at(lodIndex).meshMaterialData.at(matIndex).size(); meshIndex++)
                {
                    similarMeshes.push_back(&instance->getParentModelPtr()->getLODs().at(lodIndex).meshMaterialData.at(matIndex).at(meshIndex));
                }

                //check if mesh group class is created
                if(!renderTree[(Material*)materialInstance->getBaseMaterialPtr()].instances.count(materialInstance))
                {
                    renderTree[(Material*)materialInstance->getBaseMaterialPtr()].instances[materialInstance] = 
                        std::make_unique<CommonMeshGroup>(rendererPtr, this);
                }

                //add references
                renderTree[(Material*)materialInstance->getBaseMaterialPtr()].instances[materialInstance]->addInstanceMeshes(instance, similarMeshes);

                instance->renderPassSelfReferences[this].meshGroupReferences[&instance->getParentModelPtr()->getLODs().at(lodIndex).meshMaterialData.at(matIndex)] = 
                    renderTree.at((Material*)materialInstance->getBaseMaterialPtr()).instances.at(materialInstance).get();
            }
        }

        //add reference
        instance->renderPassSelfReferences.at(this).selfIndex = renderPassInstances.size();
        renderPassInstances.push_back(instance);

        //check size
        if(hostInstancesBuffer->getSize() < renderPassInstances.size() * sizeof(ModelInstance::RenderPassInstance))
        {
            rebuildAllocationsAndBuffers(rendererPtr);
        }

        //copy data into buffer
        instance->setRenderPassInstanceData(this);
        std::vector<char> materialData = instance->getRenderPassInstanceData(this);

        VkDeviceSize materialDataLocation = 0;
        if(hostInstancesDataBuffer->newWrite(materialData.data(), materialData.size(), 8, &materialDataLocation) == FragmentableBuffer::OUT_OF_MEMORY)
        {
            rebuildAllocationsAndBuffers(rendererPtr);
            hostInstancesDataBuffer->newWrite(materialData.data(), materialData.size(), 8, &materialDataLocation);
        }

        ModelInstance::RenderPassInstance shaderData = {};
        shaderData.modelInstanceIndex = instance->rendererSelfIndex;
        shaderData.LODsMaterialDataOffset = materialDataLocation;
        shaderData.isVisible = true;

        memcpy((ModelInstance::RenderPassInstance*)hostInstancesBuffer->getHostDataPtr() + instance->renderPassSelfReferences.at(this).selfIndex, &shaderData, sizeof(ModelInstance::RenderPassInstance));

        //reset data
        instance->setRenderPassInstanceData(this);
        materialData = instance->getRenderPassInstanceData(this);
        memcpy((char*)(hostInstancesDataBuffer->getBuffer()->getHostDataPtr()) + shaderData.LODsMaterialDataOffset, materialData.data(), materialData.size());
    }

    void RenderPass::removeInstance(ModelInstance *instance)
    {
        for(auto& [mesh, reference] : instance->renderPassSelfReferences.at(this).meshGroupReferences)
        {
            reference->removeInstanceMeshes(instance);
        }
        instance->renderPassSelfReferences.at(this).meshGroupReferences.clear();

        //remove reference
        if(renderPassInstances.size() > 1)
        {
            uint32_t& selfReference = instance->renderPassSelfReferences.at(this).selfIndex;
            renderPassInstances.at(selfReference) = renderPassInstances.back();
            renderPassInstances.at(selfReference)->renderPassSelfReferences.at(this).selfIndex = selfReference;
            renderPassInstances.pop_back();
        }
        else
        {
            renderPassInstances.clear();
        }

        instance->renderPassSelfReferences.erase(this);
    }
}