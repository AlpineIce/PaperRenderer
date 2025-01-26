#include "PaperRenderer.h"
#define VK_NO_PROTOTYPES
#include "volk.h"
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include "vk_mem_alloc.h"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE 
#include "glm.hpp"

#include <math.h>

namespace PaperRenderer
{
    RenderEngine::RenderEngine(const PaperRendererInfo& creationInfo)
        :logger(*this, creationInfo.logEventCallbackFunction),
        device(*this, creationInfo.deviceInstanceInfo),
        swapchain(*this, creationInfo.swapchainRebuildCallbackFunction, creationInfo.windowState),
        descriptors(*this),
        pipelineBuilder(*this),
        rasterPreprocessPipeline(*this, creationInfo.rasterPreprocessSpirv),
        tlasInstanceBuildPipeline(*this, creationInfo.rtPreprocessSpirv),
        asBuilder(*this),
        stagingBuffer({ std::make_unique<RendererStagingBuffer>(*this), std::make_unique<RendererStagingBuffer>(*this) })
    {
        //initialize buffers
        rebuildModelDataBuffer();
        rebuildInstancesbuffer();

        //indirect draw model matrices descriptor layout
        defaultDescriptorLayouts[INDIRECT_DRAW_MATRICES] = descriptors.createDescriptorSetLayout({ {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
            .pImmutableSamplers = NULL
        } });

        //camera matrices descriptor layout
        defaultDescriptorLayouts[CAMERA_MATRICES] = descriptors.createDescriptorSetLayout({ {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
            .descriptorCount = 1,
            .stageFlags = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .pImmutableSamplers = NULL
        } });

        //ray tracing instance descriptions
        defaultDescriptorLayouts[TLAS_INSTANCE_DESCRIPTIONS] = descriptors.createDescriptorSetLayout({ {
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
            .pImmutableSamplers = NULL
        } });

        //finish up
        vkDeviceWaitIdle(device.getDevice());
        lastFrameTimePoint = std::chrono::high_resolution_clock::now();

        //log
        logger.recordLog({
            .type = INFO,
            .text = "----------Renderer initialization finished----------"
        });
    }

    RenderEngine::~RenderEngine()
    {
        vkDeviceWaitIdle(device.getDevice());

        //destroy default set layouts
        for(VkDescriptorSetLayout layout : defaultDescriptorLayouts)
        {
            vkDestroyDescriptorSetLayout(device.getDevice(), layout, nullptr);
        }
    
        //destroy buffers
        modelDataBuffer.reset();
        instancesDataBuffer.reset();

        //log destructor
        logger.recordLog({
            .type = INFO,
            .text = "----------Renderer destructor initialized----------"
        });
    }

    void RenderEngine::rebuildModelDataBuffer()
    {
        //timer
        Timer timer(*this, "Rebuild Model Data Buffer", IRREGULAR);

        //new buffer to replace old
        VkDeviceSize newModelDataSize = 4096;
        VkDeviceSize newWriteSize = 0;
        if(modelDataBuffer)
        {
            modelDataBuffer->compact();
            newModelDataSize = modelDataBuffer->getDesiredLocation();
            newWriteSize = modelDataBuffer->getStackLocation();
        }

        BufferInfo modelsBufferInfo = {};
        modelsBufferInfo.allocationFlags = 0;
        modelsBufferInfo.size = newModelDataSize * modelsDataOverhead;
        modelsBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
        std::unique_ptr<FragmentableBuffer> newBuffer = std::make_unique<FragmentableBuffer>(*this, modelsBufferInfo, 8);
        newBuffer->setCompactionCallback([this](std::vector<CompactionResult> results){ handleModelDataCompaction(results); });

        //copy old data into new if old buffer existed
        if(newWriteSize)
        {
            VkBufferCopy copyRegion = {};
            copyRegion.srcOffset = 0;
            copyRegion.dstOffset = 0;
            copyRegion.size = newWriteSize;

            SynchronizationInfo syncInfo = {};
            syncInfo.fence = device.getCommands().getUnsignaledFence();
            newBuffer->getBuffer().copyFromBufferRanges(*instancesDataBuffer, { copyRegion }, syncInfo);
            vkWaitForFences(device.getDevice(), 1, &syncInfo.fence, VK_TRUE, UINT64_MAX);
            vkDestroyFence(device.getDevice(), syncInfo.fence, nullptr);

            //pseudo write
            newBuffer->newWrite(NULL, newWriteSize, NULL);
        }

        //replace old buffer
        modelDataBuffer = std::move(newBuffer);
    }

