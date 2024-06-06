#include "PaperRenderer.h"

#include <iostream>
#include <math.h>

namespace PaperRenderer
{
    RenderEngine::RenderEngine(RendererCreationStruct creationInfo)
        :appName(creationInfo.appName),
        device(creationInfo.appName),
        window(WindowInformation(creationInfo.resX, creationInfo.resY, false), creationInfo.appName, &device),
        swapchain(&device, &window, false),
        descriptors(&device),
        pipelineBuilder(&device, &descriptors, &swapchain),
        defaultMaterial("resources/shaders/"),
        defaultMaterialInstance(&defaultMaterial)
    {
        //synchronization
        bufferCopyFences.resize(PaperMemory::Commands::getFrameCount());
        imageSemaphores.resize(PaperMemory::Commands::getFrameCount());
        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            bufferCopyFences.at(i) = PaperMemory::Commands::getSignaledFence(device.getDevice());
            imageSemaphores.at(i) = PaperMemory::Commands::getSemaphore(device.getDevice());
        }

        //buffers
        rebuildInstancesbuffers();

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
            vkDestroySemaphore(device.getDevice(), imageSemaphores.at(i), nullptr);

            //free cmd buffers
            PaperMemory::Commands::freeCommandBuffers(device.getDevice(), usedCmdBuffers.at(i));
            usedCmdBuffers.at(i).clear();
        }
    }

    void RenderEngine::rebuildInstancesbuffers()
    {
        //create copy of old host visible data if old was valid
        std::vector<char> oldData;
        if(hostInstancesDataBuffer)
        {
            oldData.resize(hostInstancesDataBuffer->getSize());
            memcpy(oldData.data(), hostInstancesDataBuffer->getHostDataPtr(), hostInstancesDataBuffer->getSize());
        }
        //host visible
        PaperMemory::DeviceAllocationInfo hostAllocationInfo = {};
        hostAllocationInfo.allocFlags = 0;
        hostAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        hostAllocationInfo.allocationSize = std::max((unsigned long long)(renderingModels.size() * 1.4), sizeof(ModelInstance::ShaderInputObject) * 128); //minimum of the size of 128 objects; otherwise 140% of required size
        hostInstancesDataAllocation = std::make_unique<PaperMemory::DeviceAllocation>(device.getDevice(), device.getGPU(), hostAllocationInfo);

        PaperMemory::BufferInfo hostBufferInfo = {};
        hostBufferInfo.queueFamilyIndices = { device.getQueues().at(PaperMemory::QueueType::TRANSFER).queueFamilyIndex };
        hostBufferInfo.size = std::max((unsigned long long)(renderingModels.size() * 1.4), sizeof(ModelInstance::ShaderInputObject) * 128); //minimum of the size of 128 objects; otherwise 140% of required size
        hostBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
        hostInstancesDataBuffer = std::make_unique<PaperMemory::Buffer>(device.getDevice(), hostBufferInfo);

        //copy new data if needed
        memcpy(hostInstancesDataBuffer->getHostDataPtr(), oldData.data(), oldData.size());

        //device local
        PaperMemory::DeviceAllocationInfo deviceAllocationInfo = {};
        deviceAllocationInfo.allocFlags = VkMemoryAllocateFlagBits::VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        deviceAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        deviceAllocationInfo.allocationSize = hostInstancesDataAllocation->getMemorySize();
        deviceInstancesDataAllocation = std::make_unique<PaperMemory::DeviceAllocation>(device.getDevice(), device.getGPU(), deviceAllocationInfo);

        PaperMemory::BufferInfo deviceBufferInfo = {};
        deviceBufferInfo.queueFamilyIndices = { device.getQueues().at(PaperMemory::QueueType::TRANSFER).queueFamilyIndex };
        deviceBufferInfo.size = hostInstancesDataBuffer->getSize();
        deviceBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;
        deviceInstancesDataBuffer = std::make_unique<PaperMemory::Buffer>(device.getDevice(), deviceBufferInfo);
    }

    void RenderEngine::addObject(ModelInstance& object, std::unordered_map<LODMesh const*, CommonMeshGroup*>& meshReferences, uint64_t& selfIndex)
    {
        if(object.getParentModelPtr() != NULL)
        {
            for(uint32_t lodIndex = 0; lodIndex < object.getParentModelPtr()->getLODs().size(); lodIndex++)
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
            }

            //self reference
            selfIndex = renderingModels.size();
            renderingModels.push_back(&object);

            //check buffer size and rebuild if too small
            if(hostInstancesDataBuffer->getSize() / sizeof(ModelInstance::ShaderInputObject) < renderingModels.size())
            {
                rebuildInstancesbuffers(); //TODO THIS NEEDS TO WAIT ON BOTH FRAME FENCES
            }

            //copy initial data into host visible instances data
            ModelInstance::ShaderInputObject shaderInputObject = object.getShaderInputObject();
            memcpy((ModelInstance::ShaderInputObject*)hostInstancesDataBuffer->getHostDataPtr() + selfIndex, &shaderInputObject, sizeof(ModelInstance::ShaderInputObject));
        }
    }

    void RenderEngine::removeObject(ModelInstance& object, std::unordered_map<LODMesh const*, CommonMeshGroup*>& meshReferences, uint64_t& selfIndex)
    {
        for(auto& [mesh, reference] : meshReferences)
        {
            if(reference) reference->removeInstanceMeshes(&object);
        }
        
        if(renderingModels.size() > 1)
        {
            //new reference for last element and remove
            renderingModels.at(selfIndex) = renderingModels.back();
            renderingModels.at(selfIndex)->setRendererIndex(selfIndex);
            renderingModels.pop_back();

            //check buffer size and rebuild if unnecessarily large by a factor of 2
            if(hostInstancesDataBuffer->getSize() / sizeof(ModelInstance::ShaderInputObject) > renderingModels.size() * 2)
            {
                rebuildInstancesbuffers(); //TODO THIS NEEDS TO WAIT ON BOTH FRAME FENCES
            }

            //re-copy data
            memcpy((
                ModelInstance::ShaderInputObject*)hostInstancesDataBuffer->getHostDataPtr() + selfIndex, 
                (ModelInstance::ShaderInputObject*)hostInstancesDataBuffer->getHostDataPtr() + renderingModels.size(), //isn't n - 1 because element was already removed with pop_back()
                sizeof(ModelInstance::ShaderInputObject));
        }
        else
        {
            renderingModels.clear();
        }
        
        selfIndex = UINT64_MAX;
    }

    const VkSemaphore& RenderEngine::beginFrame(std::vector<VkFence>& waitFences)
    {
        //copy host visible buffer into device local buffer TODO OPTIMIZE THIS TO ONLY UPDATE CHANGED REGIONS
        VkBufferCopy region;
        region.srcOffset = 0;
        region.dstOffset = 0;
        region.size = hostInstancesDataBuffer->getSize();

        PaperMemory::SynchronizationInfo syncInfo = {};
        syncInfo.queueType = PaperMemory::QueueType::TRANSFER;
        syncInfo.waitPairs = {};
        syncInfo.signalPairs = {};
        syncInfo.fence = bufferCopyFences.at(currentImage);
        usedCmdBuffers.at(currentImage).push_back(deviceInstancesDataBuffer->copyFromBufferRanges(*hostInstancesDataBuffer.get(), device.getQueues().at(PaperMemory::QueueType::TRANSFER).queueFamilyIndex, { region }, syncInfo));
        
        std::vector<VkFence> frameFences = waitFences;
        frameFences.push_back(bufferCopyFences.at(currentImage));

        //wait for fences
        vkWaitForFences(device.getDevice(), frameFences.size(), frameFences.data(), VK_TRUE, UINT64_MAX);

        //get available image
        VkResult imageAquireResult = vkAcquireNextImageKHR(device.getDevice(),
            swapchain.getSwapchain(),
            UINT64_MAX,
            imageSemaphores.at(currentImage),
            VK_NULL_HANDLE, &currentImage);

        if(imageAquireResult == VK_ERROR_OUT_OF_DATE_KHR || imageAquireResult == VK_SUBOPTIMAL_KHR)
        {
            swapchain.recreate();
            if(cameraPtr) cameraPtr->updateCameraProjection();

            //try again
            imageAquireResult = vkAcquireNextImageKHR(device.getDevice(),
                swapchain.getSwapchain(),
                UINT64_MAX,
                imageSemaphores.at(currentImage),
                VK_NULL_HANDLE, &currentImage);
        }

        //reset fences
        vkResetFences(device.getDevice(), frameFences.size(), frameFences.data());

        //free command buffers and reset descriptor pool
        PaperMemory::Commands::freeCommandBuffers(device.getDevice(), usedCmdBuffers.at(currentImage));
        usedCmdBuffers.at(currentImage).clear();
        descriptors.refreshPools(currentImage);

        return imageSemaphores.at(currentImage);
    }

    void RenderEngine::endFrame(const std::vector<VkSemaphore>& waitSemaphores)
    {
        //presentation
        VkResult returnResult;
        VkPresentInfoKHR presentSubmitInfo = {};
        presentSubmitInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentSubmitInfo.pNext = NULL;
        presentSubmitInfo.waitSemaphoreCount = waitSemaphores.size();
        presentSubmitInfo.pWaitSemaphores = waitSemaphores.data();
        presentSubmitInfo.swapchainCount = 1;
        presentSubmitInfo.pSwapchains = &swapchain.getSwapchain();
        presentSubmitInfo.pImageIndices = &currentImage;
        presentSubmitInfo.pResults = &returnResult;

        //too lazy to properly fix this, it probably barely affects performance anyways
        device.getQueues().at(PaperMemory::QueueType::PRESENT).queues.at(0)->threadLock.lock();
        VkResult presentResult = vkQueuePresentKHR(device.getQueues().at(PaperMemory::QueueType::PRESENT).queues.at(0)->queue, &presentSubmitInfo);
        device.getQueues().at(PaperMemory::QueueType::PRESENT).queues.at(0)->threadLock.unlock();

        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) 
        {
            swapchain.recreate();
            cameraPtr->updateCameraProjection();
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
    }
}
