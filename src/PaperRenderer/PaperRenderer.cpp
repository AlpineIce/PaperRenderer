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
        defaultDescriptorLayouts({
            DescriptorSetLayout(*this, {{ //INDIRECT_DRAW_MATRICES
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
                .pImmutableSamplers = NULL
            }}),
            DescriptorSetLayout(*this, { { //CAMERA_MATRICES
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_ALL,
                .pImmutableSamplers = NULL
            }}),
            DescriptorSetLayout(*this, { { //TLAS_INSTANCE_DESCRIPTIONS
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
                .pImmutableSamplers = NULL
            }}),
            DescriptorSetLayout(*this, { { //INSTANCES
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_ALL,
                .pImmutableSamplers = NULL
            }})
        }),
        rasterPreprocessPipeline(*this, creationInfo.rasterPreprocessSpirv),
        tlasInstanceBuildPipeline(*this, creationInfo.rtPreprocessSpirv),
        asBuilder(*this),
        stagingBuffer([this] (){
            const uint32_t transferQueueCount = device.getQueues()[TRANSFER].queues.size();

            return std::array<RendererStagingBuffer, 2>({
                RendererStagingBuffer(*this, *device.getQueues()[TRANSFER].queues[0 % transferQueueCount]),
                RendererStagingBuffer(*this, *device.getQueues()[TRANSFER].queues[1 % transferQueueCount])
            });
        } ()),
        instancesBufferDescriptor(*this, defaultDescriptorLayouts[INSTANCES].getSetLayout()),
        modelDataBuffer(*this, {
            .size = 4096,
            .usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR,
            .allocationFlags = 0
        }, 8)
    {
        //initialize buffers
        rebuildInstancesbuffer();

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
        modelDataBuffer.compact();
        const VkDeviceSize newModelDataSize = modelDataBuffer.getDesiredLocation();
        const VkDeviceSize newWriteSize = modelDataBuffer.getStackLocation();

        const BufferInfo modelsBufferInfo = {
            .size = (VkDeviceSize)(newModelDataSize * modelsDataOverhead),
            .usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR,
            .allocationFlags = 0
        };
        FragmentableBuffer newBuffer(*this, modelsBufferInfo, 8);
        newBuffer.setCompactionCallback([this](std::vector<CompactionResult> results){ handleModelDataCompaction(results); });

        //copy old data into new if old buffer existed
        if(newWriteSize)
        {
            const VkBufferCopy copyRegion = {
                .srcOffset = 0,
                .dstOffset = 0,
                .size = newWriteSize
            };
            newBuffer.getBuffer().copyFromBufferRanges(instancesDataBuffer, { copyRegion }, {}).idle();

            //pseudo write
            newBuffer.newWrite(NULL, newWriteSize, NULL);
        }

        //replace old buffer
        modelDataBuffer = std::move(newBuffer);
    }

    void RenderEngine::handleModelDataCompaction(const std::vector<CompactionResult>& results)
    {
        //fix model data first
        for(const CompactionResult compactionResult : results)
        {
            for(ModelGeometryData* modelData : renderingModels)
            {
                if(modelData->shaderDataReference.shaderDataLocation > compactionResult.location)
                {
                    //shift stored location
                    modelData->shaderDataReference.shaderDataLocation -= compactionResult.shiftSize;
                }
            }
        }

        //then fix instances data
        for(ModelInstance* instance : renderingModelInstances)
        {
            toUpdateModelInstances.insert(instance);
        }
    }

    void RenderEngine::rebuildInstancesbuffer()
    {
        //timer
        Timer timer(*this, "Rebuild Instances Buffer", IRREGULAR);

        //new buffer to replace old
        const BufferInfo bufferInfo = {
            .size = std::max((VkDeviceSize)(renderingModelInstances.size() * sizeof(ShaderModelInstance) * instancesDataOverhead), (VkDeviceSize)sizeof(ShaderModelInstance) * 128),
            .usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR,
            .allocationFlags = 0
        };
        Buffer newBuffer(*this, bufferInfo);

        //idle old buffer
        instancesDataBuffer.idleOwners();

        //copy
        const VkBufferCopy copyRegion = {
            .srcOffset = 0,
            .dstOffset = 0,
            .size = std::min(renderingModelInstances.size() * sizeof(ShaderModelInstance), (size_t)instancesDataBuffer.getSize())
        };

        if(copyRegion.size)
        {
            newBuffer.copyFromBufferRanges(instancesDataBuffer, { copyRegion }, {}).idle();
        }
        
        //replace old buffer
        instancesDataBuffer = std::move(newBuffer);

        //update descriptors
        instancesBufferDescriptor.updateDescriptorSet({
            .bufferWrites = {
                { //binding 0: UBO input data
                    .infos = { {
                        .buffer = instancesDataBuffer.getBuffer(),
                        .offset = 0,
                        .range = VK_WHOLE_SIZE
                    } },
                    .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    .binding = 0
                }
            }
        });
    }

    void RenderEngine::addModelData(ModelGeometryData* modelData)
    {
        //lock mutex
        std::lock_guard guard(rendererMutex);

        //self reference
        modelData->shaderDataReference.selfIndex = renderingModels.size();
        renderingModels.push_back(modelData);
        
        //"write"
        if(modelDataBuffer.newWrite(NULL, modelData->getShaderData().size(), &modelData->shaderDataReference.shaderDataLocation) == FragmentableBuffer::WriteResult::OUT_OF_MEMORY)
        {
            rebuildModelDataBuffer();
            modelDataBuffer.newWrite(NULL, modelData->getShaderData().size(), &modelData->shaderDataReference.shaderDataLocation);
        }

        //queue data transfer
        toUpdateModels.insert(modelData);
    }

    void RenderEngine::removeModelData(ModelGeometryData* modelData)
    {
        //lock mutex
        std::lock_guard guard(rendererMutex);

        if(renderingModels.size() > 1)
        {
            //new reference for last element and remove
            renderingModels.at(modelData->shaderDataReference.selfIndex) = renderingModels.back();
            renderingModels.at(modelData->shaderDataReference.selfIndex)->shaderDataReference.selfIndex = modelData->shaderDataReference.selfIndex;
            renderingModels.pop_back();
        }
        else
        {
            renderingModels.clear();
        }

        //remove from buffer
        modelDataBuffer.removeFromRange(modelData->shaderDataReference.shaderDataLocation, modelData->getShaderData().size());
        toUpdateModels.erase(modelData);

        modelData->shaderDataReference.selfIndex = UINT32_MAX;
    }

    void RenderEngine::rereferenceModelData(ModelGeometryData* modelData)
    {
        // Clear old reference and insert new one
        auto it = toUpdateModels.find(renderingModels.at(modelData->shaderDataReference.selfIndex));
        if(it != toUpdateModels.end())
        {
            toUpdateModels.erase(it);
            toUpdateModels.insert(modelData);
        }
        
        renderingModels.at(modelData->shaderDataReference.selfIndex) = modelData;
    }

    void RenderEngine::addObject(ModelInstance* object)
    {
        //lock mutex
        std::lock_guard guard(rendererMutex);

        //self reference
        object->rendererSelfIndex = renderingModelInstances.size();
        renderingModelInstances.push_back(object);
        
        //queue data transfer
        toUpdateModelInstances.insert(object);
    }

    void RenderEngine::removeObject(ModelInstance* object)
    {
        //lock mutex
        std::lock_guard guard(rendererMutex);
        
        if(renderingModelInstances.size() > 1)
        {
            //new reference for last element and remove
            renderingModelInstances.at(object->rendererSelfIndex) = renderingModelInstances.back();
            renderingModelInstances.at(object->rendererSelfIndex)->rendererSelfIndex = object->rendererSelfIndex;

            //queue data transfer
            toUpdateModelInstances.insert(renderingModelInstances.at(object->rendererSelfIndex));

            //remove last element from instances vector (the one that was moved in the mirrored buffer)
            renderingModelInstances.pop_back();
        }
        else
        {
            renderingModelInstances.clear();
        }

        toUpdateModelInstances.erase(object);

        object->rendererSelfIndex = UINT32_MAX;
    }

    void RenderEngine::rereferenceObject(ModelInstance* object)
    {
        // Clear old reference and insert new one
        auto it = toUpdateModelInstances.find(renderingModelInstances.at(object->rendererSelfIndex));
        if(it != toUpdateModelInstances.end())
        {
            toUpdateModelInstances.erase(it);
            toUpdateModelInstances.insert(object);
        }
        
        renderingModelInstances.at(object->rendererSelfIndex) = object;
    }

    std::vector<StagingBufferTransfer> RenderEngine::queueModelsAndInstancesTransfers()
    {
        //timer
        Timer timer(*this, "Queue Models and Instances Transfers", REGULAR);

        //lock mutex
        std::lock_guard guard(rendererMutex);

        //check buffer sizes
        if(instancesDataBuffer.getSize() / sizeof(ShaderModelInstance) < renderingModelInstances.size() && renderingModelInstances.size() > 128)
        {
            rebuildInstancesbuffer();
        }

        std::vector<StagingBufferTransfer> transfers = {};

        //queue instance data
        for(ModelInstance* instance : toUpdateModelInstances)
        {
            //skip if instance is NULL
            if(!instance) continue;
            
            //write instance data
            transfers.push_back({
                .dstOffset = sizeof(ShaderModelInstance) * instance->rendererSelfIndex,
                .data = [&] {
                    std::vector<uint8_t> transferData(sizeof(ShaderModelInstance));
                    const ShaderModelInstance shaderInstance = instance->getShaderInstance();
                    memcpy(transferData.data(), &shaderInstance, sizeof(ShaderModelInstance));

                    return transferData;
                } (),
                .dstBuffer = &instancesDataBuffer
            });
        }

        //queue model data
        for(ModelGeometryData* modelData : toUpdateModels)
        {
            //skip if model is NULL
            if(!modelData) continue;

            //write model data
            transfers.push_back({
                .dstOffset = modelData->shaderDataReference.shaderDataLocation,
                .data = modelData->getShaderData(),
                .dstBuffer = &modelDataBuffer.getBuffer()
            });
        }

        //clear deques
        toUpdateModelInstances.clear();
        toUpdateModels.clear();

        return transfers;
    }

    const VkSemaphore& RenderEngine::beginFrame(std::vector<StagingBufferTransfer>& extraTransfers, const SynchronizationInfo& transferSyncInfo)
    {
        //clear previous statistics
        statisticsTracker.clearStatistics();

        //idle staging buffer
        stagingBuffer[getBufferIndex()].resetBuffer();

        //reset command pools
        device.getCommands().resetCommandPools();

        //acquire next image
        const VkSemaphore& imageAcquireSemaphore = swapchain.acquireNextImage();

        //queue data transfers
        std::vector<StagingBufferTransfer> transfers = queueModelsAndInstancesTransfers();
        transfers.insert(transfers.end(), extraTransfers.begin(), extraTransfers.end());
        stagingBuffer[getBufferIndex()].submitTransfers(transfers, transferSyncInfo);

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