    void RenderEngine::handleModelDataCompaction(const std::vector<CompactionResult>& results)
    {
        //fix model data first
        for(const CompactionResult compactionResult : results)
        {
            for(Model* model : renderingModels)
            {
                if(model->shaderDataLocation > compactionResult.location)
                {
                    //shift stored location
                    model->shaderDataLocation -= compactionResult.shiftSize;
                }
            }
        }

        //then fix instances data
        for(ModelInstance* instance : renderingModelInstances)
        {
            toUpdateModelInstances.push_front(instance);
        }
    }

    void RenderEngine::rebuildInstancesbuffer()
    {
        //timer
        Timer timer(*this, "Rebuild Instances Buffer", IRREGULAR);

        //new buffer to replace old
        BufferInfo bufferInfo = {};
        bufferInfo.allocationFlags = 0;
        bufferInfo.size = std::max((VkDeviceSize)(renderingModelInstances.size() * sizeof(ModelInstance::ShaderModelInstance) * instancesDataOverhead), (VkDeviceSize)sizeof(ModelInstance::ShaderModelInstance) * 128);
        bufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;
        std::unique_ptr<Buffer> newBuffer = std::make_unique<Buffer>(*this, bufferInfo);

        //copy old data into new if old existed
        if(instancesDataBuffer)
        {
            VkBufferCopy copyRegion = {};
            copyRegion.srcOffset = 0;
            copyRegion.dstOffset = 0;
            copyRegion.size = std::min(renderingModelInstances.size() * sizeof(ModelInstance::ShaderModelInstance), (size_t)instancesDataBuffer->getSize());

            SynchronizationInfo syncInfo = {};
            syncInfo.queueType = TRANSFER;
            syncInfo.fence = device.getCommands().getUnsignaledFence();
            newBuffer->copyFromBufferRanges(*instancesDataBuffer, { copyRegion }, syncInfo);
            vkWaitForFences(device.getDevice(), 1, &syncInfo.fence, VK_TRUE, UINT64_MAX);
            vkDestroyFence(device.getDevice(), syncInfo.fence, nullptr);
        }
        
        //replace old buffer
        instancesDataBuffer = std::move(newBuffer);
    }

    void RenderEngine::addModelData(Model* model)
    {
        //self reference
        model->selfIndex = renderingModels.size();
        renderingModels.push_back(model);
        
        //"write"
        if(modelDataBuffer->newWrite(NULL, model->getShaderData().size(), &model->shaderDataLocation) == FragmentableBuffer::WriteResult::OUT_OF_MEMORY)
        {
            rebuildModelDataBuffer();
        }

        //queue data transfer
        toUpdateModels.push_front(model);
    }

    void RenderEngine::removeModelData(Model* model)
    {
        if(renderingModels.size() > 1)
        {
            //new reference for last element and remove
            renderingModels.at(model->selfIndex) = renderingModels.back();
            renderingModels.at(model->selfIndex)->selfIndex = model->selfIndex;
            renderingModels.pop_back();
        }
        else
        {
            renderingModels.clear();
        }

        //remove from buffer
        modelDataBuffer->removeFromRange(model->shaderDataLocation, model->getShaderData().size());
        
        model->selfIndex = UINT64_MAX;

        //TODO UPDATE DEPENDENCIES
    }

    void RenderEngine::addObject(ModelInstance* object)
    {
        //self reference
        object->rendererSelfIndex = renderingModelInstances.size();
        renderingModelInstances.push_back(object);
        
        //queue data transfer
        toUpdateModelInstances.push_front(object);
    }

