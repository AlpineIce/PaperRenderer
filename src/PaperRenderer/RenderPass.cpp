#include "RenderPass.h"
#include "PaperRenderer.h"

#include <iostream>
#include <algorithm>

namespace PaperRenderer
{
    //----------PREPROCESS PIPELINES DEFINITIONS----------//

    RasterPreprocessPipeline::RasterPreprocessPipeline(RenderEngine& renderer, std::string fileDir)
        :ComputeShader(renderer),
        renderer(renderer)
    {
        //preprocess uniform buffer
        BufferInfo preprocessBufferInfo = {};
        preprocessBufferInfo.allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        preprocessBufferInfo.usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR;
        preprocessBufferInfo.size = sizeof(UBOInputData);
        uniformBuffer = std::make_unique<Buffer>(renderer, preprocessBufferInfo);
        
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
        throw std::runtime_error("todo fix binding 3 and 4 for raster");

        UBOInputData uboInputData = {};
        uboInputData.camPos = glm::vec4(renderPass.cameraPtr->getTranslation().position, 1.0f);
        uboInputData.projection = renderPass.cameraPtr->getProjection();
        uboInputData.view = renderPass.cameraPtr->getViewMatrix();
        uboInputData.materialDataPtr = renderPass.instancesDataBuffer->getBuffer().getBufferDeviceAddress();
        uboInputData.modelDataPtr = renderer.modelDataBuffer->getBuffer().getBufferDeviceAddress();
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
        bufferWrite1Info.buffer = renderer.instancesDataBuffer->getBuffer();
        bufferWrite1Info.offset = 0;
        bufferWrite1Info.range = renderer.renderingModelInstances.size() * sizeof(ModelInstance::ShaderModelInstance);

        BuffersDescriptorWrites bufferWrite1 = {};
        bufferWrite1.binding = 1;
        bufferWrite1.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bufferWrite1.infos = { bufferWrite1Info };

        //set0 - binding 2: input objects
        VkDescriptorBufferInfo bufferWrite2Info = {};
        bufferWrite2Info.buffer = renderPass.instancesBuffer->getBuffer();
        bufferWrite2Info.offset = 0;
        bufferWrite2Info.range = renderPass.renderPassInstances.size() * sizeof(ModelInstance::RenderPassInstance);

        BuffersDescriptorWrites bufferWrite2 = {};
        bufferWrite2.binding = 2;
        bufferWrite2.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bufferWrite2.infos = { bufferWrite2Info };

        //set0 - binding 3: instance counts
        VkDescriptorBufferInfo bufferWrite3Info = {};
        //bufferWrite3Info.buffer = CommonMeshGroup::getDrawCommandsBuffer()->getBuffer();
        bufferWrite3Info.offset = 0;
        bufferWrite3Info.range = VK_WHOLE_SIZE;

        BuffersDescriptorWrites bufferWrite3 = {};
        bufferWrite3.binding = 3;
        bufferWrite3.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bufferWrite3.infos = { bufferWrite3Info };

        //set0 - binding 4: output objects
        VkDescriptorBufferInfo bufferWrite4Info = {};
        //bufferWrite4Info.buffer = CommonMeshGroup::getModelMatricesBuffer()->getBuffer();
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

    RenderPass::RenderPass(RenderEngine& renderer, Camera* camera, MaterialInstance* defaultMaterialInstance)
        :renderer(renderer),
        cameraPtr(camera),
        defaultMaterialInstancePtr(defaultMaterialInstance)
    {
        rebuildMaterialDataBuffer();
        rebuildInstancesBuffer();

        renderer.renderPasses.push_back(this);
    }

    RenderPass::~RenderPass()
    {
        instancesBuffer.reset();
        instancesDataBuffer.reset();

        for(auto& [material, materialNode] : renderTree)
        {
            for(auto& [materialInstance, instanceNode] : materialNode.instances)
            {
                instanceNode.reset();
            }
        }

        renderer.renderPasses.remove(this);
    }

    void RenderPass::rebuildInstancesBuffer()
    {
        //create new instance buffer
        VkDeviceSize newInstancesBufferSize = std::max((VkDeviceSize)(renderPassInstances.size() * sizeof(ModelInstance::RenderPassInstance) * instancesOverhead), (VkDeviceSize)(sizeof(ModelInstance::RenderPassInstance) * 64));

        BufferInfo instancesBufferInfo = {};
        instancesBufferInfo.allocationFlags = 0;
        instancesBufferInfo.size = newInstancesBufferSize;
        instancesBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR;
        std::unique_ptr<Buffer> newInstancesBuffer = std::make_unique<Buffer>(renderer, instancesBufferInfo);

        //copy old data into new if old existed
        if(instancesBuffer)
        {
            VkBufferCopy instancesCopyRegion = {};
            instancesCopyRegion.srcOffset = 0;
            instancesCopyRegion.dstOffset = 0;
            instancesCopyRegion.size = std::min(renderPassInstances.size() * sizeof(ModelInstance::RenderPassInstance), instancesBuffer->getSize());

            if(instancesCopyRegion.size)
            {
                SynchronizationInfo syncInfo = {};
                syncInfo.queueType = TRANSFER;
                syncInfo.fence = renderer.getDevice().getCommands().getUnsignaledFence();
                newInstancesBuffer->copyFromBufferRanges(*instancesBuffer, { instancesCopyRegion }, syncInfo);
                vkWaitForFences(renderer.getDevice().getDevice(), 1, &syncInfo.fence, VK_TRUE, UINT64_MAX);
                vkDestroyFence(renderer.getDevice().getDevice(), syncInfo.fence, nullptr);
            }
            
        }

        //replace old buffer
        instancesBuffer = std::move(newInstancesBuffer);
    }


    void RenderPass::rebuildMaterialDataBuffer()
    {
        //create new material data buffer
        VkDeviceSize newMaterialDataBufferSize = 4096;
        VkDeviceSize newMaterialDataWriteSize = 0;
        if(instancesDataBuffer)
        {
            instancesDataBuffer->compact();
            newMaterialDataBufferSize = instancesDataBuffer->getDesiredLocation();
            newMaterialDataWriteSize = instancesDataBuffer->getStackLocation();
        }

        BufferInfo instancesMaterialDataBufferInfo = {};
        instancesMaterialDataBufferInfo.allocationFlags = 0;
        instancesMaterialDataBufferInfo.size = newMaterialDataBufferSize * instancesOverhead;
        instancesMaterialDataBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
        std::unique_ptr<FragmentableBuffer> newInstancesDataBuffer = std::make_unique<FragmentableBuffer>(renderer, instancesMaterialDataBufferInfo);
        newInstancesDataBuffer->setCompactionCallback([this](std::vector<CompactionResult> results){ handleMaterialDataCompaction(results); });

        //copy old data into new if old existed
        if(instancesDataBuffer)
        {
            //pseudo write for material data
            newInstancesDataBuffer->newWrite(NULL, newMaterialDataWriteSize, 1, NULL);

            VkBufferCopy materialDataCopyRegion = {};
            materialDataCopyRegion.srcOffset = 0;
            materialDataCopyRegion.dstOffset = 0;
            materialDataCopyRegion.size = newMaterialDataWriteSize;

            SynchronizationInfo syncInfo = {};
            syncInfo.queueType = TRANSFER;
            syncInfo.fence = renderer.getDevice().getCommands().getUnsignaledFence();
            newInstancesDataBuffer->getBuffer().copyFromBufferRanges(instancesDataBuffer->getBuffer(), { materialDataCopyRegion }, syncInfo);

            vkWaitForFences(renderer.getDevice().getDevice(), 1, &syncInfo.fence, VK_TRUE, UINT64_MAX);
            vkDestroyFence(renderer.getDevice().getDevice(), syncInfo.fence, nullptr);
        }
        
        //replace old buffer
        instancesDataBuffer = std::move(newInstancesDataBuffer);
    }

    void RenderPass::queueInstanceTransfers()
    {
        //verify mesh group buffers
        for(const auto& [material, materialInstanceNode] : renderTree) //material
        {
            for(const auto& [materialInstance, meshGroups] : materialInstanceNode.instances) //material instances
            {
                if(meshGroups)
                {
                    meshGroups->verifyBufferSize();
                }
            }
        }

        //check buffer sizes
        if(instancesBuffer->getSize() / sizeof(ModelInstance::ShaderModelInstance) < renderPassInstances.size() ||
            instancesBuffer->getSize() / sizeof(ModelInstance::ShaderModelInstance) > renderPassInstances.size() * 2)
        {
            rebuildInstancesBuffer(); //TODO SYNCHRONIZATION
        }

        //sort instances; remove duplicates
        std::sort(toUpdateInstances.begin(), toUpdateInstances.end());
        auto sortedInstances = std::unique(toUpdateInstances.begin(), toUpdateInstances.end());
        toUpdateInstances.erase(sortedInstances, toUpdateInstances.end());

        //material data pseudo writes
        for(ModelInstance* instance : toUpdateInstances)
        {
            //skip if instance is NULL
            if(!instance) continue;

            const std::vector<char>& materialData = instance->getRenderPassInstanceData(this);
            FragmentableBuffer::WriteResult writeResult = instancesDataBuffer->newWrite(NULL, materialData.size(), 8, &(instance->renderPassSelfReferences.at(this).LODsMaterialDataOffset));
            if(writeResult == FragmentableBuffer::OUT_OF_MEMORY)
            {
                rebuildMaterialDataBuffer();
                instancesDataBuffer->newWrite(NULL, materialData.size(), 8, &(instance->renderPassSelfReferences.at(this).LODsMaterialDataOffset));
            }
            else if(writeResult == FragmentableBuffer::COMPACTED)
            {
                //recursive redo
                queueInstanceTransfers();

                return;
            }
        }

        //queue instance data
        for(ModelInstance* instance : toUpdateInstances)
        {
            //skip if instance is NULL
            if(!instance) continue;

            //queue material data write
            const std::vector<char>& materialData = instance->getRenderPassInstanceData(this);
            renderer.getEngineStagingBuffer().queueDataTransfers(instancesDataBuffer->getBuffer(), instance->renderPassSelfReferences.at(this).LODsMaterialDataOffset, materialData);

            //write instance data
            ModelInstance::RenderPassInstance instanceShaderData = {};
            instanceShaderData.modelInstanceIndex = instance->rendererSelfIndex;
            instanceShaderData.LODsMaterialDataOffset = instance->renderPassSelfReferences.at(this).LODsMaterialDataOffset;
            instanceShaderData.isVisible = true;

            std::vector<char> instanceData(sizeof(ModelInstance::RenderPassInstance));
            memcpy(instanceData.data(), &instanceShaderData, instanceData.size());
            
            //queue data transfer
            renderer.getEngineStagingBuffer().queueDataTransfers(*instancesBuffer, sizeof(ModelInstance::AccelerationStructureInstance) * instance->rendererSelfIndex, instanceData);
        }

        //clear deques
        toUpdateInstances.clear();
    }

    void RenderPass::handleMaterialDataCompaction(std::vector<CompactionResult> results) //UNTESTED
    {
        //fix material data offsets
        for(const CompactionResult compactionResult : results)
        {
            for(ModelInstance* instance : renderPassInstances)
            {
                VkDeviceSize& materialDataOffset = instance->renderPassSelfReferences.at(this).LODsMaterialDataOffset;
                if(materialDataOffset > compactionResult.location)
                {
                    //shift stored location
                    materialDataOffset -= compactionResult.shiftSize;
                }
            }
        }

        //then queue data transfers
        for(ModelInstance* instance : renderPassInstances)
        {
            toUpdateInstances.push_front(instance);
        }
    }

    void RenderPass::handleCommonMeshGroupResize(std::vector<ModelInstance*> invalidInstances)
    {
        for(ModelInstance* instance : invalidInstances)
        {
            //queue data transfer
            toUpdateInstances.push_front(instance);
        }
    }

    void RenderPass::clearDrawCounts(VkCommandBuffer cmdBuffer)
    {
        //clear draw counts
        for(const auto& [material, materialInstanceNode] : renderTree) //material
        {
            for(const auto& [materialInstance, meshGroups] : materialInstanceNode.instances) //material instances
            {
                if(meshGroups)
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
                        .buffer = meshGroups->getDrawCommandsBuffer()->getBuffer(),
                        .offset = 0,
                        .size = VK_WHOLE_SIZE
                    };

                    VkDependencyInfo preClearDependency = {};
                    preClearDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    preClearDependency.pNext = NULL;
                    preClearDependency.bufferMemoryBarrierCount = 1;
                    preClearDependency.pBufferMemoryBarriers = &preClearMemBarrier;

                    vkCmdPipelineBarrier2(cmdBuffer, &preClearDependency);

                    //clear
                    meshGroups->clearDrawCommand(cmdBuffer);
                }
            }
        }
    }

    void RenderPass::render(VkCommandBuffer cmdBuffer, const RenderPassInfo& renderPassInfo)
    {
        if(renderPassInstances.size())
        {
            //perform data transfers
            queueInstanceTransfers();

            //pre-render barriers
            if(renderPassInfo.preRenderBarriers)
            {
                vkCmdPipelineBarrier2(cmdBuffer, renderPassInfo.preRenderBarriers);
            }

            //clear draw counts
            clearDrawCounts(cmdBuffer);
            
            //----------PRE-PROCESS----------//

            //compute shader
            renderer.getRasterPreprocessPipeline().submit(cmdBuffer, *this);

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
            vkCmdSetRasterizationSamplesEXT(cmdBuffer, renderer.getRendererState().msaaSamples);

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
                for(uint32_t meshIndex = 0; meshIndex < instance->getParentModelPtr()->getLODs().at(lodIndex).meshMaterialData.at(matIndex).meshes.size(); meshIndex++)
                {
                    similarMeshes.push_back(&instance->getParentModelPtr()->getLODs().at(lodIndex).meshMaterialData.at(matIndex).meshes.at(meshIndex));
                }

                //check if mesh group class is created
                if(!renderTree[(Material*)materialInstance->getBaseMaterialPtr()].instances.count(materialInstance))
                {
                    renderTree[(Material*)materialInstance->getBaseMaterialPtr()].instances[materialInstance] = 
                        std::make_unique<CommonMeshGroup>(renderer, this);
                }

                //add references
                renderTree[(Material*)materialInstance->getBaseMaterialPtr()].instances[materialInstance]->addInstanceMeshes(instance, similarMeshes);

                instance->renderPassSelfReferences[this].meshGroupReferences[&instance->getParentModelPtr()->getLODs().at(lodIndex).meshMaterialData.at(matIndex).meshes] = 
                    renderTree.at((Material*)materialInstance->getBaseMaterialPtr()).instances.at(materialInstance).get();
            }
        }

        //add reference
        instance->renderPassSelfReferences.at(this).selfIndex = renderPassInstances.size();
        renderPassInstances.push_back(instance);

        //set material data
        instance->setRenderPassInstanceData(this);

        //add instance to queue
        toUpdateInstances.push_front(instance);
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

            //queue data transfer
            toUpdateInstances.push_front(renderPassInstances.at(instance->renderPassSelfReferences.at(this).selfIndex));
            
            renderPassInstances.pop_back();
        }
        else
        {
            renderPassInstances.clear();
        }

        //null out any instances that may be queued
        for(ModelInstance*& thisInstance : toUpdateInstances)
        {
            if(thisInstance == instance)
            {
                thisInstance = NULL;
            }
        }

        //remove data from fragmenable buffer if referenced
        if(instance->renderPassSelfReferences.at(this).LODsMaterialDataOffset != UINT64_MAX)
        {
            instancesDataBuffer->removeFromRange(instance->renderPassSelfReferences.at(this).LODsMaterialDataOffset, instance->getRenderPassInstanceData(this).size());
        }

        instance->renderPassSelfReferences.erase(this);
    }
}