#include "RenderPass.h"
#include "PaperRenderer.h"

#include <iostream>

namespace PaperRenderer
{
    //----------PREPROCESS PIPELINES DEFINITIONS----------//

    RasterPreprocessPipeline::RasterPreprocessPipeline(RenderEngine* renderer, std::string fileDir)
        :rendererPtr(renderer)
    {
        //preprocess uniform buffers
        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            PaperMemory::BufferInfo preprocessBuffersInfo = {};
            preprocessBuffersInfo.usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR;
            preprocessBuffersInfo.size = sizeof(UBOInputData);
            preprocessBuffersInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
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

        VkDescriptorSetLayoutBinding debugBufferDescriptor = {};
        debugBufferDescriptor.binding = 3;
        debugBufferDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        debugBufferDescriptor.descriptorCount = 1;
        debugBufferDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        descriptorSets[0].descriptorBindings[3] = debugBufferDescriptor;

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

    void RasterPreprocessPipeline::submit(const PaperMemory::SynchronizationInfo& syncInfo, const RenderPass& renderPass)
    {
        UBOInputData uboInputData = {};
        uboInputData.camPos = glm::vec4(renderPass.cameraPtr->getTranslation().position, 1.0f);
        uboInputData.projection = renderPass.cameraPtr->getProjection();
        uboInputData.view = renderPass.cameraPtr->getViewMatrix();
        uboInputData.materialDataPtr = renderPass.deviceInstancesDataBuffer->getBufferDeviceAddress();
        uboInputData.modelDataPtr = rendererPtr->deviceModelDataBuffer->getBufferDeviceAddress();
        uboInputData.objectCount = renderPass.renderPassInstances.size();

        PaperMemory::BufferWrite write = {};
        write.data = &uboInputData;
        write.size = sizeof(UBOInputData);
        write.offset = 0;

        uniformBuffers.at(*rendererPtr->getCurrentFramePtr())->writeToBuffer({ write });

        //set0 - binding 0: UBO input data
        VkDescriptorBufferInfo bufferWrite0Info = {};
        bufferWrite0Info.buffer = uniformBuffers.at(*rendererPtr->getCurrentFramePtr())->getBuffer();
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

        //set0 - binding 3: debug buffer
        VkDescriptorBufferInfo bufferWrite3Info = {};
        bufferWrite3Info.buffer = renderPass.debugBuffer->getBuffer();
        bufferWrite3Info.offset = 0;
        bufferWrite3Info.range = 40000;

        BuffersDescriptorWrites bufferWrite3 = {};
        bufferWrite3.binding = 3;
        bufferWrite3.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bufferWrite3.infos = { bufferWrite3Info };

        VkCommandBufferBeginInfo commandInfo;
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        commandInfo.pNext = NULL;
        commandInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        commandInfo.pInheritanceInfo = NULL;

        VkCommandBuffer cullingCmdBuffer = PaperMemory::Commands::getCommandBuffer(PipelineBuilder::getRendererInfo().devicePtr->getDevice(), syncInfo.queueType);

        vkBeginCommandBuffer(cullingCmdBuffer, &commandInfo);
        bind(cullingCmdBuffer);

        DescriptorWrites descriptorWritesInfo = {};
        descriptorWritesInfo.bufferWrites = { bufferWrite0, bufferWrite1, bufferWrite2, bufferWrite3 };
        descriptorWrites[0] = descriptorWritesInfo;
        writeDescriptorSet(cullingCmdBuffer, *rendererPtr->getCurrentFramePtr(), 0);

        //dispatch
        workGroupSizes.x = ((rendererPtr->getModelInstanceReferences().size()) / 128) + 1;
        dispatch(cullingCmdBuffer);
        
        vkEndCommandBuffer(cullingCmdBuffer);

        //submit
        PaperMemory::Commands::submitToQueue(PipelineBuilder::getRendererInfo().devicePtr->getDevice(), syncInfo, { cullingCmdBuffer });

        PaperMemory::CommandBuffer commandBuffer = { cullingCmdBuffer, syncInfo.queueType };
        rendererPtr->recycleCommandBuffer(commandBuffer);
    }

    //----------RENDER PASS DEFINITIONS----------//

    std::unique_ptr<PaperMemory::DeviceAllocation> RenderPass::hostInstancesAllocation;
    std::unique_ptr<PaperMemory::DeviceAllocation> RenderPass::deviceInstancesAllocation;
    std::list<RenderPass*> RenderPass::renderPasses;

    RenderPass::RenderPass(RenderEngine *renderer, Camera *camera, Material *defaultMaterial, MaterialInstance *defaultMaterialInstance, RenderPassInfo const *renderPassInfo)
        :rendererPtr(renderer),
        cameraPtr(camera),
        defaultMaterialPtr(defaultMaterial),
        defaultMaterialInstancePtr(defaultMaterialInstance),
        renderPassInfoPtr(renderPassInfo)
    {
        renderPasses.push_back(this);

        instancesBufferCopySemaphores.resize(PaperMemory::Commands::getFrameCount());
        materialDataBufferCopySemaphores.resize(PaperMemory::Commands::getFrameCount());
        preprocessSignalSemaphores.resize(PaperMemory::Commands::getFrameCount());
        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            instancesBufferCopySemaphores.at(i) = PaperMemory::Commands::getSemaphore(rendererPtr->getDevice()->getDevice());
            materialDataBufferCopySemaphores.at(i) = PaperMemory::Commands::getSemaphore(rendererPtr->getDevice()->getDevice());
            preprocessSignalSemaphores.at(i) = PaperMemory::Commands::getSemaphore(rendererPtr->getDevice()->getDevice());
        }

        rebuildAllocationsAndBuffers(rendererPtr);
    }