    void RenderEngine::removeObject(ModelInstance* object)
    {
        if(renderingModelInstances.size() > 1)
        {
            //new reference for last element and remove
            renderingModelInstances.at(object->rendererSelfIndex) = renderingModelInstances.back();
            renderingModelInstances.at(object->rendererSelfIndex)->rendererSelfIndex = object->rendererSelfIndex;

            //queue data transfer
            toUpdateModelInstances.push_front(renderingModelInstances.at(object->rendererSelfIndex));

            //remove last element from instances vector (the one that was moved in the mirrored buffer)
            renderingModelInstances.pop_back();
        }
        else
        {
            renderingModelInstances.clear();
        }

        //null out any instances that may be queued
        for(ModelInstance*& instance : toUpdateModelInstances)
        {
            if(instance == object)
            {
                instance = NULL;
            }
        }
        
        object->rendererSelfIndex = UINT32_MAX;
    }

    void RenderEngine::queueModelsAndInstancesTransfers()
    {
        //timer
        Timer timer(*this, "Queue Models and Instances Transfers", REGULAR);

        //check buffer sizes
        if(instancesDataBuffer->getSize() / sizeof(ModelInstance::ShaderModelInstance) < renderingModelInstances.size() && renderingModelInstances.size() > 128)
        {
            rebuildInstancesbuffer(); //TODO SYNCHRONIZATION
        }

        //sort instances and models; remove duplicates
        std::sort(toUpdateModelInstances.begin(), toUpdateModelInstances.end());
        auto sortedInstances = std::unique(toUpdateModelInstances.begin(), toUpdateModelInstances.end());
        toUpdateModelInstances.erase(sortedInstances, toUpdateModelInstances.end());

        std::sort(toUpdateModels.begin(), toUpdateModels.end());
        auto sortedModels = std::unique(toUpdateModels.begin(), toUpdateModels.end());
        toUpdateModels.erase(sortedModels, toUpdateModels.end());

        //queue instance data
        for(ModelInstance* instance : toUpdateModelInstances)
        {
            //skip if instance is NULL
            if(!instance) continue;

            ModelInstance::ShaderModelInstance shaderInstance = instance->getShaderInstance();

            //write data
            std::vector<char> data(sizeof(ModelInstance::ShaderModelInstance));
            memcpy(data.data(), &shaderInstance, data.size());
            
            getStagingBuffer().queueDataTransfers(*instancesDataBuffer, sizeof(ModelInstance::ShaderModelInstance) * instance->rendererSelfIndex, data);
        }

        //queue model data
        for(Model* model : toUpdateModels)
        {
            getStagingBuffer().queueDataTransfers(modelDataBuffer->getBuffer(), model->shaderDataLocation, model->getShaderData());
        }

        //clear deques
        toUpdateModelInstances.clear();
        toUpdateModels.clear();
    }

    const VkSemaphore& RenderEngine::beginFrame()
    {
        //clear previous statistics
        statisticsTracker.clearStatistics();

        //idle staging buffer
        stagingBuffer[getBufferIndex()]->idleBuffer();

        //reset command pools
        device.getCommands().resetCommandPools();

        //acquire next image
        const VkSemaphore& imageAcquireSemaphore = swapchain.acquireNextImage();

        //queue data transfers
        queueModelsAndInstancesTransfers();

        //return image acquire semaphore
        return imageAcquireSemaphore;
    }

    void RenderEngine::endFrame(const std::vector<VkSemaphore>& waitSemaphores)
    {
        //timer
        Timer timer(*this, "End Frame", REGULAR);

        //presentation
        swapchain.presentImage(waitSemaphores);

        //increment frame counter so next frame can be prepared early
        frameNumber++;

        //change delta time
        deltaTime = (std::chrono::high_resolution_clock::now() - lastFrameTimePoint).count() / (1000.0 * 1000.0 * 1000.0);
        lastFrameTimePoint = std::chrono::high_resolution_clock::now();

        glfwPollEvents();
    }
}
