#include "PaperRenderer.h"

#include <iostream>
#include <math.h>

namespace PaperRenderer
{
    RenderEngine::RenderEngine(RendererCreationStruct creationInfo)
        :appName(creationInfo.appName),
        shadersDir(creationInfo.shadersDir),
        device(creationInfo.appName),
        window(WindowInformation(creationInfo.resX, creationInfo.resY, false), creationInfo.appName, &device),
        swapchain(&device, &window, false),
        descriptors(&device),
        pipelineBuilder(&device, &descriptors, &swapchain),
        rasterPreprocessPipeline(this, creationInfo.shadersDir)
    {
        //synchronization and cmd buffers
        bufferCopyFences.resize(PaperMemory::Commands::getFrameCount());
        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            bufferCopyFences.at(i) = PaperMemory::Commands::getUnsignaledFence(device.getDevice());
        }
        usedCmdBuffers.resize(PaperMemory::Commands::getFrameCount());

        rebuildBuffersAndAllocations();

        //finish up
        vkDeviceWaitIdle(device.getDevice());
        std::cout << "Renderer initialization complete" << std::endl;
    }

    RenderEngine::~RenderEngine()
    {
        vkDeviceWaitIdle(device.getDevice());
        
        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            vkDestroyFence(device.getDevice(), bufferCopyFences.at(i), nullptr);

            //free cmd buffers
            PaperMemory::Commands::freeCommandBuffers(device.getDevice(), usedCmdBuffers.at(i));
            usedCmdBuffers.at(i).clear();
        }
    }

    void RenderEngine::rebuildBuffersAndAllocations()
    {
        //copy old data into temporary variables and "delete" buffers
        std::vector<char> oldModelInstancesData(renderingModelInstances.size() * sizeof(ModelInstance::ShaderModelInstance));
        if(hostInstancesDataBuffer)
        {
            memcpy(oldModelInstancesData.data(), hostInstancesDataBuffer->getHostDataPtr(), oldModelInstancesData.size());
            hostInstancesDataBuffer.reset();
        }
        
        VkDeviceSize oldModelsDataSize = 4096;
        if(hostModelDataBuffer) oldModelsDataSize = hostModelDataBuffer->getStackLocation();
        std::vector<char> oldModelsData;
        if(hostModelDataBuffer)
        {
            oldModelsData.resize(oldModelsDataSize);
            memcpy(oldModelsData.data(), hostModelDataBuffer->getBufferPtr(), oldModelsData.size());
            hostModelDataBuffer.reset();
        }
        
        //delete old allocations (technically not needed)
        hostDataAllocation.reset();
        deviceDataAllocation.reset();

        //rebuild buffers
        rebuildInstancesbuffers();
        rebuildModelDataBuffers(oldModelsDataSize);

        //get allocation size requirements
        VkDeviceSize hostAllocationSize = 0;
        hostAllocationSize += std::max(hostInstancesDataBuffer->getMemoryRequirements().size, (VkDeviceSize)sizeof(ModelInstance::ShaderModelInstance) * 128);
        hostAllocationSize = PaperMemory::DeviceAllocation::padToMultiple(hostAllocationSize, hostInstancesDataBuffer->getMemoryRequirements().alignment); //pad buffer to allocation requirement
        hostAllocationSize += hostModelDataBuffer->getBufferPtr()->getMemoryRequirements().size;

        VkDeviceSize deviceAllocationSize = 0;
        deviceAllocationSize += std::max(deviceInstancesDataBuffer->getMemoryRequirements().size, (VkDeviceSize)sizeof(ModelInstance::ShaderModelInstance) * 128);
        deviceAllocationSize = PaperMemory::DeviceAllocation::padToMultiple(deviceAllocationSize, deviceInstancesDataBuffer->getMemoryRequirements().alignment); //pad buffer to allocation requirement
        deviceAllocationSize += deviceModelDataBuffer->getMemoryRequirements().size;

        //create new allocations
        PaperMemory::DeviceAllocationInfo hostAllocationInfo = {};
        hostAllocationInfo.allocationSize = hostAllocationSize;
        hostAllocationInfo.allocFlags = 0;
        hostAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        hostDataAllocation = std::make_unique<PaperMemory::DeviceAllocation>(device.getDevice(), device.getGPU(), hostAllocationInfo);

        PaperMemory::DeviceAllocationInfo deviceAllocationInfo = {};
        deviceAllocationInfo.allocationSize = deviceAllocationSize;
        deviceAllocationInfo.allocFlags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        deviceAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        deviceDataAllocation = std::make_unique<PaperMemory::DeviceAllocation>(device.getDevice(), device.getGPU(), deviceAllocationInfo);

        //assign allocations and copy old data back in
        hostInstancesDataBuffer->assignAllocation(hostDataAllocation.get());
        memcpy(hostInstancesDataBuffer->getHostDataPtr(), oldModelInstancesData.data(), oldModelInstancesData.size());
        hostModelDataBuffer->assignAllocation(hostDataAllocation.get());
        hostModelDataBuffer->newWrite(oldModelsData.data(), oldModelsData.size(), NULL); //location isn't needed

        deviceInstancesDataBuffer->assignAllocation(deviceDataAllocation.get());
        deviceModelDataBuffer->assignAllocation(deviceDataAllocation.get());
    }

    void RenderEngine::rebuildModelDataBuffers(VkDeviceSize rebuildSize)
    {
        PaperMemory::BufferInfo hostModelsBufferInfo = {};
        hostModelsBufferInfo.size = rebuildSize * modelsDataOverhead;
        hostModelsBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
        hostModelsBufferInfo.queueFamiliesIndices = device.getQueueFamiliesIndices();
        hostModelDataBuffer = std::make_unique<PaperMemory::FragmentableBuffer>(device.getDevice(), hostModelsBufferInfo);
        hostModelDataBuffer->setCompactionCallback([this](std::vector<PaperMemory::CompactionResult> results){ handleModelDataCompaction(results); });

        PaperMemory::BufferInfo deviceModelsBufferInfo = {};
        deviceModelsBufferInfo.size = hostModelsBufferInfo.size;
        deviceModelsBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
        deviceModelsBufferInfo.queueFamiliesIndices = device.getQueueFamiliesIndices();
        deviceModelDataBuffer = std::make_unique<PaperMemory::Buffer>(device.getDevice(), deviceModelsBufferInfo);
    }

    void RenderEngine::handleModelDataCompaction(std::vector<PaperMemory::CompactionResult> results)
    {
        std::cout << "SHIT SHIT SHIT COMPACTION EVENT!!!" << std::endl;
    }

    void RenderEngine::rebuildInstancesbuffers()
    {
        //host visible
        PaperMemory::BufferInfo hostBufferInfo = {};
        hostBufferInfo.size = std::max((VkDeviceSize)(renderingModelInstances.size() * sizeof(ModelInstance::ShaderModelInstance) * instancesDataOverhead), (VkDeviceSize)sizeof(ModelInstance::ShaderModelInstance) * 128);
        hostBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
        hostBufferInfo.queueFamiliesIndices = device.getQueueFamiliesIndices();
        hostInstancesDataBuffer = std::make_unique<PaperMemory::Buffer>(device.getDevice(), hostBufferInfo);

        //device local
        PaperMemory::BufferInfo deviceBufferInfo = {};
        deviceBufferInfo.size = hostBufferInfo.size; //same size as host visible buffer
        deviceBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
        deviceBufferInfo.queueFamiliesIndices = device.getQueueFamiliesIndices();
        deviceInstancesDataBuffer = std::make_unique<PaperMemory::Buffer>(device.getDevice(), deviceBufferInfo);
    }

    void RenderEngine::addModelData(Model &model, uint64_t &selfIndex)
    {
        selfIndex = renderingModels.size();
        renderingModels.push_back(&model);
        

        //copy initial data into host visible instances data
        std::vector<char> shaderData = model.getShaderData();

        hostModelDataBuffer->newWrite(shaderData.data(), shaderData.size(), &model.shaderDataLocation);
    }

    void RenderEngine::removeModelData(Model &model, uint64_t &selfIndex)
    {
        if(renderingModels.size() > 1)
        {
            //new reference for last element and remove
            renderingModels.at(selfIndex) = renderingModels.back();
            renderingModels.at(selfIndex)->selfIndex = selfIndex;
            renderingModels.pop_back();
        }
        else
        {
            renderingModels.clear();
        }

        //remove from buffer
        hostModelDataBuffer->removeFromRange(model.shaderDataLocation, model.getShaderData().size());
        
        selfIndex = UINT64_MAX;
    }

    void RenderEngine::addObject(ModelInstance& object, uint64_t& selfIndex)
    {
        if(object.getParentModelPtr() != NULL)
        {
            /*for(uint32_t lodIndex = 0; lodIndex < object.getParentModelPtr()->getLODs().size(); lodIndex++)
            {

                for(uint32_t matIndex = 0; matIndex < object.getParentModelPtr()->getLODs().at(lodIndex).meshMaterialData.size(); matIndex++) //iterate materials in LOD
                {
                    //get material instance
                    MaterialInstance* materialInstance;
                    if(object.getMaterialInstances().at(lodIndex).at(matIndex))
                    {
                        materialInstance = object.getMaterialInstances().at(lodIndex).at(matIndex);
                    }
                    else //use default material if one isn't selected
                    {
                        materialInstance = &defaultMaterialInstance;
                    }

                    //get meshes using same material
                    std::vector<InstancedMeshData> similarMeshes;
                    for(uint32_t meshIndex = 0; meshIndex < object.getParentModelPtr()->getLODs().at(lodIndex).meshMaterialData.at(matIndex).size(); meshIndex++)
                    {
                        InstancedMeshData meshData = {};
                        meshData.meshPtr = &object.getParentModelPtr()->getLODs().at(lodIndex).meshMaterialData.at(matIndex).at(meshIndex);
                        meshData.shaderMeshOffsetPtr = &object.shaderMeshOffsetReferences.at(lodIndex).at(matIndex).at(meshIndex);

                        similarMeshes.push_back(meshData);
                    }

                    //check if mesh group class is created
                    if(!renderTree[(Material*)materialInstance->getBaseMaterialPtr()].instances.count(materialInstance))
                    {
                        renderTree[(Material*)materialInstance->getBaseMaterialPtr()].instances[materialInstance].meshGroups = 
                            std::make_unique<CommonMeshGroup>(&device, &descriptors, materialInstance->getBaseMaterialPtr()->getRasterPipeline());
                    }

                    //add reference
                    renderTree[(Material*)materialInstance->getBaseMaterialPtr()].instances[materialInstance].meshGroups->addInstanceMeshes(&object, similarMeshes);
                }
            }*/

            //self reference
            selfIndex = renderingModelInstances.size();
            renderingModelInstances.push_back(&object);

            //check buffer size and rebuild if too small
            if(hostInstancesDataBuffer->getSize() / sizeof(ModelInstance::ShaderModelInstance) < renderingModelInstances.size() && renderingModelInstances.size() > 128)
            {
                rebuildBuffersAndAllocations(); //TODO SYNCHRONIZATION
            }

            //copy initial data into host visible instances data
            ModelInstance::ShaderModelInstance shaderModelInstance = object.getShaderInstance();
            memcpy((ModelInstance::ShaderModelInstance*)hostInstancesDataBuffer->getHostDataPtr() + selfIndex, &shaderModelInstance, sizeof(ModelInstance::ShaderModelInstance));
        }
    }

    void RenderEngine::removeObject(ModelInstance& object, uint64_t& selfIndex)
    {
        /*for(auto& [mesh, reference] : meshReferences)
        {
            if(reference) reference->removeInstanceMeshes(&object);
        }*/
        
        if(renderingModelInstances.size() > 1)
        {
            //new reference for last element and remove
            renderingModelInstances.at(selfIndex) = renderingModelInstances.back();
            renderingModelInstances.at(selfIndex)->selfIndex = selfIndex;
            renderingModelInstances.pop_back();

            //check buffer size and rebuild if unnecessarily large by a factor of 2
            if(hostInstancesDataBuffer->getSize() / sizeof(ModelInstance::ShaderModelInstance) > renderingModelInstances.size() * 2 && renderingModelInstances.size() > 128)
            {
                rebuildInstancesbuffers(); //TODO THIS NEEDS TO WAIT ON BOTH FRAME FENCES
            }

            //re-copy data
            memcpy((
                ModelInstance::ShaderModelInstance*)hostInstancesDataBuffer->getHostDataPtr() + selfIndex, 
                (ModelInstance::ShaderModelInstance*)hostInstancesDataBuffer->getHostDataPtr() + renderingModelInstances.size(), //isn't n - 1 because element was already removed with pop_back()
                sizeof(ModelInstance::ShaderModelInstance));
        }
        else
        {
            renderingModelInstances.clear();
        }
        
        selfIndex = UINT64_MAX;
    }

    int RenderEngine::beginFrame(const std::vector<VkFence>& waitFences, VkSemaphore& imageAquireSignalSemaphore)
    {
        //copy host visible buffer into device local buffer TODO OPTIMIZE THIS TO ONLY UPDATE CHANGED REGIONS
        VkBufferCopy region;
        region.srcOffset = 0;
        region.dstOffset = 0;
        region.size = renderingModelInstances.size() * sizeof(ModelInstance::ShaderModelInstance);

        PaperMemory::SynchronizationInfo syncInfo = {};
        syncInfo.queueType = PaperMemory::QueueType::TRANSFER;
        syncInfo.waitPairs = {};
        syncInfo.signalPairs = {};
        syncInfo.fence = bufferCopyFences.at(currentImage);
        usedCmdBuffers.at(currentImage).push_back(deviceInstancesDataBuffer->copyFromBufferRanges(*hostInstancesDataBuffer.get(), { region }, syncInfo));
        
        std::vector<VkFence> frameFences = waitFences;
        frameFences.push_back(bufferCopyFences.at(currentImage));

        //wait for fences
        vkWaitForFences(device.getDevice(), frameFences.size(), frameFences.data(), VK_TRUE, UINT64_MAX);

        //get available image
        VkResult imageAquireResult = vkAcquireNextImageKHR(device.getDevice(),
            swapchain.getSwapchain(),
            UINT64_MAX,
            imageAquireSignalSemaphore,
            VK_NULL_HANDLE, &currentImage);

        int returnResult = 0;
        if(imageAquireResult == VK_ERROR_OUT_OF_DATE_KHR || imageAquireResult == VK_SUBOPTIMAL_KHR)
        {
            swapchain.recreate();

            //try again
            imageAquireResult = vkAcquireNextImageKHR(device.getDevice(),
                swapchain.getSwapchain(),
                UINT64_MAX,
                imageAquireSignalSemaphore,
                VK_NULL_HANDLE, &currentImage);

            returnResult = 1;
        }

        //reset fences
        vkResetFences(device.getDevice(), frameFences.size(), frameFences.data());

        //free command buffers and reset descriptor pool
        PaperMemory::Commands::freeCommandBuffers(device.getDevice(), usedCmdBuffers.at(currentImage));
        usedCmdBuffers.at(currentImage).clear();
        descriptors.refreshPools(currentImage);

        return returnResult;
    }

    int RenderEngine::endFrame(const std::vector<VkSemaphore>& waitSemaphores)
    {
        //presentation
        //VkResult returnResult;
        VkPresentInfoKHR presentSubmitInfo = {};
        presentSubmitInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentSubmitInfo.pNext = NULL;
        presentSubmitInfo.waitSemaphoreCount = waitSemaphores.size();
        presentSubmitInfo.pWaitSemaphores = waitSemaphores.data();
        presentSubmitInfo.swapchainCount = 1;
        presentSubmitInfo.pSwapchains = &swapchain.getSwapchain();
        presentSubmitInfo.pImageIndices = &currentImage;
        presentSubmitInfo.pResults = NULL;//&returnResult;

        //too lazy to properly fix this, it probably barely affects performance anyways
        device.getQueues().at(PaperMemory::QueueType::PRESENT).queues.at(0)->threadLock.lock();
        VkResult presentResult = vkQueuePresentKHR(device.getQueues().at(PaperMemory::QueueType::PRESENT).queues.at(0)->queue, &presentSubmitInfo);
        device.getQueues().at(PaperMemory::QueueType::PRESENT).queues.at(0)->threadLock.unlock();

        int returnNumber = 0;
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) 
        {
            swapchain.recreate();
            returnNumber = 1;
        }

        //increment frame counter
        if(currentImage == 0)
        {
            currentImage = 1;
        }
        else
        {
            currentImage = 0;
        }

        glfwPollEvents();

        return returnNumber;
    }

    void RenderEngine::recycleCommandBuffer(PaperMemory::CommandBuffer &commandBuffer)
    {
        if(commandBuffer.buffer)
        {
            usedCmdBuffers.at(currentImage).push_back(commandBuffer);
            commandBuffer.buffer = VK_NULL_HANDLE;
        }
    }
}
