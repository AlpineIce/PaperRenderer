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
        write.readData = &uboInputData;
        write.size = sizeof(UBOInputData);
        write.offset = sizeof(UBOInputData) * renderer.getBufferIndex();

        renderPass.preprocessUniformBuffer.writeToBuffer({ write });

        //set0 - binding 0: UBO input data
        VkDescriptorBufferInfo bufferWrite0Info = {};
        bufferWrite0Info.buffer = renderPass.preprocessUniformBuffer.getBuffer();
        bufferWrite0Info.offset = sizeof(UBOInputData) * renderer.getBufferIndex();
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
            .size = sizeof(RasterPreprocessPipeline::UBOInputData) * 2,
            .usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR,
            .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
        }),
        renderer(renderer),
        defaultMaterialInstance(defaultMaterialInstance)
    {
    }

    RenderPass::~RenderPass()
    {
        instancesBuffer.reset();
        instancesDataBuffer.reset();

        for(ModelInstance* instance : renderPassInstances)
        {
            removeInstance(*instance);
        }
    }

    void RenderPass::rebuildInstancesBuffer()
    {
        //Timer
        Timer timer(renderer, "Rebuild RenderPass Instances Buffer", IRREGULAR);

        //create new instance buffer
        VkDeviceSize newInstancesBufferSize = std::max((VkDeviceSize)(renderPassInstances.size() * sizeof(ModelInstance::RenderPassInstance) * instancesOverhead), (VkDeviceSize)(sizeof(ModelInstance::RenderPassInstance) * 64));

        const BufferInfo instancesBufferInfo = {
            .size = newInstancesBufferSize,
            .usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR,
            .allocationFlags = 0
        };
        std::unique_ptr<Buffer> newInstancesBuffer = std::make_unique<Buffer>(renderer, instancesBufferInfo);

        //copy old data into new if old existed
        if(instancesBuffer)
        {
            const VkBufferCopy instancesCopyRegion = {
                .srcOffset = 0,
                .dstOffset = 0,
                .size = std::min(renderPassInstances.size() * sizeof(ModelInstance::RenderPassInstance), instancesBuffer->getSize())
            };

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

    void RenderPass::rebuildSortedInstancesBuffer()
    {
        //Timer
        Timer timer(renderer, "Rebuild RenderPass Sorted Instances Buffer", IRREGULAR);

        //create new sorted instance buffer
        VkDeviceSize newSortedInstancesBufferSize = 
            std::max((VkDeviceSize)(renderPassSortedInstances.size() * sizeof(ShaderOutputObject) * instancesOverhead), (VkDeviceSize)(sizeof(ShaderOutputObject) * 64));

        BufferInfo sortedInstancesBufferInfo = {
            .size = newSortedInstancesBufferSize,
            .usageFlags = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR,
            .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
        };
        std::unique_ptr<Buffer> newSortedInstancesBuffer = std::make_unique<Buffer>(renderer, sortedInstancesBufferInfo);

        //replace old buffer
        sortedInstancesOutputBuffer = std::move(newSortedInstancesBuffer);
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

        const BufferInfo instancesMaterialDataBufferInfo = {
            .size = (VkDeviceSize)((float)newMaterialDataBufferSize * instancesOverhead),
            .usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR,
            .allocationFlags = 0
        };
        std::unique_ptr<FragmentableBuffer> newInstancesDataBuffer = std::make_unique<FragmentableBuffer>(renderer, instancesMaterialDataBufferInfo, 8);
        newInstancesDataBuffer->setCompactionCallback([this](std::vector<CompactionResult> results){ handleMaterialDataCompaction(results); });

        //copy old data into new if old existed
        if(instancesDataBuffer)
        {
            //pseudo write for material data
            newInstancesDataBuffer->newWrite(NULL, newMaterialDataWriteSize, NULL);

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

    void RenderPass::queueInstanceTransfers(VkCommandBuffer cmdBuffer)
    {
        //Timer
        Timer timer(renderer, "RenderPass Queue instance Transfers", REGULAR);

        //verify mesh group buffers
        for(auto& [material, materialInstanceNode] : renderTree) //material
        {
            for(auto& [materialInstance, meshGroups] : materialInstanceNode) //material instances
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
        if(!sortedInstancesOutputBuffer || sortedInstancesOutputBuffer->getSize() / sizeof(ShaderOutputObject) < renderPassSortedInstances.size())
        {
            rebuildSortedInstancesBuffer();
        }
        if(!instancesDataBuffer)
        {
            rebuildMaterialDataBuffer();
        }

        //sort instances; remove duplicates
        std::sort(toUpdateInstances.begin(), toUpdateInstances.end());
        auto sortedInstances = std::unique(toUpdateInstances.begin(), toUpdateInstances.end());
        toUpdateInstances.erase(sortedInstances, toUpdateInstances.end());

        //material data pseudo writes (this doesn't actually write anything its just to setup the fragmentable buffer)
        for(ModelInstance* instance : toUpdateInstances)
        {
            //skip if instance is NULL
            if(!instance) continue;

            //remove old if exists
            if(instance->renderPassSelfReferences.count(this) && instance->renderPassSelfReferences[this].LODsMaterialDataOffset != UINT64_MAX)
            {
                const std::vector<char>& oldMaterialData = instance->getRenderPassInstanceData(this);
                if(oldMaterialData.size()) instancesDataBuffer->removeFromRange(instance->renderPassSelfReferences[this].LODsMaterialDataOffset, oldMaterialData.size());
            }

            //set and get material data
            instance->setRenderPassInstanceData(this);
            const std::vector<char>& materialData = instance->getRenderPassInstanceData(this);

            //write new
            FragmentableBuffer::WriteResult writeResult = instancesDataBuffer->newWrite(NULL, materialData.size(), &(instance->renderPassSelfReferences[this].LODsMaterialDataOffset));
            if(writeResult == FragmentableBuffer::OUT_OF_MEMORY)
            {
                rebuildMaterialDataBuffer();
                instancesDataBuffer->newWrite(NULL, materialData.size(), &(instance->renderPassSelfReferences[this].LODsMaterialDataOffset));
            }
            else if(writeResult == FragmentableBuffer::COMPACTED)
            {
                //recursive redo (handling compaction resets the instances)
                queueInstanceTransfers(cmdBuffer);
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
            renderer.getStagingBuffer().queueDataTransfers(instancesDataBuffer->getBuffer(), instance->renderPassSelfReferences[this].LODsMaterialDataOffset, materialData);

            //write instance data
            const ModelInstance::RenderPassInstance instanceShaderData = {
                .modelInstanceIndex = instance->rendererSelfIndex,
                .LODsMaterialDataOffset = (uint32_t)instance->renderPassSelfReferences[this].LODsMaterialDataOffset,
                .isVisible = true
            };

            std::vector<char> instanceData(sizeof(ModelInstance::RenderPassInstance));
            memcpy(instanceData.data(), &instanceShaderData, instanceData.size());
            
            //queue data transfer
            renderer.getStagingBuffer().queueDataTransfers(*instancesBuffer, sizeof(ModelInstance::RenderPassInstance) * instance->renderPassSelfReferences[this].selfIndex, instanceData);
        }

        //clear deques
        toUpdateInstances.clear();

        //submit
        renderer.getStagingBuffer().submitQueuedTransfers(cmdBuffer);

        //memory barriers
        const std::array<VkBufferMemoryBarrier2, 2> transferMemBarriers = {
            VkBufferMemoryBarrier2({
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .pNext = NULL,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
                .buffer = instancesBuffer->getBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE
            }),
            VkBufferMemoryBarrier2({
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .pNext = NULL,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
                .buffer = instancesDataBuffer->getBuffer().getBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE
            })
        };

        const VkDependencyInfo transferDependencyInfo = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = NULL,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
            .bufferMemoryBarrierCount = transferMemBarriers.size(),
            .pBufferMemoryBarriers = transferMemBarriers.data()
        };

        vkCmdPipelineBarrier2(cmdBuffer, &transferDependencyInfo);
    }

    void RenderPass::handleMaterialDataCompaction(const std::vector<CompactionResult>& results)
    {
        //fix material data offsets
        for(const CompactionResult compactionResult : results)
        {
            for(ModelInstance* instance : renderPassInstances)
            {
                VkDeviceSize& materialDataOffset = instance->renderPassSelfReferences[this].LODsMaterialDataOffset;
                if(materialDataOffset != UINT64_MAX && materialDataOffset > compactionResult.location)
                {
                    //shift stored location
                    materialDataOffset -= compactionResult.shiftSize;
                }
            }
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
            for(const auto& [materialInstance, meshGroup] : materialInstanceNode) //material instances
            {
                //clear
                meshGroup.clearDrawCommand(cmdBuffer);
            }
        }
    }

    void RenderPass::assignResourceOwner(const Queue &queue)
    {
        //this
        instancesBuffer->addOwner(queue);
        sortedInstancesOutputBuffer->addOwner(queue);
        instancesDataBuffer->addOwner(queue);

        //common mesh groups
        for(auto& [material, materialInstanceNode] : renderTree) //material
        {
            for(auto& [materialInstance, meshGroup] : materialInstanceNode) //material instances
            {
                //clear
                meshGroup.addOwner(queue);
            }
        }
    }

    const Queue& RenderPass::render(const RenderPassInfo& renderPassInfo, SynchronizationInfo syncInfo)
    {
        //Timer
        Timer timer(renderer, "RenderPass Submission", REGULAR);
        
        VkCommandBuffer cmdBuffer = renderer.getDevice().getCommands().getCommandBuffer(GRAPHICS);

        const VkCommandBufferBeginInfo cmdBufferBeginInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = NULL
        };
        vkBeginCommandBuffer(cmdBuffer, &cmdBufferBeginInfo);

        //pre-render barriers
        if(renderPassInfo.preRenderBarriers)
        {
            vkCmdPipelineBarrier2(cmdBuffer, renderPassInfo.preRenderBarriers);
        }

        //instance transfers
        queueInstanceTransfers(cmdBuffer);

        //clear draw counts
        clearDrawCounts(cmdBuffer);
        
        //preprocess
        if(renderPassInstances.size())
        {
            //compute shader
            renderer.getRasterPreprocessPipeline().submit(cmdBuffer, *this, renderPassInfo.camera);

            //memory barrier
            const VkMemoryBarrier2 preprocessMemBarrier = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                .pNext = NULL,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT
            };

            const VkDependencyInfo preprocessDependencyInfo = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = NULL,
                .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                .memoryBarrierCount = 1,
                .pMemoryBarriers = &preprocessMemBarrier
            };

            vkCmdPipelineBarrier2(cmdBuffer, &preprocessDependencyInfo);
        }
        
        //----------RENDER PASS----------//

        //rendering
        VkRenderingInfo renderInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
            .pNext = NULL,
            .flags = 0,
            .renderArea = renderPassInfo.renderArea,
            .layerCount = 1,
            .viewMask = 0,
            .colorAttachmentCount = (uint32_t)renderPassInfo.colorAttachments.size(),
            .pColorAttachments = renderPassInfo.colorAttachments.data(),
            .pDepthAttachment = renderPassInfo.depthAttachment,
            .pStencilAttachment = renderPassInfo.stencilAttachment
        };
        vkCmdBeginRendering(cmdBuffer, &renderInfo);

        //scissors and viewports
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
            for(const auto& [materialInstance, meshGroups] : materialInstanceNode) //material instances
            {
                std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites> instanceDescriptorWrites;
                materialInstance->bind(cmdBuffer, instanceDescriptorWrites);
                meshGroups.draw(cmdBuffer, *material);
            }
        }

        //sorted instances
        if(renderPassSortedInstances.size())
        {
            //Timer
            Timer timer(renderer, "RenderPass Render Sorted Instances Recording", REGULAR);

            //sort sorted instances
            std::vector<SortedInstance*> sortedInstances;
            sortedInstances.reserve(renderPassSortedInstances.size());
            for(SortedInstance& instance : renderPassSortedInstances)
            {
                sortedInstances.push_back(&instance);
            }

            std::sort(sortedInstances.begin(), sortedInstances.end(), [&](const SortedInstance* instanceA, const SortedInstance* instanceB)
            {
                const float distA = glm::length(instanceA->instance->getTransformation().position - renderPassInfo.camera.getPosition());
                const float distB = glm::length(instanceB->instance->getTransformation().position - renderPassInfo.camera.getPosition());

                switch(renderPassInfo.sortMode)
                {
                case FRONT_FIRST:
                    return distA < distB;
                case BACK_FIRST:
                    return distA > distB;
                default: //aka DONT_CARE
                    return distA < distB;
                }
            });

            //calculate model matrices and transfer them to the sortedInstancesOutputBuffer
            std::vector<ShaderOutputObject> sortedInstancesMatricesData(sortedInstances.size());
            for(uint32_t i = 0; i < sortedInstances.size(); i++)
            {
                ModelTransformation transform = sortedInstances[i]->instance->getTransformation();

                glm::mat3 qMat;

                //rotation
                glm::quat q = transform.rotation;
                float qxx = q.x * q.x;
                float qyy = q.y * q.y;
                float qzz = q.z * q.z;
                float qxz = q.x * q.z;
                float qxy = q.x * q.y;
                float qyz = q.y * q.z;
                float qwx = q.w * q.x;
                float qwy = q.w * q.y;
                float qwz = q.w * q.z;

                qMat[0] = glm::vec3(1.0 - 2.0 * (qyy + qzz), 2.0 * (qxy + qwz), 2.0 * (qxz - qwy));
                qMat[1] = glm::vec3(2.0 * (qxy - qwz), 1.0 - 2.0 * (qxx + qzz), 2.0 * (qyz + qwx));
                qMat[2] = glm::vec3(2.0 * (qxz + qwy), 2.0 * (qyz - qwx), 1.0 - 2.0 * (qxx + qyy));

                //scale
                glm::mat3 scaleMat;
                scaleMat[0] = glm::vec3(transform.scale.x, 0.0, 0.0);
                scaleMat[1] = glm::vec3(0.0, transform.scale.y, 0.0);
                scaleMat[2] = glm::vec3(0.0, 0.0, transform.scale.z);

                //composition of rotation and scale
                glm::mat3 scaleRotMat = scaleMat * qMat;

                glm::mat3x4 result;
                result[0] = glm::vec4(scaleRotMat[0], transform.position.x);
                result[1] = glm::vec4(scaleRotMat[1], transform.position.y);
                result[2] = glm::vec4(scaleRotMat[2], transform.position.z);

                sortedInstancesMatricesData[i] = { result };
            }

            //transfer data
            const BufferWrite matricesWrite = {
                .offset = 0,
                .size = sortedInstancesMatricesData.size() * sizeof(ShaderOutputObject),
                .readData = sortedInstancesMatricesData.data()
            };
            sortedInstancesOutputBuffer->writeToBuffer({ matricesWrite });

            //LOD index function (tbh i should really change this whole function)
            auto getLODIndex = [&](ModelInstance* instance)
            {
                //get largest OBB extent to be used as size
                AABB bounds = instance->getParentModel().getAABB();
                float xLength = bounds.posX - bounds.negX;
                float yLength = bounds.posY - bounds.negY;
                float zLength = bounds.posZ - bounds.negZ;

                float worldSize = 0.0;
                worldSize = std::max(worldSize, xLength);
                worldSize = std::max(worldSize, yLength);
                worldSize = std::max(worldSize, zLength);

                float cameraDistance = glm::length(instance->getTransformation().position - renderPassInfo.camera.getPosition());
                
                uint32_t lodLevel = floor(1.0 / sqrt(worldSize * 10.0) * sqrt(cameraDistance));

                return lodLevel;
            };

            //draw sorted instances in order
            for(uint32_t i = 0; i < sortedInstances.size(); i++)
            {
                //get LOD level
                const uint32_t lodIndex = std::min(getLODIndex(sortedInstances[i]->instance), (uint32_t)sortedInstances[i]->instance->getParentModel().getLODs().size() - 1);
                for(auto& [matSlot, materialInstance] : sortedInstances[i]->materials[lodIndex])
                {
                    //get material
                    Material* material = ((Material*)(&materialInstance->getBaseMaterial()));

                    //bind material
                    std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites> materialDescriptorWrites;
                    material->bind(cmdBuffer, renderPassInfo.camera, materialDescriptorWrites);

                    //bind material instance
                    std::unordered_map<uint32_t, PaperRenderer::DescriptorWrites> instanceDescriptorWrites;
                    materialInstance->bind(cmdBuffer, instanceDescriptorWrites);

                    //assign object descriptor if used
                    if(material->usesDefaultDescriptors())
                    {
                        //get new descriptor set
                        VkDescriptorSet objDescriptorSet = 
                            renderer.getDescriptorAllocator().allocateDescriptorSet(material->getRasterPipeline().getDescriptorSetLayouts().at(material->getRasterPipeline().getDrawDescriptorIndex()));
                        
                        //write uniforms
                        VkDescriptorBufferInfo descriptorInfo = {
                            .buffer = sortedInstancesOutputBuffer->getBuffer(),
                            .offset = 0,
                            .range = sizeof(ShaderOutputObject) * sortedInstances.size()
                        };

                        BuffersDescriptorWrites write = {};
                        write.binding = 0;
                        write.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                        write.infos.push_back(descriptorInfo);

                        DescriptorWrites descriptorWritesInfo = {};
                        descriptorWritesInfo.bufferWrites = { write };
                        renderer.getDescriptorAllocator().writeUniforms(objDescriptorSet, descriptorWritesInfo);

                        //bind set
                        DescriptorBind bindingInfo = {};
                        bindingInfo.descriptorSetIndex = material->getRasterPipeline().getDrawDescriptorIndex();
                        bindingInfo.set = objDescriptorSet;
                        bindingInfo.layout = material->getRasterPipeline().getLayout();
                        bindingInfo.bindingPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
                        renderer.getDescriptorAllocator().bindSet(cmdBuffer, bindingInfo);
                    }

                    //get mesh data ptr
                    const LODMesh& meshData = sortedInstances[i]->instance->getParentModel().getLODs()[lodIndex].materialMeshes[matSlot].mesh;

                    //bind vbo and ibo and send draw calls (draw calls should be computed in the performCulling() function)
                    sortedInstances[i]->instance->getParentModel().bindBuffers(cmdBuffer);

                    //draw
                    vkCmdDrawIndexed(
                        cmdBuffer,
                        meshData.indexCount,
                        1,
                        meshData.iboOffset,
                        meshData.vboOffset,
                        i //first index at i because we want the same behavior as the normal drawing method
                    );
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

        //end cmd buffer
        vkEndCommandBuffer(cmdBuffer);

        renderer.getDevice().getCommands().unlockCommandBuffer(cmdBuffer);

        //submit
        const Queue& queue = renderer.getDevice().getCommands().submitToQueue(syncInfo, { cmdBuffer });

        //assign owner to resources in case destruction is required
        assignResourceOwner(queue);
        renderer.getStagingBuffer().addOwner(queue);

        return queue;
    }

    void RenderPass::addInstance(ModelInstance& instance, std::vector<std::unordered_map<uint32_t, MaterialInstance*>> materials, bool sorted)
    {
        if(sorted)
        {
            //add reference
            instance.renderPassSelfReferences[this].selfIndex = renderPassSortedInstances.size();
            instance.renderPassSelfReferences[this].sorted = true;
            renderPassSortedInstances.push_back({ &instance, materials });
        }
        else
        {
            //material data
            materials.resize(instance.getParentModel().getLODs().size());
            for(uint32_t lodIndex = 0; lodIndex < instance.getParentModel().getLODs().size(); lodIndex++)
            {
                for(uint32_t matIndex = 0; matIndex < instance.getParentModel().getLODs().at(lodIndex).materialMeshes.size(); matIndex++) //iterate materials in LOD
                {
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

                    //unique geometry or not
                    if(instance.getUniqueGeometryData().isUsed)
                    {
                        //sorry excuse for me being too lazy to implement unique geometry for now
                        renderer.getLogger().recordLog({
                            .type = WARNING,
                            .text = "Unique VBOs aren't yet supported in raster render passes. Parent model VBO will be used"
                        });
                    }

                    //get mesh using same material
                    const LODMesh& similarMesh = instance.getParentModel().getLODs().at(lodIndex).materialMeshes.at(matIndex).mesh;

                    //check if mesh group class is created
                    if(!renderTree[(Material*)&materialInstance->getBaseMaterial()].count(materialInstance))
                    {
                        renderTree[(Material*)&materialInstance->getBaseMaterial()].emplace(std::piecewise_construct, std::forward_as_tuple(materialInstance), std::forward_as_tuple(renderer, this));
                    }

                    //add references
                    renderTree[(Material*)&materialInstance->getBaseMaterial()].at(materialInstance).addInstanceMesh(instance, similarMesh);

                    instance.renderPassSelfReferences[this].meshGroupReferences[&instance.getParentModel().getLODs().at(lodIndex).materialMeshes.at(matIndex).mesh] = 
                        &renderTree.at((Material*)&materialInstance->getBaseMaterial()).at(materialInstance);
                }
            }

            //add reference
            instance.renderPassSelfReferences[this].selfIndex = renderPassInstances.size();
            instance.renderPassSelfReferences[this].sorted = false;
            renderPassInstances.push_back(&instance);

            //add instance to queue
            toUpdateInstances.push_front(&instance);
        }
    }

    void RenderPass::removeInstance(ModelInstance& instance)
    {
        //remove from normal if reference exists
        if(instance.renderPassSelfReferences.count(this))
        {
            //remove from mesh groups
            for(auto& [mesh, reference] : instance.renderPassSelfReferences[this].meshGroupReferences)
            {
                reference->removeInstanceMeshes(instance);
            }
            instance.renderPassSelfReferences[this].meshGroupReferences.clear();

            //shift instances
            const uint32_t selfReference = instance.renderPassSelfReferences[this].selfIndex;
            if(instance.renderPassSelfReferences[this].sorted)
            {
                if(renderPassSortedInstances.size() > 1)
                {
                    renderPassSortedInstances[selfReference] = renderPassSortedInstances.back();
                    renderPassSortedInstances[selfReference].instance->renderPassSelfReferences[this].selfIndex = selfReference;

                    renderPassSortedInstances.pop_back();
                }
                else
                {
                    renderPassSortedInstances.clear();
                }
            }
            else
            {
                if(renderPassInstances.size() > 1)
                {
                    renderPassInstances[selfReference] = renderPassInstances.back();
                    renderPassInstances[selfReference]->renderPassSelfReferences[this].selfIndex = selfReference;

                    //queue data transfer
                    toUpdateInstances.push_front(renderPassInstances.at(instance.renderPassSelfReferences[this].selfIndex));
                    
                    renderPassInstances.pop_back();
                }
                else
                {
                    renderPassInstances.clear();
                }
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
            if(instance.renderPassSelfReferences[this].LODsMaterialDataOffset != UINT64_MAX && instancesDataBuffer)
            {
                instancesDataBuffer->removeFromRange(instance.renderPassSelfReferences[this].LODsMaterialDataOffset, instance.getRenderPassInstanceData(this).size());
            }

            //erase this reference
            instance.renderPassSelfReferences.erase(this);
        }
    }
}