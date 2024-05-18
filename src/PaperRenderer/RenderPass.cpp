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

    RTPreprocessPipeline::RTPreprocessPipeline(std::string fileDir)
        :ComputeShader()
    {
    }

    RTPreprocessPipeline::~RTPreprocessPipeline()
    {
    }

    PaperMemory::CommandBuffer RTPreprocessPipeline::submit()
    {
        return { VK_NULL_HANDLE, PaperMemory::QueueType::COMPUTE };
    }

    //----------RENDER PASS DEFINITIONS----------//
    
    RenderPass::RenderPass(Swapchain* swapchain, Device* device, DescriptorAllocator* descriptors, PipelineBuilder* pipelineBuilder)
        :swapchainPtr(swapchain),
        devicePtr(device),
        descriptorsPtr(descriptors),
        pipelineBuilderPtr(pipelineBuilder),
        currentImage(0),
        rtAccelStructure(device)
    {
        //THE UBER-BUFFER
        newDataStagingBuffers.resize(PaperMemory::Commands::getFrameCount());
        stagingAllocations.resize(PaperMemory::Commands::getFrameCount());
        renderingData.resize(PaperMemory::Commands::getFrameCount());

        //synchronization objects
        imageSemaphores.resize(PaperMemory::Commands::getFrameCount());
        bufferCopySemaphores.resize(PaperMemory::Commands::getFrameCount());
        bufferCopyFences.resize(PaperMemory::Commands::getFrameCount());
        BLASBuildSemaphores.resize(PaperMemory::Commands::getFrameCount());
        TLASBuildSemaphores.resize(PaperMemory::Commands::getFrameCount());
        rasterPreprocessSemaphores.resize(PaperMemory::Commands::getFrameCount());
        preprocessTLASSignalSemaphores.resize(PaperMemory::Commands::getFrameCount());
        renderSemaphores.resize(PaperMemory::Commands::getFrameCount());
        RTFences.resize(PaperMemory::Commands::getFrameCount());
        renderFences.resize(PaperMemory::Commands::getFrameCount());
        usedCmdBuffers.resize(PaperMemory::Commands::getFrameCount());

        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            //descriptors
            descriptorsPtr->refreshPools(i);

            //synchronization stuff
            imageSemaphores.at(i) = PaperMemory::Commands::getSemaphore(devicePtr->getDevice());
            bufferCopySemaphores.at(i) = PaperMemory::Commands::getSemaphore(devicePtr->getDevice());
            bufferCopyFences.at(i) = PaperMemory::Commands::getSignaledFence(devicePtr->getDevice());
            TLASBuildSemaphores.at(i) = PaperMemory::Commands::getSemaphore(devicePtr->getDevice());
            rasterPreprocessSemaphores.at(i) = PaperMemory::Commands::getSemaphore(devicePtr->getDevice());
            preprocessTLASSignalSemaphores.at(i) = PaperMemory::Commands::getSemaphore(devicePtr->getDevice());
            renderSemaphores.at(i) = PaperMemory::Commands::getSemaphore(devicePtr->getDevice());
            RTFences.at(i) = PaperMemory::Commands::getSignaledFence(devicePtr->getDevice());
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

    RenderPass::~RenderPass()
    {
        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            vkDestroySemaphore(devicePtr->getDevice(), imageSemaphores.at(i), nullptr);
            vkDestroySemaphore(devicePtr->getDevice(), bufferCopySemaphores.at(i), nullptr);
            vkDestroyFence(devicePtr->getDevice(), bufferCopyFences.at(i), nullptr);
            vkDestroySemaphore(devicePtr->getDevice(), BLASBuildSemaphores.at(i), nullptr);
            vkDestroySemaphore(devicePtr->getDevice(), TLASBuildSemaphores.at(i), nullptr);
            vkDestroySemaphore(devicePtr->getDevice(), rasterPreprocessSemaphores.at(i), nullptr);
            vkDestroySemaphore(devicePtr->getDevice(), preprocessTLASSignalSemaphores.at(i), nullptr);
            vkDestroySemaphore(devicePtr->getDevice(), renderSemaphores.at(i), nullptr);
            vkDestroyFence(devicePtr->getDevice(), RTFences.at(i), nullptr);
            vkDestroyFence(devicePtr->getDevice(), renderFences.at(i), nullptr);

            //free cmd buffers
            for(PaperMemory::CommandBuffer& buffer : usedCmdBuffers.at(i))
            {
                PaperMemory::Commands::freeCommandBuffer(devicePtr->getDevice(), buffer);
            }
        }
    }

    void RenderPass::rebuildRenderDataAllocation(uint32_t currentFrame)
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

    void RenderPass::setRasterStagingData(const std::unordered_map<Material *, MaterialNode> &renderTree)
    {
        //clear old
        std::vector<char>& stagingData = renderingData.at(currentImage).stagingData;
        stagingData.clear();

        //----------MESH REQUIREMENETS----------//

        //draw counts
        renderingData.at(currentImage).meshDrawCountsRegion = VkBufferCopy();
        renderingData.at(currentImage).meshDrawCountsRegion.srcOffset = stagingData.size();
        renderingData.at(currentImage).meshDrawCountsRegion.dstOffset = stagingData.size();
        for(const auto& [material, materialNode] : renderTree)
        {
            for(const auto& [materialInstance, instanceNode] : materialNode.instances) //memcpy because it needs to be cleared //TODO NEEDS TO HAPPEN AFTER VERTEX SHADER STAGE
            {
                uint32_t lastSize = stagingData.size();
                std::vector<uint32_t> data;
                stagingData.resize(stagingData.size() + instanceNode.objectBuffer->getDrawCountsSize(stagingData.size()));
                for(uint32_t i = 0; i < (stagingData.size() - lastSize) / sizeof(uint32_t); i++)
                {
                    data.push_back(0);
                }
                memcpy(stagingData.data() + lastSize, data.data(), stagingData.size() - lastSize);
            }
        }
        renderingData.at(currentImage).meshDrawCountsRegion.size = stagingData.size() - renderingData.at(currentImage).meshDrawCountsRegion.dstOffset;
        stagingData.resize(PaperMemory::DeviceAllocation::padToMultiple(stagingData.size(), devicePtr->getGPUProperties().properties.limits.minStorageBufferOffsetAlignment));

        //draw commands
        renderingData.at(currentImage).meshDrawCommandsRegion = VkBufferCopy();
        renderingData.at(currentImage).meshDrawCommandsRegion.srcOffset = stagingData.size();
        renderingData.at(currentImage).meshDrawCommandsRegion.dstOffset = stagingData.size();
        for(const auto& [material, materialNode] : renderTree)
        {
            for(const auto& [materialInstance, instanceNode] : materialNode.instances)
            {
                stagingData.resize(stagingData.size() + instanceNode.objectBuffer->getDrawCommandsSize(stagingData.size()));
            }
        }
        renderingData.at(currentImage).meshDrawCommandsRegion.size = stagingData.size() - renderingData.at(currentImage).meshDrawCommandsRegion.dstOffset;
        stagingData.resize(PaperMemory::DeviceAllocation::padToMultiple(stagingData.size(), devicePtr->getGPUProperties().properties.limits.minStorageBufferOffsetAlignment));

        //output mesh instance data
        renderingData.at(currentImage).meshOutputObjectsRegion = VkBufferCopy();
        renderingData.at(currentImage).meshOutputObjectsRegion.srcOffset = stagingData.size();
        renderingData.at(currentImage).meshOutputObjectsRegion.dstOffset = stagingData.size();
        for(const auto& [material, materialNode] : renderTree)
        {
            for(const auto& [materialInstance, instanceNode] : materialNode.instances)
            {
                stagingData.resize(stagingData.size() + instanceNode.objectBuffer->getOutputObjectSize(stagingData.size()));
            }
        }
        renderingData.at(currentImage).meshOutputObjectsRegion.size = stagingData.size() - renderingData.at(currentImage).meshOutputObjectsRegion.dstOffset;
        stagingData.resize(PaperMemory::DeviceAllocation::padToMultiple(stagingData.size(), devicePtr->getGPUProperties().properties.limits.minStorageBufferOffsetAlignment));

        //----------PREPROCESS INPUT REQUIREMENETS----------//

        //LOD mesh data
        renderingData.at(currentImage).meshLODOffsetsRegion = VkBufferCopy();
        renderingData.at(currentImage).meshLODOffsetsRegion.srcOffset = stagingData.size();
        renderingData.at(currentImage).meshLODOffsetsRegion.dstOffset = stagingData.size();
        for(auto& [model, instances] : renderingModels)
        {
            uint32_t lastSize = stagingData.size();
            std::vector<LODMesh> lodMeshData = model->getMeshLODData(stagingData.size());
            stagingData.resize(stagingData.size() + lodMeshData.size() * sizeof(LODMesh));
            memcpy(stagingData.data() + lastSize, lodMeshData.data(), lodMeshData.size() * sizeof(LODMesh));
        }
        renderingData.at(currentImage).meshLODOffsetsRegion.size = stagingData.size() - renderingData.at(currentImage).meshLODOffsetsRegion.dstOffset;
        stagingData.resize(PaperMemory::DeviceAllocation::padToMultiple(stagingData.size(), devicePtr->getGPUProperties().properties.limits.minStorageBufferOffsetAlignment));

        //LOD data          THIS **ABSOLUTELY MUST** COME AFTER MESH DATA
        renderingData.at(currentImage).LODOffsetsRegion = VkBufferCopy();
        renderingData.at(currentImage).LODOffsetsRegion.srcOffset = stagingData.size();
        renderingData.at(currentImage).LODOffsetsRegion.dstOffset = stagingData.size();
        for(auto& [model, instances] : renderingModels)
        {
            uint32_t lastSize = stagingData.size();
            std::vector<ShaderLOD> lodData = model->getLODData(stagingData.size());
            stagingData.resize(stagingData.size() + lodData.size() * sizeof(ShaderLOD));
            memcpy(stagingData.data() + lastSize, lodData.data(), lodData.size() * sizeof(ShaderLOD));
        }
        renderingData.at(currentImage).LODOffsetsRegion.size = stagingData.size() - renderingData.at(currentImage).LODOffsetsRegion.dstOffset;
        stagingData.resize(PaperMemory::DeviceAllocation::padToMultiple(stagingData.size(), devicePtr->getGPUProperties().properties.limits.minStorageBufferOffsetAlignment));
        
        //get input objects (binding 1)
        renderingData.at(currentImage).inputObjectsRegion = VkBufferCopy();
        renderingData.at(currentImage).inputObjectsRegion.srcOffset = stagingData.size();
        renderingData.at(currentImage).inputObjectsRegion.dstOffset = stagingData.size();
        std::vector<ShaderInputObject> shaderInputObjects;
        for(auto& [model, instances] : renderingModels) //material
        {
            for(ModelInstance* inputObject : instances)
            {
                ShaderInputObject shaderInputObject;
                shaderInputObject.position = glm::vec4(inputObject->getTransformation().position, 0.0f);
                shaderInputObject.rotation = glm::mat4_cast(inputObject->getTransformation().rotation);
                shaderInputObject.scale = glm::vec4(inputObject->getTransformation().scale, 0.0f);
                shaderInputObject.bounds = inputObject->getModelPtr()->getAABB();
                shaderInputObject.lodCount = inputObject->getModelPtr()->getLODs().size();
                shaderInputObject.lodsOffset = model->getLODDataOffset();

                shaderInputObjects.push_back(shaderInputObject);
            }
        }
        renderingData.at(currentImage).objectCount = shaderInputObjects.size();
        stagingData.resize(stagingData.size() + shaderInputObjects.size() * sizeof(ShaderInputObject));
        memcpy(stagingData.data() + renderingData.at(currentImage).inputObjectsRegion.srcOffset, shaderInputObjects.data(), sizeof(ShaderInputObject) * shaderInputObjects.size());

        renderingData.at(currentImage).inputObjectsRegion.size = stagingData.size() - renderingData.at(currentImage).inputObjectsRegion.dstOffset; //input objects region
        stagingData.resize(PaperMemory::DeviceAllocation::padToMultiple(stagingData.size(), devicePtr->getGPUProperties().properties.limits.minStorageBufferOffsetAlignment));
    }

    void RenderPass::setRTStagingData(const std::unordered_map<Material *, MaterialNode> &renderTree)
    {

    }

    void RenderPass::copyStagingData()
    {

        //check if staging buffer and allocation need to be recreated
        if(renderingData.at(currentImage).stagingData.size() > newDataStagingBuffers.at(currentImage)->getSize())
        {
            //create staging buffer
            PaperMemory::BufferInfo newDataStagingBufferInfo = {};
            newDataStagingBufferInfo.queueFamilyIndices = {}; //only uses transfer
            newDataStagingBufferInfo.size = renderingData.at(currentImage).stagingData.size() * 1.2; //20% overhead
            newDataStagingBufferInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            newDataStagingBuffers.at(currentImage) = std::make_unique<PaperMemory::Buffer>(devicePtr->getDevice(), newDataStagingBufferInfo);

            //create staging allocation
            PaperMemory::DeviceAllocationInfo stagingAllocationInfo = {};
            stagingAllocationInfo.allocationSize = newDataStagingBuffers.at(currentImage)->getMemoryRequirements().size;
            stagingAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            stagingAllocations.at(currentImage) = std::make_unique<PaperMemory::DeviceAllocation>(devicePtr->getDevice(), devicePtr->getGPU(), stagingAllocationInfo);

            //assign allocation
            if(newDataStagingBuffers.at(currentImage)->assignAllocation(stagingAllocations.at(currentImage).get()) != 0)
            {
                throw std::runtime_error("Failed to assign allocation to staging buffer for rendering data");
            }
        }

        //write data
        PaperMemory::BufferWrite stagingWriteInfo = {};
        stagingWriteInfo.data = renderingData.at(currentImage).stagingData.data();
        stagingWriteInfo.size = renderingData.at(currentImage).stagingData.size();
        stagingWriteInfo.offset = 0;
        if(newDataStagingBuffers.at(currentImage)->writeToBuffer({ stagingWriteInfo }) != 0)
        {
            throw std::runtime_error("Failed to write to staging buffer");
        }

        //check if buffer rebuild is needed (ONLY ONE BUFFER ASSOCIATED WITH THIS ALLOCATION)
        if(renderingData.at(currentImage).stagingData.size() > renderingData.at(currentImage).bufferData->getSize() || //allocate new if oversized
            renderingData.at(currentImage).stagingData.size() < renderingData.at(currentImage).stagingData.size() * 0.5) //trim if below 50%
        {
            PaperMemory::BufferInfo bufferInfo = {};
            bufferInfo.queueFamilyIndices = {
                devicePtr->getQueues().at(PaperMemory::QueueType::GRAPHICS).queueFamilyIndex,
                devicePtr->getQueues().at(PaperMemory::QueueType::COMPUTE).queueFamilyIndex};
            bufferInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
            bufferInfo.size = renderingData.at(currentImage).stagingData.size() * 1.2; //allocate 20% overhead
            renderingData.at(currentImage).bufferData = std::make_unique<PaperMemory::Buffer>(devicePtr->getDevice(), bufferInfo);

            rebuildRenderDataAllocation(currentImage); //function also assigns allocation to buffer
        }

        //wait for fences
        std::vector<VkFence> waitFences = {
            bufferCopyFences.at(currentImage) //wait for raster to finish
        };
        vkWaitForFences(devicePtr->getDevice(), waitFences.size(), waitFences.data(), VK_TRUE, 3000000000);
        vkResetFences(devicePtr->getDevice(), waitFences.size(), waitFences.data());
        
        //copy to dedicated buffer
        std::vector<VkBufferCopy> copyRegions = {
            renderingData.at(currentImage).inputObjectsRegion,
            renderingData.at(currentImage).LODOffsetsRegion,
            renderingData.at(currentImage).meshLODOffsetsRegion,
            renderingData.at(currentImage).meshDrawCountsRegion
        };
        PaperRenderer::PaperMemory::SynchronizationInfo syncInfo;
        syncInfo.queueType = PaperMemory::QueueType::TRANSFER;
        syncInfo.waitPairs = {};
        syncInfo.signalPairs = { { bufferCopySemaphores.at(currentImage), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT } };
        syncInfo.fence = bufferCopyFences.at(currentImage);
        usedCmdBuffers.at(currentImage).push_back(renderingData.at(currentImage).bufferData->copyFromBufferRanges(
            *newDataStagingBuffers.at(currentImage), devicePtr->getQueues().at(PaperMemory::QueueType::TRANSFER).queueFamilyIndex, copyRegions, syncInfo));
    }

    void RenderPass::rasterPreProcess(const std::unordered_map<Material *, MaterialNode> &renderTree)
    {
        //wait for fences
        std::vector<VkFence> waitFences = {
            renderFences.at(currentImage) //wait for raster to finish
        };
        vkWaitForFences(devicePtr->getDevice(), waitFences.size(), waitFences.data(), VK_TRUE, 3000000000); //i give up on fixing the resize deadlock
        vkResetFences(devicePtr->getDevice(), waitFences.size(), waitFences.data());

        //get available image
        if(vkAcquireNextImageKHR(devicePtr->getDevice(),
            *(swapchainPtr->getSwapchainPtr()),
            UINT64_MAX,
            imageSemaphores.at(currentImage),
            VK_NULL_HANDLE, &currentImage) == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateFlag = true;
        }

        //free command buffers and reset descriptor pool
        for(PaperMemory::CommandBuffer& buffer : usedCmdBuffers.at(currentImage))
        {
            PaperMemory::Commands::freeCommandBuffer(devicePtr->getDevice(), buffer);
        }
        usedCmdBuffers.at(currentImage).clear();
        descriptorsPtr->refreshPools(currentImage);

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

    void RenderPass::rayTracePreProcess(const std::unordered_map<Material *, MaterialNode> &renderTree)
    {
        //wait for fences
        std::vector<VkFence> BLASWaitFences = {
            RTFences.at(currentImage)
        };
        vkWaitForFences(devicePtr->getDevice(), BLASWaitFences.size(), BLASWaitFences.data(), VK_TRUE, 3000000000);
        vkResetFences(devicePtr->getDevice(), BLASWaitFences.size(), BLASWaitFences.data());

        //get available image
        if(vkAcquireNextImageKHR(devicePtr->getDevice(),
            *(swapchainPtr->getSwapchainPtr()),
            UINT64_MAX,
            imageSemaphores.at(currentImage),
            VK_NULL_HANDLE, &currentImage) == VK_ERROR_OUT_OF_DATE_KHR)
        {
            recreateFlag = true;
        }

        //free command buffers and reset descriptor pool
        for(PaperMemory::CommandBuffer& buffer : usedCmdBuffers.at(currentImage))
        {
            PaperMemory::Commands::freeCommandBuffer(devicePtr->getDevice(), buffer);
        }
        usedCmdBuffers.at(currentImage).clear();
        descriptorsPtr->refreshPools(currentImage);

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

    void RenderPass::raster(const std::unordered_map<Material*, MaterialNode>& renderTree)
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

        //record draw commands
        for(const auto& [material, materialNode] : renderTree) //material
        {
            material->bind(graphicsCmdBuffer, currentImage);

            for(const auto& [materialInstance, instanceNode] : materialNode.instances) //material instances
            {
                materialInstance->bind(graphicsCmdBuffer, currentImage);
                instanceNode.objectBuffer->draw(graphicsCmdBuffer, renderingData.at(currentImage), currentImage);
            }
        }

        //end render "pass"
        vkCmdEndRendering(graphicsCmdBuffer);

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

    //----------OBJECT ADD/REMOVE FUNCTIONS----------//

    void RenderPass::rasterOrTrace(bool shouldRaster, const std::unordered_map<Material *, MaterialNode> &renderTree)
    {
        if(shouldRaster) //do raster, dont use ray tracing
        {
            rasterPreProcess(renderTree);
            raster(renderTree);
            frameEnd(renderSemaphores.at(currentImage)); //end with raster semaphore
        }
        else //do ray tracing, no raster
        {
            rayTracePreProcess(renderTree);
            frameEnd(TLASBuildSemaphores.at(currentImage)); //end with RT semaphore
        }

        if(recreateFlag)
        {
            recreateFlag = false;
            swapchainPtr->recreate();
            cameraPtr->updateCameraProjection();
            
            return;
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
    }

    void RenderPass::frameEnd(VkSemaphore waitSemaphore)
    {
        //presentation
        std::vector<VkSemaphore> presentWaitSemaphores = {
            waitSemaphore
        };

        VkResult returnResult;
        VkPresentInfoKHR presentSubmitInfo = {};
        presentSubmitInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentSubmitInfo.pNext = NULL;
        presentSubmitInfo.waitSemaphoreCount = presentWaitSemaphores.size();
        presentSubmitInfo.pWaitSemaphores = presentWaitSemaphores.data();
        presentSubmitInfo.swapchainCount = 1;
        presentSubmitInfo.pSwapchains = swapchainPtr->getSwapchainPtr();
        presentSubmitInfo.pImageIndices = &currentImage;
        presentSubmitInfo.pResults = &returnResult;

        //too lazy to properly fix this, it probably barely affects performance anyways
        devicePtr->getQueues().at(PaperMemory::QueueType::PRESENT).queues.at(0)->threadLock.lock();
        VkResult presentResult = vkQueuePresentKHR(devicePtr->getQueues().at(PaperMemory::QueueType::PRESENT).queues.at(0)->queue, &presentSubmitInfo);
        devicePtr->getQueues().at(PaperMemory::QueueType::PRESENT).queues.at(0)->threadLock.unlock();

        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) 
        {
            recreateFlag = true;
        }
    }

    void RenderPass::addModelInstance(ModelInstance* instance, uint64_t& selfIndex)
    {   
        selfIndex = renderingModels[instance->getModelPtr()].size();
        renderingModels[instance->getModelPtr()].push_back(instance);
    }

    void RenderPass::removeModelInstance(ModelInstance* instance, uint64_t& selfIndex)
    {
        //replace object index with the last element in the vector, change last elements self index to match, remove last element with pop_back()
        if(renderingModels.at(instance->getModelPtr()).size() > 1)
        {
            renderingModels.at(instance->getModelPtr()).at(selfIndex) = renderingModels.at(instance->getModelPtr()).back();
            renderingModels.at(instance->getModelPtr()).at(selfIndex)->setRendererIndex(selfIndex);
            renderingModels.at(instance->getModelPtr()).pop_back();
        }
        else
        {
            renderingModels.at(instance->getModelPtr()).clear();
        }
        
        selfIndex = UINT64_MAX;
    }
}