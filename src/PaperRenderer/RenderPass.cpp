#include "RenderPass.h"
#include "PaperRenderer.h"

#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <future>
#include <functional>

namespace PaperRenderer
{
    //----------PREPROCESS PIPELINES DEFINITIONS----------//

    RasterPreprocessPipeline::RasterPreprocessPipeline(RenderEngine& renderer, const std::vector<uint32_t>& shaderData)
        :computeShader(renderer, {
            .shaderInfo = {
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .data = shaderData
            },
            .descriptors = {
                { 0, {
                    {
                        .binding = 0,
                        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        .descriptorCount = 1,
                        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                        .pImmutableSamplers = NULL
                    },
                    {
                        .binding = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        .descriptorCount = 1,
                        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                        .pImmutableSamplers = NULL
                    },
                    {
                        .binding = 2,
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                        .descriptorCount = 1,
                        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                        .pImmutableSamplers = NULL
                    }
                }}
            },
            .pcRanges = {}
        }),
        renderer(renderer)
    {
        //log constructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "RasterPreprocessPipeline constructor finished"
        });
    }
    
    RasterPreprocessPipeline::~RasterPreprocessPipeline()
    {
        //log destructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "RasterPreprocessPipeline destructor finished"
        });
    }

    void RasterPreprocessPipeline::submit(VkCommandBuffer cmdBuffer, const RenderPass& renderPass, const Camera& camera)
    {
        UBOInputData uboInputData = {};
        uboInputData.camPos = glm::vec4(camera.getPosition(), 1.0f);
        uboInputData.projection = camera.getProjection();
        uboInputData.view = camera.getViewMatrix();
        uboInputData.materialDataPtr = renderPass.instancesDataBuffer->getBuffer().getBufferDeviceAddress();
        uboInputData.modelDataPtr = renderer.modelDataBuffer->getBuffer().getBufferDeviceAddress();
        uboInputData.objectCount = renderPass.renderPassInstances.size();
        uboInputData.doCulling = true;

        BufferWrite write = {};
        write.data = &uboInputData;
        write.size = sizeof(UBOInputData);
        write.offset = 0;

        renderPass.preprocessUniformBuffer.writeToBuffer({ write });

        //set0 - binding 0: UBO input data
        VkDescriptorBufferInfo bufferWrite0Info = {};
        bufferWrite0Info.buffer = renderPass.preprocessUniformBuffer.getBuffer();
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

        //dispatch
        const DescriptorWrites descriptorWritesInfo = {
            .bufferWrites = { bufferWrite0, bufferWrite1, bufferWrite2 }
        };
        computeShader.dispatch(cmdBuffer, { { 0, descriptorWritesInfo } }, glm::uvec3((renderPass.renderPassInstances.size() / 128) + 1, 1, 1));
    }

    //----------RENDER PASS DEFINITIONS----------//

    RenderPass::RenderPass(RenderEngine& renderer, MaterialInstance& defaultMaterialInstance)
        :preprocessUniformBuffer(renderer, {
            .size = sizeof(RasterPreprocessPipeline::UBOInputData),
            .usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
            .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        }),
        renderer(renderer),
        defaultMaterialInstance(defaultMaterialInstance)
    {
        renderer.renderPasses.push_back(this);
    }

    RenderPass::~RenderPass()
    {
        instancesBuffer.reset();
        instancesDataBuffer.reset();

        for(ModelInstance* instance : renderPassInstances)
        {
            removeInstance(*instance);
        }

        renderer.renderPasses.remove(this);
    }

    void RenderPass::rebuildInstancesBuffer()
    {
        //Timer
        Timer timer(renderer, "Rebuild RenderPass Instances Buffer", IRREGULAR);

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
        //Timer
        Timer timer(renderer, "Rebuild RenderPass Material Data Buffer", IRREGULAR);

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
        //Timer
        Timer timer(renderer, "RenderPass Queue instance Transfers", REGULAR);

        //verify mesh group buffers
        for(auto& [material, materialInstanceNode] : renderTree) //material
        {
            for(auto& [materialInstance, meshGroups] : materialInstanceNode.instances) //material instances
            {
                const std::vector<ModelInstance*> meshGroupUpdatedInstances = meshGroups.verifyBufferSize();
                toUpdateInstances.insert(toUpdateInstances.end(), meshGroupUpdatedInstances.begin(), meshGroupUpdatedInstances.end());
            }
        }

        //verify buffers
        if(!instancesBuffer || instancesBuffer->getSize() / sizeof(ModelInstance::RenderPassInstance) < renderPassInstances.size())
        {
            rebuildInstancesBuffer();
        }
        if(!instancesDataBuffer)
        {
            rebuildMaterialDataBuffer();
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

            instance->setRenderPassInstanceData(this);
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
            renderer.getStagingBuffer().queueDataTransfers(instancesDataBuffer->getBuffer(), instance->renderPassSelfReferences.at(this).LODsMaterialDataOffset, materialData);

            //write instance data
            ModelInstance::RenderPassInstance instanceShaderData = {};
            instanceShaderData.modelInstanceIndex = instance->rendererSelfIndex;
            instanceShaderData.LODsMaterialDataOffset = instance->renderPassSelfReferences.at(this).LODsMaterialDataOffset;
            instanceShaderData.isVisible = true;

            std::vector<char> instanceData(sizeof(ModelInstance::RenderPassInstance));
            memcpy(instanceData.data(), &instanceShaderData, instanceData.size());
            
            //queue data transfer
            renderer.getStagingBuffer().queueDataTransfers(*instancesBuffer, sizeof(ModelInstance::RenderPassInstance) * instance->renderPassSelfReferences.at(this).selfIndex, instanceData);
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
            for(const auto& [materialInstance, meshGroup] : materialInstanceNode.instances) //material instances
            {
                //clear
                meshGroup.clearDrawCommand(cmdBuffer);
            }
        }
    }

    std::vector<VkCommandBuffer> RenderPass::render(const RenderPassInfo& renderPassInfo)
    {
        //Timer
        Timer timer(renderer, "RenderPass Render Recording", REGULAR);

        //----------CLEAR DRAW COUNTS----------//

        VkCommandBuffer cmdBuffer = renderer.getDevice().getCommands().getCommandBuffer(GRAPHICS);

        VkCommandBufferBeginInfo cmdBufferBeginInfo = {};
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBufferBeginInfo.pNext = NULL;
        cmdBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        cmdBufferBeginInfo.pInheritanceInfo = NULL;
        
        vkBeginCommandBuffer(cmdBuffer, &cmdBufferBeginInfo);

        //pre-render barriers
        if(renderPassInfo.preRenderBarriers)
        {
            vkCmdPipelineBarrier2(cmdBuffer, renderPassInfo.preRenderBarriers);
        }

        //clear draw counts
        clearDrawCounts(cmdBuffer);
        
        //----------PRE-PROCESS----------//

        //compute shader
        renderer.getRasterPreprocessPipeline().submit(cmdBuffer, *this, renderPassInfo.camera);

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
        VkRenderingInfo renderInfo = {};
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
        vkCmdSetRasterizationSamplesEXT(cmdBuffer, renderPassInfo.sampleCount);

        //compare op
        vkCmdSetDepthCompareOp(cmdBuffer, renderPassInfo.depthCompareOp);

        //----------MAIN PASS----------//

        //record draw commands
        for(const auto& [material, materialInstanceNode] : renderTree) //material
        {
            std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites> materialDescriptorWrites;
            material->bind(cmdBuffer, renderPassInfo.camera, materialDescriptorWrites);
            for(const auto& [materialInstance, meshGroups] : materialInstanceNode.instances) //material instances
            {
                std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites> instanceDescriptorWrites;
                materialInstance->bind(cmdBuffer, instanceDescriptorWrites);
                meshGroups.draw(cmdBuffer, *material);
            }
        }

        //end rendering
        vkCmdEndRendering(cmdBuffer);

        //post-render barriers
        if(renderPassInfo.postRenderBarriers)
        {
            vkCmdPipelineBarrier2(cmdBuffer, renderPassInfo.postRenderBarriers);
        }

        //end cmd buffer
        vkEndCommandBuffer(cmdBuffer);

        renderer.getDevice().getCommands().unlockCommandBuffer(cmdBuffer);

        return { cmdBuffer };
    }

    void RenderPass::addInstance(ModelInstance& instance, std::vector<std::unordered_map<uint32_t, MaterialInstance*>> materials)
    {
        //material data
        materials.resize(instance.getParentModel().getLODs().size());
        for(uint32_t lodIndex = 0; lodIndex < instance.getParentModel().getLODs().size(); lodIndex++)
        {
            for(uint32_t matIndex = 0; matIndex < instance.getParentModel().getLODs().at(lodIndex).materialMeshes.size(); matIndex++) //iterate materials in LOD
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
                    materialInstance = &defaultMaterialInstance;
                }

                //get mesh using same material
                const LODMesh& similarMesh = instance.getParentModel().getLODs().at(lodIndex).materialMeshes.at(matIndex).mesh;

                //check if mesh group class is created
                if(!renderTree[(Material*)&materialInstance->getBaseMaterial()].instances.count(materialInstance))
                {
                    renderTree[(Material*)&materialInstance->getBaseMaterial()].instances.emplace(std::piecewise_construct, std::forward_as_tuple(materialInstance), std::forward_as_tuple(renderer, this));
                }

                //add references
                renderTree[(Material*)&materialInstance->getBaseMaterial()].instances.at(materialInstance).addInstanceMesh(instance, similarMesh);

                instance.renderPassSelfReferences[this].meshGroupReferences[&instance.getParentModel().getLODs().at(lodIndex).materialMeshes.at(matIndex).mesh] = 
                    &renderTree.at((Material*)&materialInstance->getBaseMaterial()).instances.at(materialInstance);
            }
        }

        //add reference
        instance.renderPassSelfReferences.at(this).selfIndex = renderPassInstances.size();
        renderPassInstances.push_back(&instance);

        //add instance to queue
        toUpdateInstances.push_front(&instance);
    }

    void RenderPass::removeInstance(ModelInstance& instance)
    {
        for(auto& [mesh, reference] : instance.renderPassSelfReferences.at(this).meshGroupReferences)
        {
            reference->removeInstanceMeshes(instance);
        }
        instance.renderPassSelfReferences.at(this).meshGroupReferences.clear();

        //remove reference
        if(renderPassInstances.size() > 1)
        {
            uint32_t& selfReference = instance.renderPassSelfReferences.at(this).selfIndex;
            renderPassInstances.at(selfReference) = renderPassInstances.back();
            renderPassInstances.at(selfReference)->renderPassSelfReferences.at(this).selfIndex = selfReference;

            //queue data transfer
            toUpdateInstances.push_front(renderPassInstances.at(instance.renderPassSelfReferences.at(this).selfIndex));
            
            renderPassInstances.pop_back();
        }
        else
        {
            renderPassInstances.clear();
        }

        //null out any instances that may be queued
        for(ModelInstance*& thisInstance : toUpdateInstances)
        {
            if(thisInstance == &instance)
            {
                thisInstance = NULL;
            }
        }

        //remove data from fragmenable buffer if referenced
        if(instance.renderPassSelfReferences.at(this).LODsMaterialDataOffset != UINT64_MAX && instancesDataBuffer)
        {
            instancesDataBuffer->removeFromRange(instance.renderPassSelfReferences.at(this).LODsMaterialDataOffset, instance.getRenderPassInstanceData(this).size());
        }

        instance.renderPassSelfReferences.erase(this);
    }
}