#include "RenderPass.h"
#include "PaperRenderer.h"

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

    void RasterPreprocessPipeline::submit(const PaperMemory::SynchronizationInfo& syncInfo, const RasterPreprocessSubmitInfo& submitInfo)
    {
        UBOInputData uboInputData;
        //uboInputData.bufferAddress = renderingData.bufferData->getBufferDeviceAddress();
        uboInputData.camPos = glm::vec4(submitInfo.camera->getTranslation().position, 1.0f);
        uboInputData.projection = submitInfo.camera->getProjection();
        uboInputData.view = submitInfo.camera->getViewMatrix();
        uboInputData.objectCount = rendererPtr->getModelInstanceReferences().size();
        //uboInputData.frustumData = submitInfo.camera->getFrustum();

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

        //set0 - binding 1: input objects
        VkDescriptorBufferInfo bufferWrite1Info = {};
        bufferWrite1Info.buffer = rendererPtr->getDeviceInstancesBufferPtr()->getBuffer();
        bufferWrite1Info.offset = 0;
        bufferWrite1Info.range = rendererPtr->getModelInstanceReferences().size() * sizeof(ModelInstance::ShaderModelInstance);

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

    /*void OldRenderPass::setRasterStagingData(const std::unordered_map<Material*, MaterialNode> &renderTree)
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
    }*/

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

        preprocessSignalSemaphores.resize(PaperMemory::Commands::getFrameCount());
        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            preprocessSignalSemaphores.at(i) = PaperMemory::Commands::getSemaphore(rendererPtr->getDevice()->getDevice());
        }

        //instances buffer
        //instancesBuffer
    }

    RenderPass::~RenderPass()
    {
        for(uint32_t i = 0; i < PaperMemory::Commands::getFrameCount(); i++)
        {
            vkDestroySemaphore(rendererPtr->getDevice()->getDevice(), preprocessSignalSemaphores.at(i), nullptr);
        }

        renderPasses.remove(this);
    }

    void RenderPass::rebuildAllocationsAndBuffers(RenderEngine* renderer)
    {
        //copy old buffer data from all render passes, rebuild, and gather information for new size
        std::unordered_map<RenderPass*, std::vector<char>> oldInstanceDatas;
        std::unordered_map<RenderPass*, std::vector<char>> oldInstanceMaterialDatas;
        VkDeviceSize newHostSize = 0;
        VkDeviceSize newDeviceSize = 0;
        
        for(auto renderPass = renderPasses.begin(); renderPass != renderPasses.end(); renderPass++)
        {
            //instance data
            std::vector<char> oldData((*renderPass)->renderPassInstances.size() * sizeof(ModelInstance::RenderPassInstance));
            memcpy(oldData.data(), (*renderPass)->hostInstancesBuffer->getHostDataPtr(), oldData.size());
            oldInstanceDatas[*renderPass] = (oldData);

            //instance material data
            (*renderPass)->hostInstancesDataBuffer->compact();
            std::vector<char> oldMaterialData((*renderPass)->hostInstancesDataBuffer->getStackLocation());
            oldInstanceMaterialDatas[*renderPass] = oldMaterialData;

            //rebuild buffers
            (*renderPass)->rebuildBuffers();

            newHostSize += PaperMemory::DeviceAllocation::padToMultiple(oldData.size(), (*renderPass)->hostInstancesBuffer->getMemoryRequirements().alignment);
            //newDeviceSize += PaperMemory::DeviceAllocation::padToMultiple(oldData.size(), (*renderPass)->deviceInstancesBuffer->getMemoryRequirements().alignment);
            newDeviceSize += PaperMemory::DeviceAllocation::padToMultiple(oldMaterialData.size(), (*renderPass)->hostInstancesDataBuffer->getBufferPtr()->getMemoryRequirements().alignment);

        }

        //rebuild allocations
        PaperMemory::DeviceAllocationInfo hostAllocationInfo = {};
        hostAllocationInfo.allocationSize = newHostSize;
        hostAllocationInfo.allocFlags = 0;
        hostAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        hostInstancesAllocation = std::make_unique<PaperMemory::DeviceAllocation>(renderer->getDevice()->getDevice(), renderer->getDevice()->getGPU(), hostAllocationInfo);

        PaperMemory::DeviceAllocationInfo deviceAllocationInfo = {};
        deviceAllocationInfo.allocationSize = newDeviceSize;
        deviceAllocationInfo.allocFlags = 0;
        deviceAllocationInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        deviceInstancesAllocation = std::make_unique<PaperMemory::DeviceAllocation>(renderer->getDevice()->getDevice(), renderer->getDevice()->getGPU(), deviceAllocationInfo);

        //assign buffer memory and re-copy
        for(auto renderPass = renderPasses.begin(); renderPass != renderPasses.end(); renderPass++)
        {
            //assign memory
            (*renderPass)->hostInstancesBuffer->assignAllocation(hostInstancesAllocation.get());
            //(*renderPass)->deviceInstancesBuffer->assignAllocation(deviceInstancesAllocation.get());
            (*renderPass)->hostInstancesDataBuffer->assignAllocation(hostInstancesAllocation.get());

            //re-copy
            memcpy((*renderPass)->hostInstancesBuffer->getHostDataPtr(), oldInstanceDatas.at(*renderPass).data(), oldInstanceDatas.at(*renderPass).size());
            (*renderPass)->hostInstancesDataBuffer->newWrite(oldInstanceMaterialDatas.at(*renderPass).data(), oldInstanceMaterialDatas.at(*renderPass).size(), NULL);
            
        }
    }

    void RenderPass::rebuildBuffers()
    {
        VkDeviceSize newInstancesBufferSize = std::max((VkDeviceSize)(renderPassInstances.size() * sizeof(ModelInstance::RenderPassInstance) * instancesOverhead), (VkDeviceSize)(sizeof(ModelInstance::RenderPassInstance) * 64));
        VkDeviceSize newInstancesMaterialDataBufferSize = ;//TODO IM TIRED ASF IM GOING TO BED

        PaperMemory::BufferInfo hostInstancesBufferInfo = {};
        hostInstancesBufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
        hostInstancesBufferInfo.size = newBufferSize;
        hostInstancesBufferInfo.usageFlags = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;
        hostInstancesBuffer = std::make_unique<PaperMemory::Buffer>(rendererPtr->getDevice()->getDevice(), hostInstancesBufferInfo);

        PaperMemory::BufferInfo hostInstancesMaterialDataBufferInfo = {};
        hostInstancesMaterialDataBufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
        hostInstancesMaterialDataBufferInfo.size = newBufferSize;
        hostInstancesMaterialDataBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;

        /*PaperMemory::BufferInfo deviceBufferInfo = {};
        deviceBufferInfo.queueFamiliesIndices = rendererPtr->getDevice()->getQueueFamiliesIndices();
        deviceBufferInfo.size = newBufferSize;
        deviceBufferInfo.usageFlags = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;
        deviceInstancesBuffer = std::make_unique<PaperMemory::Buffer>(rendererPtr->getDevice()->getDevice(), deviceBufferInfo);*/
    }

    void RenderPass::render(const RenderPassSynchronizationInfo& syncInfo)
    {
        //----------PRE-PROCESS----------//

        PaperMemory::SynchronizationInfo preprocessSyncInfo = {};
        preprocessSyncInfo.queueType = PaperMemory::QueueType::COMPUTE;
        preprocessSyncInfo.waitPairs = syncInfo.preprocessWaitPairs;
        preprocessSyncInfo.signalPairs = { { preprocessSignalSemaphores.at(*rendererPtr->getCurrentFramePtr()), VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT } };

        RasterPreprocessSubmitInfo preprocessSubmitInfo = {};
        preprocessSubmitInfo.camera = cameraPtr;

        rendererPtr->getRasterPreprocessPipeline()->submit(preprocessSyncInfo, preprocessSubmitInfo);

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
                    //meshGroups->draw(graphicsCmdBuffer, renderingData.at(*rendererPtr->getCurrentFramePtr()).bufferData->getBuffer(), *rendererPtr->getCurrentFramePtr());
                }
            }
        }

        //end rendering
        vkCmdEndRendering(graphicsCmdBuffer);

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

    void RenderPass::addInstance(ModelInstance *instance, std::vector<std::unordered_map<uint32_t, MaterialInstance *>> materials)
    {
        //"normalize" data
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
                        std::make_unique<CommonMeshGroup>(rendererPtr->getDevice(), rendererPtr->getDescriptorAllocator(), materialInstance->getBaseMaterialPtr()->getRasterPipeline(), this);
                }

                //add reference
                renderTree[(Material*)materialInstance->getBaseMaterialPtr()].instances[materialInstance]->addInstanceMeshes(instance, similarMeshes);
            }
        }

        //add reference
        instance->renderPassSelfReferences[this].selfIndex = renderPassInstances.size();
        renderPassInstances.push_back(instance);

        //copy data into buffer
        if(hostInstancesBuffer->getSize() < renderPassInstances.size() * sizeof(ModelInstance::RenderPassInstance))
        {
            rebuildAllocationsAndBuffers(rendererPtr);
        }

    }

    void RenderPass::removeInstance(ModelInstance *instance)
    {
        for(auto& [mesh, reference] : instance->renderPassSelfReferences.at(this).meshGroupReferences)
        {
            reference->removeInstanceMeshes(instance);
        }

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