    RenderPass::~RenderPass()
    {
        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            vkDestroySemaphore(rendererPtr->getDevice()->getDevice(), instancesBufferCopySemaphores.at(i), nullptr);
            vkDestroySemaphore(rendererPtr->getDevice()->getDevice(), materialDataBufferCopySemaphores.at(i), nullptr);
            vkDestroySemaphore(rendererPtr->getDevice()->getDevice(), preprocessSignalSemaphores.at(i), nullptr);
        }

        renderPasses.remove(this);
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

            newHostSize += PaperMemory::DeviceAllocation::padToMultiple(renderPass->hostInstancesBuffer->getMemoryRequirements().size, renderPass->hostInstancesBuffer->getMemoryRequirements().alignment);
            newHostSize += PaperMemory::DeviceAllocation::padToMultiple(renderPass->hostInstancesDataBuffer->getBuffer()->getMemoryRequirements().size, renderPass->hostInstancesDataBuffer->getBuffer()->getMemoryRequirements().alignment);

            newDeviceSize += PaperMemory::DeviceAllocation::padToMultiple(renderPass->deviceInstancesBuffer->getMemoryRequirements().size, renderPass->deviceInstancesBuffer->getMemoryRequirements().alignment);
            newDeviceSize += PaperMemory::DeviceAllocation::padToMultiple(renderPass->deviceInstancesDataBuffer->getMemoryRequirements().size, renderPass->deviceInstancesDataBuffer->getMemoryRequirements().alignment);
            newDeviceSize += PaperMemory::DeviceAllocation::padToMultiple(renderPass->debugBuffer->getMemoryRequirements().size, renderPass->debugBuffer->getMemoryRequirements().alignment);
        }

        //rebuild allocations
        PaperMemory::DeviceAllocationInfo hostAllocationInfo = {};
        hostAllocationInfo.allocationSize = newHostSize;
        hostAllocationInfo.allocFlags = 0;
        hostAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        hostInstancesAllocation = std::make_unique<PaperMemory::DeviceAllocation>(renderer->getDevice()->getDevice(), renderer->getDevice()->getGPU(), hostAllocationInfo);

        PaperMemory::DeviceAllocationInfo deviceAllocationInfo = {};
        deviceAllocationInfo.allocationSize = newDeviceSize;
        deviceAllocationInfo.allocFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        deviceAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        deviceInstancesAllocation = std::make_unique<PaperMemory::DeviceAllocation>(renderer->getDevice()->getDevice(), renderer->getDevice()->getGPU(), deviceAllocationInfo);

        //assign buffer memory and re-copy
        for(auto& renderPass : renderPasses)
        {
            //assign memory
            renderPass->hostInstancesBuffer->assignAllocation(hostInstancesAllocation.get());
            renderPass->hostInstancesDataBuffer->assignAllocation(hostInstancesAllocation.get());
            renderPass->deviceInstancesBuffer->assignAllocation(deviceInstancesAllocation.get());
            renderPass->deviceInstancesDataBuffer->assignAllocation(deviceInstancesAllocation.get());
            renderPass->debugBuffer->assignAllocation(deviceInstancesAllocation.get());

            //re-copy
            memcpy(renderPass->hostInstancesBuffer->getHostDataPtr(), oldData.at(renderPass).oldInstanceDatas.data(), oldData.at(renderPass).oldInstanceDatas.size());
            renderPass->hostInstancesDataBuffer->newWrite(oldData.at(renderPass).oldInstanceMaterialDatas.data(), oldData.at(renderPass).oldInstanceMaterialDatas.size(), NULL);
        }
    }

    void RenderPass::rebuildBuffers(VkDeviceSize newMaterialDataBufferSize)
    {
        VkDeviceSize newInstancesBufferSize = std::max((VkDeviceSize)(renderPassInstances.size() * sizeof(ModelInstance::RenderPassInstance) * instancesOverhead), (VkDeviceSize)(sizeof(ModelInstance::RenderPassInstance) * 64));
        VkDeviceSize newInstancesMaterialDataBufferSize = newMaterialDataBufferSize * instancesOverhead;

        PaperMemory::BufferInfo hostInstancesBufferInfo = {};
        hostInstancesBufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
        hostInstancesBufferInfo.size = newInstancesBufferSize;
        hostInstancesBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
        hostInstancesBuffer = std::make_unique<PaperMemory::Buffer>(rendererPtr->getDevice()->getDevice(), hostInstancesBufferInfo);

        PaperMemory::BufferInfo hostInstancesMaterialDataBufferInfo = {};
        hostInstancesMaterialDataBufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
        hostInstancesMaterialDataBufferInfo.size = newInstancesMaterialDataBufferSize;
        hostInstancesMaterialDataBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
        hostInstancesDataBuffer = std::make_unique<PaperMemory::FragmentableBuffer>(rendererPtr->getDevice()->getDevice(), hostInstancesMaterialDataBufferInfo);
        hostInstancesDataBuffer->setCompactionCallback([this](std::vector<PaperMemory::CompactionResult> results){ handleMaterialDataCompaction(results); });

        PaperMemory::BufferInfo deviceInstancesBufferInfo = {};
        deviceInstancesBufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
        deviceInstancesBufferInfo.size = newInstancesBufferSize;
        deviceInstancesBufferInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;
        deviceInstancesBuffer = std::make_unique<PaperMemory::Buffer>(rendererPtr->getDevice()->getDevice(), deviceInstancesBufferInfo);

        PaperMemory::BufferInfo deviceInstancesMaterialDataBufferInfo = {};
        deviceInstancesMaterialDataBufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
        deviceInstancesMaterialDataBufferInfo.size = newInstancesMaterialDataBufferSize;
        deviceInstancesMaterialDataBufferInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
        deviceInstancesDataBuffer = std::make_unique<PaperMemory::Buffer>(rendererPtr->getDevice()->getDevice(), deviceInstancesMaterialDataBufferInfo);

        //DEBUG BUFFER
        PaperMemory::BufferInfo debugBufferInfo = {};
        debugBufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
        debugBufferInfo.size = 40000;
        debugBufferInfo.usageFlags = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;
        debugBuffer = std::make_unique<PaperMemory::Buffer>(rendererPtr->getDevice()->getDevice(), debugBufferInfo);
    }

    void RenderPass::handleMaterialDataCompaction(std::vector<PaperMemory::CompactionResult> results)
    {
        for(ModelInstance* instance : renderPassInstances)
        {
            //BIG OL TODO
        }
    }

    void RenderPass::handleCommonMeshGroupResize(std::vector<ModelInstance*> invalidInstances)
    {
        for(ModelInstance* instance : invalidInstances)
        {
            //get new material data and copy it into the same location as the old data was (no size change so this works fine)
            std::vector<char> materialData = instance->getRenderPassInstanceData(this);
            const ModelInstance::RenderPassInstance& renderPassInstanceData = *((ModelInstance::RenderPassInstance*)hostInstancesBuffer->getHostDataPtr() + instance->renderPassSelfReferences.at(this).selfIndex);
            memcpy((char*)(hostInstancesDataBuffer->getBuffer()->getHostDataPtr()) + renderPassInstanceData.LODsMaterialDataOffset, materialData.data(), materialData.size());
        }
    }

    void RenderPass::render(const RenderPassSynchronizationInfo& syncInfo)
    {
        if(renderPassInstances.size())
        {
            //----------PRE-PROCESS----------//

            //copy data
            VkBufferCopy instancesRegion = {};
            instancesRegion.dstOffset = 0;
            instancesRegion.srcOffset = 0;
            instancesRegion.size = renderPassInstances.size() * sizeof(ModelInstance::RenderPassInstance);
            
            PaperMemory::SynchronizationInfo instancesBufferCopySync = {};
            instancesBufferCopySync.queueType = PaperMemory::QueueType::TRANSFER;
            instancesBufferCopySync.waitPairs = {};
            instancesBufferCopySync.signalPairs = { { instancesBufferCopySemaphores.at(*rendererPtr->getCurrentFramePtr()), VK_PIPELINE_STAGE_2_TRANSFER_BIT }};
            instancesBufferCopySync.fence = VK_NULL_HANDLE;
            rendererPtr->recycleCommandBuffer(deviceInstancesBuffer->copyFromBufferRanges(*hostInstancesBuffer.get(), { instancesRegion }, instancesBufferCopySync));

            VkBufferCopy materialDataRegion = {};
            materialDataRegion.dstOffset = 0;
            materialDataRegion.srcOffset = 0;
            materialDataRegion.size = hostInstancesDataBuffer->getStackLocation();
            
            PaperMemory::SynchronizationInfo materialDataCopySync = {};
            materialDataCopySync.queueType = PaperMemory::QueueType::TRANSFER;
            materialDataCopySync.waitPairs = {};
            materialDataCopySync.signalPairs = { { materialDataBufferCopySemaphores.at(*rendererPtr->getCurrentFramePtr()), VK_PIPELINE_STAGE_2_TRANSFER_BIT }};
            materialDataCopySync.fence = VK_NULL_HANDLE;
            rendererPtr->recycleCommandBuffer(deviceInstancesDataBuffer->copyFromBufferRanges(*hostInstancesDataBuffer->getBuffer(), { materialDataRegion }, materialDataCopySync));

            //compute shader
            std::vector<PaperMemory::SemaphorePair> waitPairs = syncInfo.preprocessWaitPairs;
            waitPairs.push_back({ instancesBufferCopySemaphores.at(*rendererPtr->getCurrentFramePtr()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT });
            waitPairs.push_back({ materialDataBufferCopySemaphores.at(*rendererPtr->getCurrentFramePtr()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT });

            PaperMemory::SynchronizationInfo preprocessSyncInfo = {};
            preprocessSyncInfo.queueType = PaperMemory::QueueType::COMPUTE;
            preprocessSyncInfo.waitPairs = waitPairs;
            preprocessSyncInfo.signalPairs = { { preprocessSignalSemaphores.at(*rendererPtr->getCurrentFramePtr()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT } };

            rendererPtr->getRasterPreprocessPipeline()->submit(preprocessSyncInfo, *this);

            //----------RENDER PASS----------//

            //command buffer
            VkCommandBufferBeginInfo commandInfo;
            commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            commandInfo.pNext = NULL;
            commandInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            commandInfo.pInheritanceInfo = NULL;

            VkCommandBuffer graphicsCmdBuffer = PaperMemory::Commands::getCommandBuffer(rendererPtr->getDevice()->getDevice(), PaperMemory::QueueType::GRAPHICS);
            vkBeginCommandBuffer(graphicsCmdBuffer, &commandInfo);

            //pre-render barriers
            if(renderPassInfoPtr->preRenderBarriers) vkCmdPipelineBarrier2(graphicsCmdBuffer, renderPassInfoPtr->preRenderBarriers);

            //rendering
            VkRenderingInfoKHR renderInfo = {};
            renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
            renderInfo.pNext = NULL;
            renderInfo.flags = 0;
            renderInfo.renderArea = renderPassInfoPtr->renderArea;
            renderInfo.layerCount = 1;
            renderInfo.viewMask = 0;
            renderInfo.colorAttachmentCount = renderPassInfoPtr->colorAttachments.size();
            renderInfo.pColorAttachments = renderPassInfoPtr->colorAttachments.data();
            renderInfo.pDepthAttachment = renderPassInfoPtr->depthAttachment;
            renderInfo.pStencilAttachment = renderPassInfoPtr->stencilAttachment;

            vkCmdBeginRendering(graphicsCmdBuffer, &renderInfo);

            //scissors (plural) and viewports
            vkCmdSetViewportWithCount(graphicsCmdBuffer, renderPassInfoPtr->viewports.size(), renderPassInfoPtr->viewports.data());
            vkCmdSetScissorWithCount(graphicsCmdBuffer, renderPassInfoPtr->scissors.size(), renderPassInfoPtr->scissors.data());

            //record draw commands
            for(const auto& [material, materialInstanceNode] : renderTree) //material
            {
                material->bind(graphicsCmdBuffer, *rendererPtr->getCurrentFramePtr());
                for(const auto& [materialInstance, meshGroups] : materialInstanceNode.instances) //material instances
                {
                    if(meshGroups)
                    {
                        materialInstance->bind(graphicsCmdBuffer, *rendererPtr->getCurrentFramePtr());
                        meshGroups->draw(graphicsCmdBuffer, *rendererPtr->getCurrentFramePtr());
                    }
                }
            }

            //end rendering
            vkCmdEndRendering(graphicsCmdBuffer);

            //clear draw counts
            for(const auto& [material, materialInstanceNode] : renderTree) //material
            {
                material->bind(graphicsCmdBuffer, *rendererPtr->getCurrentFramePtr());
                for(const auto& [materialInstance, meshGroups] : materialInstanceNode.instances) //material instances
                {
                    if(meshGroups)
                    {
                        materialInstance->bind(graphicsCmdBuffer, *rendererPtr->getCurrentFramePtr());
                        meshGroups->clearDrawCounts(graphicsCmdBuffer);
                    }
                }
            }

            //post-render barriers
            if(renderPassInfoPtr->postRenderBarriers) vkCmdPipelineBarrier2(graphicsCmdBuffer, renderPassInfoPtr->postRenderBarriers);

            vkEndCommandBuffer(graphicsCmdBuffer);

            //submit rendering to GPU   
            PaperMemory::SynchronizationInfo graphicsSyncInfo = {};
            graphicsSyncInfo.queueType = PaperMemory::QueueType::GRAPHICS;
            graphicsSyncInfo.waitPairs = syncInfo.renderWaitPairs;
            graphicsSyncInfo.waitPairs.push_back({ preprocessSignalSemaphores.at(*rendererPtr->getCurrentFramePtr()), VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT });
            graphicsSyncInfo.signalPairs = syncInfo.renderSignalPairs; //{ { renderSemaphores.at(currentImage), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT } };
            graphicsSyncInfo.fence = syncInfo.renderSignalFence;

            PaperMemory::Commands::submitToQueue(rendererPtr->getDevice()->getDevice(), graphicsSyncInfo, { graphicsCmdBuffer });

            PaperMemory::CommandBuffer commandBuffer = { graphicsCmdBuffer, PaperMemory::QueueType::GRAPHICS };
            rendererPtr->recycleCommandBuffer(commandBuffer);
        }
    }

    void RenderPass::addInstance(ModelInstance *instance, std::vector<std::unordered_map<uint32_t, MaterialInstance *>> materials)
    {
        //add reference
        instance->renderPassSelfReferences[this].selfIndex = renderPassInstances.size();
        instance->setRenderPassInstanceData(this);
        renderPassInstances.push_back(instance);

        //check size
        if(hostInstancesBuffer->getSize() < (renderPassInstances.size() + 3) * sizeof(ModelInstance::RenderPassInstance))
        {
            rebuildAllocationsAndBuffers(rendererPtr);
        }

        //copy data into buffer
        std::vector<char> materialData = instance->getRenderPassInstanceData(this);

        VkDeviceSize materialDataLocation = 0;
        if(hostInstancesDataBuffer->newWrite(materialData.data(), materialData.size(), &materialDataLocation) == PaperMemory::FragmentableBuffer::OUT_OF_MEMORY)
        {
            rebuildAllocationsAndBuffers(rendererPtr);
            hostInstancesDataBuffer->newWrite(materialData.data(), materialData.size(), &materialDataLocation);
        }

        ModelInstance::RenderPassInstance shaderData = {};
        shaderData.modelInstanceIndex = instance->rendererSelfIndex;
        shaderData.LODsMaterialDataOffset = materialDataLocation;
        shaderData.isVisible = true;

        if(renderPassInstances.size() == 95)
        {
            int a = 0;
        }

        memcpy((ModelInstance::RenderPassInstance*)hostInstancesBuffer->getHostDataPtr() + instance->renderPassSelfReferences.at(this).selfIndex, &shaderData, sizeof(ModelInstance::ShaderModelInstance));
        
        if(((ModelInstance::RenderPassInstance*)hostInstancesBuffer->getHostDataPtr())->modelInstanceIndex != 0)
        {
            int a = 0;
        }
        
        //material data
        materials.resize(instance->getParentModelPtr()->getLODs().size());
        for(uint32_t lodIndex = 0; lodIndex < instance->getParentModelPtr()->getLODs().size(); lodIndex++)
        {
            for(uint32_t matIndex = 0; matIndex < instance->getParentModelPtr()->getLODs().at(lodIndex).meshMaterialData.size(); matIndex++) //iterate materials in LOD
            {
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
                        std::make_unique<CommonMeshGroup>(rendererPtr, this, materialInstance->getBaseMaterialPtr()->getRasterPipeline());
                    
                    renderTree.at((Material*)materialInstance->getBaseMaterialPtr()).instances.at(materialInstance)->setBufferRebuildCallback([this](std::vector<ModelInstance*> instances) { handleCommonMeshGroupResize(instances); });
                }

                //add reference
                renderTree[(Material*)materialInstance->getBaseMaterialPtr()].instances[materialInstance]->addInstanceMeshes(instance, similarMeshes);
            }
        }

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