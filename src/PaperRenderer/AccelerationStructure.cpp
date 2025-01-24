#include "AccelerationStructure.h"
#include "PaperRenderer.h"
#include "Material.h"
#include "RayTrace.h"

#include <algorithm>

namespace PaperRenderer
{
    //----------TLAS INSTANCE BUILD PIPELINE DEFINITIONS----------//

    TLASInstanceBuildPipeline::TLASInstanceBuildPipeline(RenderEngine& renderer, const std::vector<uint32_t>& shaderData)
        :uboSetLayout(renderer.getDescriptorAllocator().createDescriptorSetLayout({
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .pImmutableSamplers = NULL
            }
        })),
        ioSetLayout(renderer.getDescriptorAllocator().createDescriptorSetLayout({
            {
                .binding = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
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
        })),
        computeShader(renderer, {
            .shaderInfo = {
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .data = shaderData
            },
            .descriptorSets = {
                { 0, uboSetLayout },
                { 1, ioSetLayout }
            },
            .pcRanges = {}
        }),
        renderer(renderer)
    {
        //log constructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "TLASInstanceBuildPipeline constructor finished"
        });
    }

    TLASInstanceBuildPipeline::~TLASInstanceBuildPipeline()
    {
        //destroy descriptor layouts
        vkDestroyDescriptorSetLayout(renderer.getDevice().getDevice(), uboSetLayout, nullptr);
        vkDestroyDescriptorSetLayout(renderer.getDevice().getDevice(), ioSetLayout, nullptr);
        
        //log destructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "TLASInstanceBuildPipeline destructor initialized"
        });
    }

    void TLASInstanceBuildPipeline::submit(VkCommandBuffer cmdBuffer, const TLAS& tlas)
    {
        //update UBO
        UBOInputData uboInputData = {};
        uboInputData.objectCount = tlas.nextUpdateSize;

        BufferWrite write = {};
        write.readData = &uboInputData;
        write.size = sizeof(UBOInputData);
        write.offset = sizeof(UBOInputData) * renderer.getBufferIndex();

        tlas.preprocessUniformBuffer.writeToBuffer({ write });

        //set0 - binding 0: UBO input data
        VkDescriptorBufferInfo bufferWrite0Info = {};
        bufferWrite0Info.buffer = tlas.preprocessUniformBuffer.getBuffer();
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
        bufferWrite2Info.buffer = tlas.instancesBuffer->getBuffer();
        bufferWrite2Info.offset = 0;
        bufferWrite2Info.range = tlas.nextUpdateSize * sizeof(ModelInstance::AccelerationStructureInstance);

        BuffersDescriptorWrites bufferWrite2 = {};
        bufferWrite2.binding = 2;
        bufferWrite2.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bufferWrite2.infos = { bufferWrite2Info };

        //set0 - binding 3: output objects
        VkDescriptorBufferInfo bufferWrite3Info = {};
        bufferWrite3Info.buffer = tlas.instancesBuffer->getBuffer();
        bufferWrite3Info.offset = tlas.tlInstancesOffset;
        bufferWrite3Info.range = tlas.nextUpdateSize * sizeof(VkAccelerationStructureInstanceKHR);

        BuffersDescriptorWrites bufferWrite3 = {};
        bufferWrite3.binding = 3;
        bufferWrite3.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bufferWrite3.infos = { bufferWrite3Info };

        //dispatch
        const DescriptorWrites descriptorWritesInfo = {
            .bufferWrites = { bufferWrite0, bufferWrite1, bufferWrite2, bufferWrite3 }
        };
        computeShader.dispatch(cmdBuffer, { { 0, descriptorWritesInfo } }, glm::uvec3((tlas.nextUpdateSize / 128) + 1, 1, 1));
    }

    //----------AS BASE CLASS DEFINITIONS----------//

    AS::AS(RenderEngine &renderer)
        :renderer(renderer)
    {
    }

    AS::~AS()
    {
        asBuffer.reset();

        if(accelerationStructure)
        {
            vkDestroyAccelerationStructureKHR(renderer.getDevice().getDevice(), accelerationStructure, nullptr);
        }
        for(const std::deque<VkAccelerationStructureKHR>& queue : asDestructionQueue)
        {
            for(VkAccelerationStructureKHR structure : queue)
            {
                vkDestroyAccelerationStructureKHR(renderer.getDevice().getDevice(), structure, nullptr);
            }
        }
    }

    AS::AsBuildData AS::getAsData(const VkAccelerationStructureTypeKHR type, const VkBuildAccelerationStructureFlagsKHR flags, const VkBuildAccelerationStructureModeKHR mode)
    {
        //delete AS in destruction queue
        for(VkAccelerationStructureKHR structure : asDestructionQueue[renderer.getBufferIndex()])
        {
            vkDestroyAccelerationStructureKHR(renderer.getDevice().getDevice(), structure, nullptr);
        }
        asDestructionQueue[renderer.getBufferIndex()].clear();

        //queue destruciton of old structure
        if(accelerationStructure && mode != VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR)
        {
            asDestructionQueue[renderer.getBufferIndex()].push_front(accelerationStructure);
            accelerationStructure = VK_NULL_HANDLE;
        }

        //get compaction flag
        const bool compact = flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR ? true : false;

        //get geometry data
        std::unique_ptr<AsGeometryBuildData> geometryBuildData = getGeometryData();

        //set build geometry info
        VkAccelerationStructureBuildGeometryInfoKHR buildGeoInfo = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .pNext = NULL,
            .type = type,
            .flags = flags,
            .mode = mode,
            .srcAccelerationStructure = accelerationStructure,
            .dstAccelerationStructure = VK_NULL_HANDLE,
            .geometryCount = (uint32_t)geometryBuildData->geometries.size(),
            .pGeometries = geometryBuildData->geometries.data(),
            .ppGeometries = NULL
        };

        //get build sizes
        VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
            .pNext = NULL
        };

        vkGetAccelerationStructureBuildSizesKHR(
            renderer.getDevice().getDevice(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildGeoInfo,
            geometryBuildData->primitiveCounts.data(),
            &buildSizeInfo);
        
        //update buffer if needed
        if(!asBuffer || asBuffer->getSize() < buildSizeInfo.accelerationStructureSize)
        {
            const BufferInfo bufferInfo = {
                .size = buildSizeInfo.accelerationStructureSize,
                .usageFlags = VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR,
                .allocationFlags = 0
            };
            asBuffer = std::make_unique<Buffer>(renderer, bufferInfo);
        }

        //create new acceleration structure
        const VkAccelerationStructureCreateInfoKHR accelerationStructureInfo = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .pNext = NULL,
            .createFlags = 0,
            .buffer = asBuffer->getBuffer(),
            .offset = 0,
            .size = buildSizeInfo.accelerationStructureSize,
            .type = buildGeoInfo.type
        };
        vkCreateAccelerationStructureKHR(renderer.getDevice().getDevice(), &accelerationStructureInfo, nullptr, &accelerationStructure);

        //update dstAccelerationStructure variable
        buildGeoInfo.dstAccelerationStructure = accelerationStructure;

        //return
        return { std::move(geometryBuildData), buildGeoInfo, buildSizeInfo, compact };
    }

    void AS::buildStructure(VkCommandBuffer cmdBuffer, AsBuildData& data, const CompactionQuery compactionQuery, const VkDeviceAddress scratchAddress)
    {
        //set scratch address
        data.buildGeoInfo.scratchData.deviceAddress = scratchAddress;

        //convert format of build ranges
        std::vector<VkAccelerationStructureBuildRangeInfoKHR const*> buildRangesPtrArray;
        for(const VkAccelerationStructureBuildRangeInfoKHR& buildRange : data.geometryBuildData->buildRangeInfos)
        {
            buildRangesPtrArray.emplace_back(&buildRange);
        }

        //build command
        vkCmdBuildAccelerationStructuresKHR(cmdBuffer, 1, &data.buildGeoInfo, buildRangesPtrArray.data());

        //write compaction data if enabled
        if(data.compact && compactionQuery.pool)
        {
            //memory barrier
            const VkBufferMemoryBarrier2 compactionMemBarrier = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .pNext = NULL,
                .srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR,
                .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = asBuffer->getBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE
            };

            const VkDependencyInfo compactionDependencyInfo = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = NULL,
                .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                .bufferMemoryBarrierCount = 1,
                .pBufferMemoryBarriers = &compactionMemBarrier
            };

            vkCmdPipelineBarrier2(cmdBuffer, &compactionDependencyInfo);     

            //write compaction properties to query pool
            vkCmdWriteAccelerationStructuresPropertiesKHR(
                cmdBuffer,
                1,
                &accelerationStructure,
                VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                compactionQuery.pool,
                compactionQuery.compactionIndex
            );
        }
    }

    std::unique_ptr<Buffer> AS::compactStructure(VkCommandBuffer cmdBuffer, const VkAccelerationStructureTypeKHR type, const VkDeviceSize newSize)
    {
        //create new buffer
        const BufferInfo bufferInfo = {
            .size = newSize,
            .usageFlags = VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR,
            .allocationFlags = 0
        };
        std::unique_ptr<Buffer> newBuffer = std::make_unique<Buffer>(renderer, bufferInfo);

        //store old accelerationStructure handle
        const VkAccelerationStructureKHR oldStructure = accelerationStructure;
        
        //overwrite accelerationStructure handle with new structure
        const VkAccelerationStructureCreateInfoKHR accelStructureInfo = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .pNext = NULL,
            .createFlags = 0,
            .buffer = newBuffer->getBuffer(),
            .offset = 0,
            .size = newSize,
            .type = type
        };
        vkCreateAccelerationStructureKHR(renderer.getDevice().getDevice(), &accelStructureInfo, nullptr, &accelerationStructure);

        //copy
        const VkCopyAccelerationStructureInfoKHR copyInfo = {
            .sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR,
            .pNext = NULL,
            .src = oldStructure,
            .dst = accelerationStructure,
            .mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR
        };
        vkCmdCopyAccelerationStructureKHR(cmdBuffer, &copyInfo);

        //queue destruction of old
        asDestructionQueue[renderer.getBufferIndex()].push_front(oldStructure);

        //set new buffer
        std::unique_ptr<Buffer> oldBuffer = std::move(asBuffer);
        asBuffer = std::move(newBuffer);

        return oldBuffer;
    }

    void AS::assignResourceOwner(const Queue &queue)
    {
        asBuffer->addOwner(queue);
    }

    //----------BLAS DEFINITIONS----------//

    BLAS::BLAS(RenderEngine &renderer, const Model &model, Buffer const *vbo)
        :AS(renderer),
        parentModel(model),
        vboPtr(vbo)
    {
    }

    BLAS::~BLAS()
    {
    }

    std::unique_ptr<AS::AsGeometryBuildData> BLAS::getGeometryData() const
    {
        std::unique_ptr<AsGeometryBuildData> returnData = std::make_unique<AsGeometryBuildData>();

        //get per material group geometry data
        for(const MaterialMesh& materialMesh : parentModel.getLODs()[0].materialMeshes) //use LOD 0 for BLAS
        {
            //mesh data
            const uint32_t vertexCount = materialMesh.mesh.vertexCount;
            const uint32_t indexCount = materialMesh.mesh.indexCount;
            const uint32_t vertexOffset = materialMesh.mesh.vboOffset;
            const uint32_t indexOffset = materialMesh.mesh.iboOffset;

            //geometry
            const VkAccelerationStructureGeometryKHR structureGeometry = {
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
                .pNext = NULL,
                .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
                .geometry = { .triangles = {
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
                    .pNext = NULL,
                    .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
                    .vertexData = VkDeviceOrHostAddressConstKHR{.deviceAddress = vboPtr->getBufferDeviceAddress()},
                    .vertexStride = parentModel.getVertexDescription().stride,
                    .maxVertex = vertexCount,
                    .indexType = VK_INDEX_TYPE_UINT32,
                    .indexData = VkDeviceOrHostAddressConstKHR{.deviceAddress = parentModel.getIBOAddress()}
                } },
                .flags = materialMesh.invokeAnyHit ? VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR : VK_GEOMETRY_OPAQUE_BIT_KHR
            };
            
            const VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo = {
                .primitiveCount = indexCount / 3,
                .primitiveOffset = indexOffset * (uint32_t)sizeof(uint32_t),
                .firstVertex = vertexOffset,
                .transformOffset = 0
            };

            returnData->geometries.emplace_back(structureGeometry);
            returnData->buildRangeInfos.emplace_back(buildRangeInfo);
            returnData->primitiveCounts.emplace_back(buildRangeInfo.primitiveCount);
        }

        return returnData;
    }

    void BLAS::buildStructure(VkCommandBuffer cmdBuffer, AsBuildData& data, const CompactionQuery compactionQuery, const VkDeviceAddress scratchAddress)
    {
        //call super
        AS::buildStructure(cmdBuffer, data, compactionQuery, scratchAddress);
    }

    //----------TLAS DEFINITIONS----------//
    
    TLAS::TLAS(RenderEngine& renderer)
        :AS(renderer),
        preprocessUniformBuffer(renderer, {
            .size = sizeof(TLASInstanceBuildPipeline::UBOInputData) * 2,
            .usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR,
            .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
        }),
        descriptorGroup(renderer, {
            { 0, renderer.getTLASPreprocessPipeline().getUboDescriptorLayout() },
            { 1, renderer.getRasterPreprocessPipeline().getIODescriptorLayout() }
        })
    {
    }

    TLAS::~TLAS()
    {
        scratchBuffer.reset();
        instancesBuffer.reset();
    }

    std::unique_ptr<AS::AsGeometryBuildData> TLAS::getGeometryData() const
    {
        std::unique_ptr<AsGeometryBuildData> returnData = std::make_unique<AsGeometryBuildData>();

        //geometries
        const VkAccelerationStructureGeometryKHR structureGeometry = {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .pNext = NULL,
            .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
            .geometry = { .instances = {
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
                .pNext = NULL,
                .arrayOfPointers = VK_FALSE,
                .data = VkDeviceOrHostAddressConstKHR{ .deviceAddress = instancesBuffer->getBufferDeviceAddress() + tlInstancesOffset }
            }},
            .flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR
        };

        const VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo = {
            .primitiveCount = nextUpdateSize,
            .primitiveOffset = 0,
            .firstVertex = 0,
            .transformOffset = 0
        };

        returnData->geometries.emplace_back(structureGeometry);
        returnData->buildRangeInfos.emplace_back(buildRangeInfo);
        returnData->primitiveCounts.push_back(nextUpdateSize);

        return returnData;
    }

    void TLAS::verifyInstancesBuffer(const uint32_t instanceCount)
    {
        //instances
        const VkDeviceSize newInstancesSize = Device::getAlignment(
            std::max((VkDeviceSize)(instanceCount * sizeof(ModelInstance::AccelerationStructureInstance) * instancesOverhead),
            (VkDeviceSize)(sizeof(ModelInstance::AccelerationStructureInstance) * 64)),
            renderer.getDevice().getGPUProperties().properties.limits.minStorageBufferOffsetAlignment
        );

        //instances description
        const VkDeviceSize newInstanceDescriptionsSize = Device::getAlignment(
            std::max((VkDeviceSize)(instanceCount * sizeof(InstanceDescription) * instancesOverhead),
            (VkDeviceSize)(sizeof(InstanceDescription) * 64)),
            renderer.getDevice().getGPUProperties().properties.limits.minStorageBufferOffsetAlignment
        );

        //tl instances
        const VkDeviceSize newTLInstancesSize = Device::getAlignment(
            std::max((VkDeviceSize)(instanceCount * sizeof(VkAccelerationStructureInstanceKHR) * instancesOverhead),
            (VkDeviceSize)(sizeof(VkAccelerationStructureInstanceKHR) * 64)),
            renderer.getDevice().getGPUProperties().properties.limits.minStorageBufferOffsetAlignment
        );

        const VkDeviceSize totalBufferSize = newInstancesSize + newInstanceDescriptionsSize + newTLInstancesSize;

        //rebuild buffer if needed
        if(!instancesBuffer || instancesBuffer->getSize() < totalBufferSize)
        {
            //create timer
            Timer timer(renderer, "TLAS Rebuild Instances Buffer", IRREGULAR);

            //offsets
            instanceDescriptionsOffset = newInstancesSize;
            tlInstancesOffset = newInstancesSize + newInstanceDescriptionsSize;

            //buffer
            const BufferInfo instancesBufferInfo = {
                .size = (VkDeviceSize)((double)totalBufferSize * instancesOverhead),
                .usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                    VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                .allocationFlags = 0
            };
            std::unique_ptr<Buffer> newInstancesBuffer = std::make_unique<Buffer>(renderer, instancesBufferInfo);

            //copy old data into new if old existed
            if(instancesBuffer)
            {
                const VkBufferCopy instancesCopyRegion = {
                    .srcOffset = 0,
                    .dstOffset = 0,
                    .size = instancesBuffer->getSize()
                };

                SynchronizationInfo syncInfo = {};
                syncInfo.queueType = TRANSFER;

                //start command buffer
                VkCommandBuffer cmdBuffer = renderer.getDevice().getCommands().getCommandBuffer(syncInfo.queueType);

                VkCommandBufferBeginInfo beginInfo = {};
                beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                beginInfo.pNext = NULL;
                beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

                vkBeginCommandBuffer(cmdBuffer, &beginInfo);
                vkCmdCopyBuffer(cmdBuffer, instancesBuffer->getBuffer(), newInstancesBuffer->getBuffer(), 1, &instancesCopyRegion);
                vkEndCommandBuffer(cmdBuffer);

                renderer.getDevice().getCommands().unlockCommandBuffer(cmdBuffer);

                //submit
                vkQueueWaitIdle(renderer.getDevice().getCommands().submitToQueue(syncInfo, { cmdBuffer }).queue);
            }
            
            //replace old buffers
            instancesBuffer = std::move(newInstancesBuffer);
        }
    }

    void TLAS::buildStructure(VkCommandBuffer cmdBuffer, AsBuildData& data, const CompactionQuery compactionQuery, const VkDeviceAddress scratchAddress)
    {
        //only rebuild/update a TLAS if any instances were updated
        if(nextUpdateSize)
        {
            renderer.tlasInstanceBuildPipeline.submit(cmdBuffer, *this);

            //TLAS instance data memory barrier
            const VkBufferMemoryBarrier2 tlasInstanceMemBarrier = {
                .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                .pNext = NULL,
                .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = instancesBuffer->getBuffer(),
                .offset = tlInstancesOffset,
                .size = instancesBuffer->getSize() - tlInstancesOffset
            };

            const VkDependencyInfo tlasInstanceDependencyInfo = {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = NULL,
                .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                .bufferMemoryBarrierCount = 1,
                .pBufferMemoryBarriers = &tlasInstanceMemBarrier
            };

            vkCmdPipelineBarrier2(cmdBuffer, &tlasInstanceDependencyInfo);
        }

        //call super function
        AS::buildStructure(cmdBuffer, data, compactionQuery, scratchAddress);
    }

    void TLAS::assignResourceOwner(const Queue &queue)
    {
        scratchBuffer->addOwner(queue);
        instancesBuffer->addOwner(queue);

        AS::assignResourceOwner(queue);
    }

    const Queue& TLAS::updateTLAS(const class RayTraceRender& rtRender, const VkBuildAccelerationStructureModeKHR mode, const VkBuildAccelerationStructureFlagsKHR flags, SynchronizationInfo syncInfo)
    {
        //----------QUEUE INSTANCE TRANSFERS----------//

        //create timer
        Timer timer(renderer, "TLAS Build/Update", REGULAR);

        //set next update size to 0
        nextUpdateSize = 0;

        //verify buffer sizes before data transfer
        verifyInstancesBuffer(rtRender.getTLASInstanceData().size());

        //queue instance data
        std::vector<char> newInstancesData(tlInstancesOffset); //allocate memory for everything but VkAccelerationStructureInstanceKHRs, which is at the end of the buffer
        for(const AccelerationStructureInstanceData& instance : rtRender.getTLASInstanceData())
        {
            //get BLAS pointer
            BLAS const* blasPtr = instance.instancePtr->getUniqueGeometryData().blas ? instance.instancePtr->getUniqueGeometryData().blas.get() : (BLAS*)instance.instancePtr->getParentModel().getBlasPtr();

            //skip if instance is NULL or has invalid BLAS
            if(instance.instancePtr && blasPtr)
            {
                //get sbt offset
                uint32_t sbtOffset = 
                    rtRender.getPipeline().getShaderBindingTableData().shaderBindingTableOffsets.
                    materialShaderGroupOffsets.at(instance.instancePtr->rtRenderSelfReferences.at(&rtRender).material);

                //write instance data
                ModelInstance::AccelerationStructureInstance instanceShaderData = {
                    .blasReference = blasPtr->getAccelerationStructureAddress(),
                    .selfIndex = instance.instancePtr->rendererSelfIndex,
                    .modelInstanceIndex = instance.instancePtr->rendererSelfIndex,
                    .customIndex = instance.customIndex,
                    .mask = instance.mask,
                    .recordOffset = sbtOffset,
                    .flags = instance.flags
                };
                memcpy(newInstancesData.data() + sizeof(ModelInstance::AccelerationStructureInstance) * nextUpdateSize, &instanceShaderData, sizeof(ModelInstance::AccelerationStructureInstance));

                //write description data
                InstanceDescription descriptionShaderData = {
                    .modelDataOffset = (uint32_t)instance.instancePtr->getParentModel().getShaderDataLocation()
                };
                memcpy(newInstancesData.data() + instanceDescriptionsOffset + sizeof(InstanceDescription) * nextUpdateSize, &descriptionShaderData, sizeof(InstanceDescription));

                nextUpdateSize++;
            }
        }

        //queue data transfers
        renderer.getStagingBuffer().queueDataTransfers(*instancesBuffer, 0, newInstancesData);

        //start command buffer
        VkCommandBuffer cmdBuffer = renderer.getDevice().getCommands().getCommandBuffer(COMPUTE);

        const VkCommandBufferBeginInfo cmdBufferInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = NULL
        };
        vkBeginCommandBuffer(cmdBuffer, &cmdBufferInfo);

        //record transfers
        renderer.getStagingBuffer().submitQueuedTransfers(cmdBuffer);

         //memory barriers
        const VkBufferMemoryBarrier2 transferMemBarrier = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = NULL,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT,
            .buffer = instancesBuffer->getBuffer(),
            .offset = 0,
            .size = VK_WHOLE_SIZE
        };

        const VkDependencyInfo transferDependencyInfo = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = NULL,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
            .bufferMemoryBarrierCount = 1,
            .pBufferMemoryBarriers = &transferMemBarrier
        };

        vkCmdPipelineBarrier2(cmdBuffer, &transferDependencyInfo);

        //----------TLAS BUILD----------//

        //set build data
        AsBuildData buildData = getAsData(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, flags, mode);

        //get scratch buffer size
        const VkDeviceSize requiredScratchSize = mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR ? buildData.buildSizeInfo.buildScratchSize : buildData.buildSizeInfo.updateScratchSize;
        
        //rebuild scratch buffer if needed
        if(!scratchBuffer || scratchBuffer->getSize() < requiredScratchSize)
        {
            const BufferInfo bufferInfo = {
                .size = requiredScratchSize,
                .usageFlags = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR,
                .allocationFlags = 0
            };
            scratchBuffer = std::make_unique<Buffer>(renderer, bufferInfo);
        }

        //build TLAS; note that compaction is ignored for TLAS
        buildStructure(cmdBuffer, buildData, {}, scratchBuffer->getBufferDeviceAddress());

        //end command buffer and submit
        vkEndCommandBuffer(cmdBuffer);

        renderer.getDevice().getCommands().unlockCommandBuffer(cmdBuffer);
        
        //submit
        const Queue& queue = renderer.getDevice().getCommands().submitToQueue(syncInfo, { cmdBuffer });

        //assign ownership
        assignResourceOwner(queue);
        renderer.getStagingBuffer().addOwner(queue);

        //return
        return queue;
    }

    //----------AS BUILDER DEFINITIONS----------//

    AccelerationStructureBuilder::AccelerationStructureBuilder(RenderEngine& renderer)
        :renderer(renderer)
    {
        //create scratch buffer with set size
        const BufferInfo bufferInfo = {
            .size = scratchBufferSize,
            .usageFlags = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR,
            .allocationFlags = 0
        };
        scratchBuffer = std::make_unique<Buffer>(renderer, bufferInfo);

        //log constructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "AccelerationStructureBuilder constructor finished"
        });
    }

    AccelerationStructureBuilder::~AccelerationStructureBuilder()
    {
        //log destructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "AccelerationStructureBuilder destructor initialized"
        });
    }

    std::unordered_map<BLAS*, VkDeviceSize> AccelerationStructureBuilder::getCompactions() const
    {
        std::unordered_map<BLAS*, VkDeviceSize> returnData;
        returnData.reserve(blasQueue.size());

        //get all build data
        VkDeviceSize compactionIndex = 0;
        for(const BLASBuildOp& op : blasQueue)
        {
            if(op.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)
            {
                returnData[&op.accelerationStructure] = compactionIndex;
                compactionIndex++;
            }
        }

        return returnData;
    }

    const Queue& AccelerationStructureBuilder::submitQueuedOps(const SynchronizationInfo& syncInfo)
    {
        Timer timer(renderer, "Submit Queued BLAS Ops", REGULAR);

        //----------AS BUILDS----------//

        //return queue
        Queue const* returnQueue = NULL;
        
        //get BLAS' that are to be compacted
        std::unordered_map<BLAS*, VkDeviceSize> compactions = getCompactions();

        //query pool for compaction if needed
        VkQueryPool queryPool = VK_NULL_HANDLE;
        if(compactions.size())
        {
            const VkQueryPoolCreateInfo queryPoolInfo = {
                .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                .pNext = NULL,
                .flags = 0,
                .queryType  = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                .queryCount = (uint32_t)compactions.size()
            };
            vkCreateQueryPool(renderer.getDevice().getDevice(), &queryPoolInfo, nullptr, &queryPool);
            vkResetQueryPool(renderer.getDevice().getDevice(), queryPool, 0, compactions.size());
        }

        //start command buffer
        VkCommandBuffer cmdBuffer = renderer.getDevice().getCommands().getCommandBuffer(COMPUTE);

        VkCommandBufferBeginInfo cmdBufferInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = NULL,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = NULL
        };
        vkBeginCommandBuffer(cmdBuffer, &cmdBufferInfo);

        //builds and updates (batch them to avoid stupidly large scratch buffer) TODO batch queue submits because microsoft's weird queue submit time limit
        VkDeviceSize scratchOffset = 0;
        for(BLASBuildOp& op : blasQueue)
        {
            //get build data
            AS::AsBuildData buildData = op.accelerationStructure.getAsData(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, op.flags, op.mode);
            const VkDeviceSize opRequiredScratchSize = op.mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR ? buildData.buildSizeInfo.buildScratchSize : buildData.buildSizeInfo.updateScratchSize;

            //verify scratch offset + required scratch size isn't too large; insert mem barrier and reset offset if it is too large
            if(scratchOffset + opRequiredScratchSize > scratchBuffer->getSize())
            {
                if(opRequiredScratchSize > scratchBuffer->getSize())
                {
                    //error handling for too big of a model
                    renderer.getLogger().recordLog({
                        .type = ERROR,
                        .text = "Tried to build a BLAS with a required scratch size of " + std::to_string(opRequiredScratchSize) + " which is larger than " + std::to_string(scratchBuffer->getSize())
                    });
                    continue;
                }

                //insert memory barrier
                const VkBufferMemoryBarrier2 memBarrier = {
                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                    .pNext = NULL,
                    .srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                    .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                    .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .buffer = scratchBuffer->getBuffer(),
                    .offset = 0,
                    .size = VK_WHOLE_SIZE
                };

                const VkDependencyInfo dependencyInfo = {
                    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                    .pNext = NULL,
                    .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
                    .bufferMemoryBarrierCount = 1,
                    .pBufferMemoryBarriers = &memBarrier
                };

                vkCmdPipelineBarrier2(cmdBuffer, &dependencyInfo);  

                //reset scratch offset
                scratchOffset = 0;
            }

            //compaction query if applies
            AS::CompactionQuery compactionQuery = {
                .pool = queryPool,
                .compactionIndex = compactions.count(&op.accelerationStructure) ? compactions[&op.accelerationStructure] : 0
            };

            //build
            op.accelerationStructure.buildStructure(cmdBuffer, buildData, compactionQuery, scratchBuffer->getBufferDeviceAddress() + scratchOffset);

            //set scratch offset
            scratchOffset += buildData.buildSizeInfo.buildScratchSize;
            scratchOffset = renderer.getDevice().getAlignment(scratchOffset, renderer.getDevice().getASproperties().minAccelerationStructureScratchOffsetAlignment);
        }

        //end command buffer and submit
        vkEndCommandBuffer(cmdBuffer);

        renderer.getDevice().getCommands().unlockCommandBuffer(cmdBuffer);

        SynchronizationInfo buildSyncInfo = {
            .queueType = COMPUTE,
            .binaryWaitPairs = syncInfo.binaryWaitPairs,
            .timelineWaitPairs = syncInfo.timelineWaitPairs,
            .fence = VK_NULL_HANDLE
        };

        if(!queryPool)
        {
            buildSyncInfo.binarySignalPairs = syncInfo.binarySignalPairs;
            buildSyncInfo.timelineSignalPairs = syncInfo.timelineSignalPairs;
            buildSyncInfo.fence = syncInfo.fence;
        }

        returnQueue = &renderer.getDevice().getCommands().submitToQueue(buildSyncInfo, { cmdBuffer });

        //----------AS COMPACTION----------//
        
        if(queryPool)
        {
            //get query results and perform compaction
            std::vector<VkDeviceSize> compactionResults(compactions.size());
            vkGetQueryPoolResults(
                renderer.getDevice().getDevice(),
                queryPool,
                0,
                compactions.size(),
                compactions.size() * sizeof(VkDeviceSize),
                compactionResults.data(),
                sizeof(VkDeviceSize),
                VK_QUERY_RESULT_WAIT_BIT
            );

            //start new command buffer
            VkCommandBuffer cmdBuffer = renderer.getDevice().getCommands().getCommandBuffer(COMPUTE);

            VkCommandBufferBeginInfo cmdBufferInfo = {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .pNext = NULL,
                .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                .pInheritanceInfo = NULL
            };
            vkBeginCommandBuffer(cmdBuffer, &cmdBufferInfo);

            //perform compactions
            std::vector<std::unique_ptr<Buffer>> oldBuffers;
            oldBuffers.reserve(compactions.size());
            for(auto& [blas, index] : compactions)
            {
                oldBuffers.push_back(blas->compactStructure(cmdBuffer, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, compactionResults[index]));
            }

            //end cmd buffer
            vkEndCommandBuffer(cmdBuffer);

            renderer.getDevice().getCommands().unlockCommandBuffer(cmdBuffer);

            //submit
            const SynchronizationInfo compactionSyncInfo = {
                .queueType = COMPUTE,
                .binarySignalPairs = syncInfo.binarySignalPairs,
                .timelineSignalPairs = syncInfo.timelineSignalPairs,
                .fence = syncInfo.fence
            };
            returnQueue = &renderer.getDevice().getCommands().submitToQueue(compactionSyncInfo, { cmdBuffer });

            //destroy query pool
            vkDestroyQueryPool(renderer.getDevice().getDevice(), queryPool, nullptr);

            //assign owners to old resources before they go out of scope (this essentially blocks this thread until compaction is completed)
            for(std::unique_ptr<Buffer>& buffer : oldBuffers)
            {
                buffer->addOwner(*returnQueue);
            }
        }

        //assign owners and clear queue
        for(BLASBuildOp& op : blasQueue)
        {
            op.accelerationStructure.assignResourceOwner(*returnQueue);
        }
        blasQueue.clear();

        //return
        return *returnQueue;
    }
}