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
        //preprocess uniform buffer
        BufferInfo preprocessBufferInfo = {};
        preprocessBufferInfo.allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        preprocessBufferInfo.usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR;
        preprocessBufferInfo.size = sizeof(UBOInputData);
        uniformBuffer = std::make_unique<Buffer>(rendererPtr, preprocessBufferInfo);
        
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

        VkDescriptorSetLayoutBinding instanceCountsDescriptor = {};
        instanceCountsDescriptor.binding = 3;
        instanceCountsDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        instanceCountsDescriptor.descriptorCount = 1;
        instanceCountsDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        descriptorSets[0].descriptorBindings[3] = instanceCountsDescriptor;

        VkDescriptorSetLayoutBinding modelMatricesDescriptor = {};
        modelMatricesDescriptor.binding = 4;
        modelMatricesDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        modelMatricesDescriptor.descriptorCount = 1;
        modelMatricesDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        descriptorSets[0].descriptorBindings[4] = modelMatricesDescriptor;

        buildPipeline();
    }
    
    RasterPreprocessPipeline::~RasterPreprocessPipeline()
    {
        uniformBuffer.reset();
    }

    void RasterPreprocessPipeline::submit(VkCommandBuffer cmdBuffer, const RenderPass& renderPass)
    {
        UBOInputData uboInputData = {};
        uboInputData.camPos = glm::vec4(renderPass.cameraPtr->getTranslation().position, 1.0f);
        uboInputData.projection = renderPass.cameraPtr->getProjection();
        uboInputData.view = renderPass.cameraPtr->getViewMatrix();
        uboInputData.materialDataPtr = renderPass.deviceInstancesDataBuffer->getBufferDeviceAddress();
        uboInputData.modelDataPtr = rendererPtr->modelDataBuffer->getBuffer()->getBufferDeviceAddress();
        uboInputData.objectCount = renderPass.renderPassInstances.size();
        uboInputData.doCulling = true;

        BufferWrite write = {};
        write.data = &uboInputData;
        write.size = sizeof(UBOInputData);
        write.offset = 0;

        uniformBuffer->writeToBuffer({ write });

        //set0 - binding 0: UBO input data
        VkDescriptorBufferInfo bufferWrite0Info = {};
        bufferWrite0Info.buffer = uniformBuffer->getBuffer();
        bufferWrite0Info.offset = 0;
        bufferWrite0Info.range = sizeof(UBOInputData);

        BuffersDescriptorWrites bufferWrite0 = {};
        bufferWrite0.binding = 0;
        bufferWrite0.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bufferWrite0.infos = { bufferWrite0Info };

        //set0 - binding 1: model instances
        VkDescriptorBufferInfo bufferWrite1Info = {};
        bufferWrite1Info.buffer = rendererPtr->instancesDataBuffer->getBuffer();
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

        //set0 - binding 3: instance counts
        VkDescriptorBufferInfo bufferWrite3Info = {};
        bufferWrite3Info.buffer = CommonMeshGroup::getDrawCommandsBuffer()->getBuffer();
        bufferWrite3Info.offset = 0;
        bufferWrite3Info.range = VK_WHOLE_SIZE;

        BuffersDescriptorWrites bufferWrite3 = {};
        bufferWrite3.binding = 3;
        bufferWrite3.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bufferWrite3.infos = { bufferWrite3Info };

        //set0 - binding 4: output objects
        VkDescriptorBufferInfo bufferWrite4Info = {};
        bufferWrite4Info.buffer = CommonMeshGroup::getModelMatricesBuffer()->getBuffer();
        bufferWrite4Info.offset = 0;
        bufferWrite4Info.range = VK_WHOLE_SIZE;

        BuffersDescriptorWrites bufferWrite4 = {};
        bufferWrite4.binding = 4;
        bufferWrite4.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bufferWrite4.infos = { bufferWrite4Info };

        //----------DISPATCH COMMANDS----------//

        bind(cmdBuffer);

        DescriptorWrites descriptorWritesInfo = {};
        descriptorWritesInfo.bufferWrites = { bufferWrite0, bufferWrite1, bufferWrite2, bufferWrite3, bufferWrite4 };
        descriptorWrites[0] = descriptorWritesInfo;
        writeDescriptorSet(cmdBuffer, 0);

        //dispatch
        workGroupSizes.x = ((renderPass.renderPassInstances.size()) / 128) + 1;
        dispatch(cmdBuffer);
    }

    //----------RENDER PASS DEFINITIONS----------//

    std::list<RenderPass*> RenderPass::renderPasses;

    RenderPass::RenderPass(RenderEngine* renderer, Camera* camera, MaterialInstance* defaultMaterialInstance)
        :rendererPtr(renderer),
        cameraPtr(camera),
        defaultMaterialInstancePtr(defaultMaterialInstance)
    {
        renderPasses.push_back(this);
        rebuildBuffers();
    }

    RenderPass::~RenderPass()
    {
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
    }

    void RenderPass::rebuildBuffers()
    {
        //copy old data
        struct OldData
        {
            std::vector<char> oldInstanceData;
            std::vector<char> oldInstanceMaterialData;
        } oldData;

        //instance data
        oldData.oldInstanceData.resize(renderPassInstances.size() * sizeof(ModelInstance::RenderPassInstance));
        if(hostInstancesBuffer)
        {
            BufferWrite read = {};
            read.offset = 0;
            read.size = oldData.oldInstanceData.size();
            read.data = oldData.oldInstanceData.data();
            hostInstancesBuffer->readFromBuffer({ read });
            hostInstancesBuffer.reset();
        }

        //instance material data
        VkDeviceSize newMaterialDataBufferSize = 4096;
        if(hostInstancesBuffer)
        {
            hostInstancesDataBuffer->compact();
            newMaterialDataBufferSize = hostInstancesDataBuffer->getDesiredLocation();
            oldData.oldInstanceMaterialData.resize(hostInstancesDataBuffer->getStackLocation());

            BufferWrite read = {};
            read.offset = 0;
            read.size = oldData.oldInstanceData.size();
            read.data = oldData.oldInstanceData.data();
            hostInstancesDataBuffer->getBuffer()->readFromBuffer({ read });
            hostInstancesDataBuffer.reset();
        }

        VkDeviceSize newInstancesBufferSize = std::max((VkDeviceSize)(renderPassInstances.size() * sizeof(ModelInstance::RenderPassInstance) * instancesOverhead), (VkDeviceSize)(sizeof(ModelInstance::RenderPassInstance) * 64));
        VkDeviceSize newInstancesMaterialDataBufferSize = newMaterialDataBufferSize * instancesOverhead;

        BufferInfo hostInstancesBufferInfo = {};
        hostInstancesBufferInfo.allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        hostInstancesBufferInfo.size = newInstancesBufferSize;
        hostInstancesBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
        hostInstancesBuffer = std::make_unique<Buffer>(rendererPtr, hostInstancesBufferInfo);

        BufferInfo hostInstancesMaterialDataBufferInfo = {};
        hostInstancesMaterialDataBufferInfo.allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        hostInstancesMaterialDataBufferInfo.size = newInstancesMaterialDataBufferSize;
        hostInstancesMaterialDataBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR;
        hostInstancesDataBuffer = std::make_unique<FragmentableBuffer>(rendererPtr, hostInstancesMaterialDataBufferInfo);
        hostInstancesDataBuffer->setCompactionCallback([this](std::vector<CompactionResult> results){ handleMaterialDataCompaction(results); });

        BufferInfo deviceInstancesBufferInfo = {};
        deviceInstancesBufferInfo.allocationFlags = 0;
        deviceInstancesBufferInfo.size = newInstancesBufferSize;
        deviceInstancesBufferInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;
        deviceInstancesBuffer = std::make_unique<Buffer>(rendererPtr, deviceInstancesBufferInfo);

        BufferInfo deviceInstancesMaterialDataBufferInfo = {};
        deviceInstancesMaterialDataBufferInfo.allocationFlags = 0;
        deviceInstancesMaterialDataBufferInfo.size = newInstancesMaterialDataBufferSize;
        deviceInstancesMaterialDataBufferInfo.usageFlags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
        deviceInstancesDataBuffer = std::make_unique<Buffer>(rendererPtr, deviceInstancesMaterialDataBufferInfo);

        //re-copy
        BufferWrite write = {};
        write.offset = 0;
        write.size = oldData.oldInstanceData.size();
        write.data = oldData.oldInstanceData.data();
        hostInstancesBuffer->writeToBuffer({ write });

        hostInstancesDataBuffer->newWrite(oldData.oldInstanceMaterialData.data(), oldData.oldInstanceMaterialData.size(), 0, NULL);
    }

    void RenderPass::handleMaterialDataCompaction(std::vector<CompactionResult> results) //UNTESTED
    {
        for(const CompactionResult compactionResult : results)
        {
            for(ModelInstance* instance : renderPassInstances)
            {
                ModelInstance::RenderPassInstance renderPassInstanceData;

                //read and write data
                BufferWrite read = {};
                read.offset = sizeof(ModelInstance::RenderPassInstance) * instance->renderPassSelfReferences.at(this).selfIndex;
                read.size = sizeof(ModelInstance::RenderPassInstance);
                read.data = &renderPassInstanceData;
                hostInstancesBuffer->readFromBuffer({ read });

                if(renderPassInstanceData.LODsMaterialDataOffset > compactionResult.location)
                {
                    renderPassInstanceData.LODsMaterialDataOffset -= compactionResult.shiftSize;

                    //write data
                    BufferWrite write = {};
                    write.offset = offsetof(ModelInstance::RenderPassInstance, LODsMaterialDataOffset) + (sizeof(ModelInstance::RenderPassInstance) * instance->renderPassSelfReferences.at(this).selfIndex);
                    write.size = sizeof(ModelInstance::RenderPassInstance::LODsMaterialDataOffset);
                    write.data = &renderPassInstanceData.LODsMaterialDataOffset;
                    hostInstancesBuffer->writeToBuffer({ write });
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

            ModelInstance::RenderPassInstance renderPassInstanceData;

            //read data
            BufferWrite read = {};
            read.offset = sizeof(ModelInstance::RenderPassInstance) * instance->renderPassSelfReferences.at(this).selfIndex;
            read.size = sizeof(ModelInstance::RenderPassInstance);
            read.data = &renderPassInstanceData;
            hostInstancesDataBuffer->getBuffer()->readFromBuffer({ read });

            //write data
            BufferWrite write = {};
            write.offset = renderPassInstanceData.LODsMaterialDataOffset;
            write.size = materialData.size();
            write.data = materialData.data();
            hostInstancesDataBuffer->getBuffer()->writeToBuffer({ write });
        }
    }

    void RenderPass::clearDrawCounts(VkCommandBuffer cmdBuffer)
    {
        //memory barrier
        VkBufferMemoryBarrier2 preClearMemBarrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = CommonMeshGroup::getDrawCommandsBuffer()->getBuffer(),
            .offset = 0,
            .size = VK_WHOLE_SIZE
        };

        VkDependencyInfo preClearDependency = {};
        preClearDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        preClearDependency.pNext = NULL;
        preClearDependency.bufferMemoryBarrierCount = 1;
        preClearDependency.pBufferMemoryBarriers = &preClearMemBarrier;

        vkCmdPipelineBarrier2(cmdBuffer, &preClearDependency);

        //clear draw counts
        for(const auto& [material, materialInstanceNode] : renderTree) //material
        {
            for(const auto& [materialInstance, meshGroups] : materialInstanceNode.instances) //material instances
            {
                if(meshGroups)
                {
                    meshGroups->clearDrawCommand(cmdBuffer);
                }
            }
        }

        //memory barrier
        VkBufferMemoryBarrier2 postClearMemBarrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = CommonMeshGroup::getDrawCommandsBuffer()->getBuffer(),
            .offset = 0,
            .size = VK_WHOLE_SIZE
        };

        VkDependencyInfo postClearDependency = {};
        postClearDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        postClearDependency.pNext = NULL;
        postClearDependency.bufferMemoryBarrierCount = 1;
        postClearDependency.pBufferMemoryBarriers = &postClearMemBarrier;

        vkCmdPipelineBarrier2(cmdBuffer, &postClearDependency);
    }

    void RenderPass::render(VkCommandBuffer cmdBuffer, const RenderPassInfo& renderPassInfo)
    {
        if(renderPassInstances.size())
        {
            //pre-render barriers
            if(renderPassInfo.preRenderBarriers)
            {
                vkCmdPipelineBarrier2(cmdBuffer, renderPassInfo.preRenderBarriers);
            }
            
            //verify mesh group buffers
            handleCommonMeshGroupResize(CommonMeshGroup::verifyBuffersSize(rendererPtr));

            //----------DATA COPY----------//

            //instances buffer copy
            VkBufferCopy2 instancesRegion = {};
            instancesRegion.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
            instancesRegion.pNext = NULL;
            instancesRegion.dstOffset = 0;
            instancesRegion.srcOffset = 0;
            instancesRegion.size = renderPassInstances.size() * sizeof(ModelInstance::RenderPassInstance);

            VkCopyBufferInfo2 instancesBufferCopyInfo = {};
            instancesBufferCopyInfo.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
            instancesBufferCopyInfo.pNext = NULL;
            instancesBufferCopyInfo.regionCount = 1;
            instancesBufferCopyInfo.pRegions = &instancesRegion;
            instancesBufferCopyInfo.srcBuffer = hostInstancesBuffer->getBuffer();
            instancesBufferCopyInfo.dstBuffer = deviceInstancesBuffer->getBuffer();
            
            vkCmdCopyBuffer2(cmdBuffer, &instancesBufferCopyInfo);

            //material data buffer copy
            VkBufferCopy2 materialDataRegion = {};
            materialDataRegion.sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2;
            materialDataRegion.pNext = NULL;
            materialDataRegion.dstOffset = 0;
            materialDataRegion.srcOffset = 0;
            materialDataRegion.size = hostInstancesDataBuffer->getStackLocation();

            VkCopyBufferInfo2 materialDataBufferCopyInfo = {};
            materialDataBufferCopyInfo.sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2;
            materialDataBufferCopyInfo.pNext = NULL;
            materialDataBufferCopyInfo.regionCount = 1;
            materialDataBufferCopyInfo.pRegions = &materialDataRegion;
            materialDataBufferCopyInfo.srcBuffer = hostInstancesDataBuffer->getBuffer()->getBuffer();
            materialDataBufferCopyInfo.dstBuffer = deviceInstancesDataBuffer->getBuffer();
            
            vkCmdCopyBuffer2(cmdBuffer, &materialDataBufferCopyInfo);

            //clear draw counts
            clearDrawCounts(cmdBuffer);

            //memory barriers
            std::vector<VkBufferMemoryBarrier2> bufferTransferMemBarriers;

            //instances buffer
            bufferTransferMemBarriers.push_back({
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .pNext = NULL,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = deviceInstancesBuffer->getBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE
            });

            //material data buffer
            bufferTransferMemBarriers.push_back({
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .pNext = NULL,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = deviceInstancesDataBuffer->getBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE
            });

            VkDependencyInfo bufferTransferDependencyInfo = {};
            bufferTransferDependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            bufferTransferDependencyInfo.pNext = NULL;
            bufferTransferDependencyInfo.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
            bufferTransferDependencyInfo.bufferMemoryBarrierCount = bufferTransferMemBarriers.size();
            bufferTransferDependencyInfo.pBufferMemoryBarriers = bufferTransferMemBarriers.data();

            vkCmdPipelineBarrier2(cmdBuffer, &bufferTransferDependencyInfo);
            
            //----------PRE-PROCESS----------//

            //compute shader
            rendererPtr->getRasterPreprocessPipeline()->submit(cmdBuffer, *this);

            //memory barrier
            VkMemoryBarrier2 preprocessMemBarrier = {};
            preprocessMemBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
            preprocessMemBarrier.pNext = NULL;
            preprocessMemBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            preprocessMemBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
            preprocessMemBarrier.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
            preprocessMemBarrier.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT;

            VkDependencyInfo preprocessDependencyInfo = {};
            preprocessDependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            preprocessDependencyInfo.pNext = NULL;
            preprocessDependencyInfo.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
            preprocessDependencyInfo.memoryBarrierCount = 1;
            preprocessDependencyInfo.pMemoryBarriers = &preprocessMemBarrier;
            
            vkCmdPipelineBarrier2(cmdBuffer, &preprocessDependencyInfo);

            //----------RENDER PASS----------//

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

            vkCmdBeginRendering(cmdBuffer, &renderInfo);

            //scissors (plural) and viewports
            vkCmdSetViewportWithCount(cmdBuffer, renderPassInfo.viewports.size(), renderPassInfo.viewports.data());
            vkCmdSetScissorWithCount(cmdBuffer, renderPassInfo.scissors.size(), renderPassInfo.scissors.data());

            //MSAA samples
            vkCmdSetRasterizationSamplesEXT(cmdBuffer, rendererPtr->getRendererState()->msaaSamples);

            //compare op
            vkCmdSetDepthCompareOp(cmdBuffer, renderPassInfo.depthCompareOp);

            //----------MAIN PASS----------//

            //record draw commands
            for(const auto& [material, materialInstanceNode] : renderTree) //material
            {
                material->bind(cmdBuffer, cameraPtr);
                for(const auto& [materialInstance, meshGroups] : materialInstanceNode.instances) //material instances
                {
                    if(meshGroups)
                    {
                        materialInstance->bind(cmdBuffer);
                        meshGroups->draw(cmdBuffer, *material->getRasterPipeline());
                    }
                }
            }

            //end rendering
            vkCmdEndRendering(cmdBuffer);

            //post-render barriers
            if(renderPassInfo.postRenderBarriers)
            {
                vkCmdPipelineBarrier2(cmdBuffer, renderPassInfo.postRenderBarriers);
            }
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
            rebuildBuffers();
        }

        //copy data into buffer
        instance->setRenderPassInstanceData(this);
        std::vector<char> materialData = instance->getRenderPassInstanceData(this);

        VkDeviceSize materialDataLocation = 0;
        if(hostInstancesDataBuffer->newWrite(materialData.data(), materialData.size(), 8, &materialDataLocation) == FragmentableBuffer::OUT_OF_MEMORY)
        {
            rebuildBuffers();
            hostInstancesDataBuffer->newWrite(materialData.data(), materialData.size(), 8, &materialDataLocation);
        }

        ModelInstance::RenderPassInstance shaderData = {};
        shaderData.modelInstanceIndex = instance->rendererSelfIndex;
        shaderData.LODsMaterialDataOffset = materialDataLocation;
        shaderData.isVisible = true;

        //write instance data
        BufferWrite instanceWrite = {};
        instanceWrite.offset = sizeof(ModelInstance::RenderPassInstance) * instance->renderPassSelfReferences.at(this).selfIndex;
        instanceWrite.size = sizeof(ModelInstance::RenderPassInstance);
        instanceWrite.data = &shaderData;
        hostInstancesDataBuffer->getBuffer()->writeToBuffer({ instanceWrite });

        //reset material data
        instance->setRenderPassInstanceData(this);
        materialData = instance->getRenderPassInstanceData(this);
        
        BufferWrite instanceDataWrite = {};
        instanceDataWrite.offset = shaderData.LODsMaterialDataOffset;
        instanceDataWrite.size = materialData.size();
        instanceDataWrite.data = materialData.data();
        hostInstancesDataBuffer->getBuffer()->writeToBuffer({ instanceDataWrite });
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