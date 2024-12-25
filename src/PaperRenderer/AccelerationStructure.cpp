#include "AccelerationStructure.h"
#include "PaperRenderer.h"
#include "Material.h"
#include "RayTrace.h"

#include <algorithm>

namespace PaperRenderer
{
    //----------TLAS INSTANCE BUILD PIPELINE DEFINITIONS----------//

    TLASInstanceBuildPipeline::TLASInstanceBuildPipeline(RenderEngine& renderer, const std::vector<uint32_t>& shaderData)
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
                    },
                    {
                        .binding = 3,
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
            .text = "TLASInstanceBuildPipeline constructor finished"
        });
    }

    TLASInstanceBuildPipeline::~TLASInstanceBuildPipeline()
    {
        //log destructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "TLASInstanceBuildPipeline destructor initialized"
        });
    }

    void TLASInstanceBuildPipeline::submit(VkCommandBuffer cmdBuffer, const TLAS& tlas)
    {
        UBOInputData uboInputData = {};
        uboInputData.objectCount = tlas.nextUpdateSize;

        BufferWrite write = {};
        write.data = &uboInputData;
        write.size = sizeof(UBOInputData);
        write.offset = 0;

        tlas.preprocessUniformBuffer.writeToBuffer({ write });

        //set0 - binding 0: UBO input data
        VkDescriptorBufferInfo bufferWrite0Info = {};
        bufferWrite0Info.buffer = tlas.preprocessUniformBuffer.getBuffer();
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

    AS::AS(RenderEngine& renderer)
        :renderer(renderer)
    {
    }

    AS::~AS()
    {
        if(accelerationStructure)
        {
            vkDestroyAccelerationStructureKHR(renderer.getDevice().getDevice(), accelerationStructure, nullptr);
        }
        asBuffer.reset();
    }

    //----------BLAS DEFINITIONS----------//

    BLAS::BLAS(RenderEngine& renderer, const Model& model, Buffer const* vbo)
        :AS(renderer),
        parentModel(model),
        vboPtr(vbo)

    {
    }

    BLAS::~BLAS()
    {
    }

    //----------TLAS DEFINITIONS----------//
    
    TLAS::TLAS(RenderEngine& renderer)
        :AS(renderer),
        preprocessUniformBuffer(renderer, {
            .size = sizeof(TLASInstanceBuildPipeline::UBOInputData),
            .usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT,
            .allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
        })
    {
    }

    TLAS::~TLAS()
    {
        instancesBuffer.reset();
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
            BufferInfo instancesBufferInfo = {};
            instancesBufferInfo.allocationFlags = 0;
            instancesBufferInfo.size = totalBufferSize;
            instancesBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
            std::unique_ptr<Buffer> newInstancesBuffer = std::make_unique<Buffer>(renderer, instancesBufferInfo);

            //copy old data into new if old existed
            if(instancesBuffer)
            {
                VkBufferCopy instancesCopyRegion = {};
                instancesCopyRegion.srcOffset = 0;
                instancesCopyRegion.dstOffset = 0;
                instancesCopyRegion.size = instancesBuffer->getSize();

                SynchronizationInfo syncInfo = {};
                syncInfo.queueType = TRANSFER;
                syncInfo.fence = renderer.getDevice().getCommands().getUnsignaledFence();

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
                renderer.getDevice().getCommands().submitToQueue(syncInfo, { cmdBuffer });

                vkWaitForFences(renderer.getDevice().getDevice(), 1, &syncInfo.fence, VK_TRUE, UINT64_MAX);
                vkDestroyFence(renderer.getDevice().getDevice(), syncInfo.fence, nullptr);
            }
            
            //replace old buffers
            instancesBuffer = std::move(newInstancesBuffer);
        }
    }

    void TLAS::queueInstanceTransfers(const RayTraceRender& rtRender)
    {
        //create timer
        Timer timer(renderer, "TLAS Queue Instance Transfers", REGULAR);

        //set next update size to 0
        nextUpdateSize = 0;

        //verify buffer sizes before data transfer
        verifyInstancesBuffer(rtRender.getTLASInstanceData().size());

        //queue instance data
        std::vector<char> newInstancesData(tlInstancesOffset); //allocate memory for everything but VkAccelerationStructureInstanceKHRs, which is at the end of the buffer
        for(const AccelerationStructureInstanceData& instance : rtRender.getTLASInstanceData())
        {
            //get BLAS pointer
            BLAS* blasPtr = instance.instancePtr->getUniqueBLAS() ? instance.instancePtr->getUniqueBLAS() : (BLAS*)instance.instancePtr->getParentModel().getBlasPtr();

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
                    .customIndex = instance.customIndex,
                    .modelInstanceIndex = instance.instancePtr->rendererSelfIndex,
                    .mask = instance.mask << 24,
                    .recordOffset = sbtOffset,
                    .flags = instance.flags << 24
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

        //record transfers
        SynchronizationInfo syncInfo = {};
        syncInfo.queueType = TRANSFER;
        renderer.getStagingBuffer().submitQueuedTransfers(syncInfo);
    }

    //----------AS BUILDER DEFINITIONS----------//

    AccelerationStructureBuilder::AccelerationStructureBuilder(RenderEngine& renderer)
        :renderer(renderer)
    {
        asBuildSemaphore = renderer.getDevice().getCommands().getTimelineSemaphore(finalSemaphoreValue);

        //log constructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "AccelerationStructureBuilder constructor finished"
        });
    }

    AccelerationStructureBuilder::~AccelerationStructureBuilder()
    {
        vkDestroySemaphore(renderer.getDevice().getDevice(), asBuildSemaphore, nullptr);

        scratchBuffer.reset();

        //log destructor
        renderer.getLogger().recordLog({
            .type = INFO,
            .text = "AccelerationStructureBuilder destructor initialized"
        });
    }

    AccelerationStructureBuilder::AsBuildData AccelerationStructureBuilder::getAsData(const AccelerationStructureOp& op) const
    {
        AsBuildData returnData = {
            .as = op.accelerationStructure,
            .type = op.type,
            .compact = op.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR ? true : false
        };
        std::vector<uint32_t> primitiveCounts;

        //geometry type
        if(op.type == VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR)
        {
            BLAS& blas = (BLAS&)(op.accelerationStructure);

            //get per material group geometry data
            for(const MaterialMesh& materialMesh : blas.getParentModel().getLODs().at(0).materialMeshes) //use LOD 0 for BLAS
            {
                //mesh data
                VkDeviceSize vertexCount = materialMesh.mesh.vertexCount;
                VkDeviceSize indexCount = materialMesh.mesh.indexCount;
                VkDeviceAddress vertexOffset = materialMesh.mesh.vboOffset;
                VkDeviceAddress indexOffset = materialMesh.mesh.iboOffset;

                //buffer information
                VkAccelerationStructureGeometryTrianglesDataKHR trianglesGeometry = {};
                trianglesGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                trianglesGeometry.pNext = NULL;
                trianglesGeometry.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
                trianglesGeometry.vertexData = VkDeviceOrHostAddressConstKHR{.deviceAddress = blas.getVBOAddress()};
                trianglesGeometry.maxVertex = vertexCount;
                trianglesGeometry.vertexStride = blas.getParentModel().getVertexDescription().stride;
                trianglesGeometry.indexType = VK_INDEX_TYPE_UINT32;
                trianglesGeometry.indexData = VkDeviceOrHostAddressConstKHR{.deviceAddress = blas.getParentModel().getIBOAddress()};

                //geometries
                VkAccelerationStructureGeometryKHR structureGeometry = {};
                structureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
                structureGeometry.pNext = NULL;
                structureGeometry.flags = materialMesh.invokeAnyHit ? VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR : VK_GEOMETRY_OPAQUE_BIT_KHR; 
                structureGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                structureGeometry.geometry.triangles = trianglesGeometry;
                
                VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo;
                buildRangeInfo.primitiveCount = indexCount / 3;
                buildRangeInfo.primitiveOffset = indexOffset * sizeof(uint32_t);
                buildRangeInfo.firstVertex = vertexOffset;
                buildRangeInfo.transformOffset = 0;

                returnData.geometries.emplace_back(structureGeometry);
                returnData.buildRangeInfos.emplace_back(buildRangeInfo);
                primitiveCounts.emplace_back(buildRangeInfo.primitiveCount);
            }
        }
        else if(op.type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR)
        {
            TLAS& tlas = (TLAS&)(op.accelerationStructure);

            //geometries
            VkAccelerationStructureGeometryInstancesDataKHR geoInstances = {};
            geoInstances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
            geoInstances.pNext = NULL;
            geoInstances.arrayOfPointers = VK_FALSE;
            geoInstances.data = VkDeviceOrHostAddressConstKHR{ .deviceAddress = tlas.instancesBuffer->getBufferDeviceAddress() + tlas.tlInstancesOffset };

            VkAccelerationStructureGeometryKHR structureGeometry = {};
            structureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            structureGeometry.pNext = NULL;
            structureGeometry.flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
            structureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
            structureGeometry.geometry.instances = geoInstances;

            VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo;
            buildRangeInfo.primitiveCount = tlas.nextUpdateSize;
            buildRangeInfo.primitiveOffset = 0;
            buildRangeInfo.firstVertex = 0;
            buildRangeInfo.transformOffset = 0;

            returnData.geometries.emplace_back(structureGeometry);
            returnData.buildRangeInfos.emplace_back(buildRangeInfo);
            primitiveCounts.push_back(tlas.nextUpdateSize);
        }
        else
        {
            throw std::runtime_error("Ambiguous acceleration structure type tried to be built. Please specify build type");
        }

        //build information
        returnData.buildGeoInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        returnData.buildGeoInfo.pNext = NULL;
        returnData.buildGeoInfo.flags = op.flags;
        returnData.buildGeoInfo.type = op.type;
        returnData.buildGeoInfo.mode = op.mode;
        returnData.buildGeoInfo.srcAccelerationStructure = returnData.compact ? op.accelerationStructure.accelerationStructure : VK_NULL_HANDLE;
        returnData.buildGeoInfo.dstAccelerationStructure = op.accelerationStructure.accelerationStructure; //probably overwritten by code after
        returnData.buildGeoInfo.geometryCount = returnData.geometries.size();
        returnData.buildGeoInfo.pGeometries = returnData.geometries.data();
        returnData.buildGeoInfo.ppGeometries = NULL;

        returnData.buildSizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        returnData.buildSizeInfo.pNext = NULL;

        vkGetAccelerationStructureBuildSizesKHR(
            renderer.getDevice().getDevice(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &returnData.buildGeoInfo,
            primitiveCounts.data(),
            &returnData.buildSizeInfo);

        //destroy buffer if compaction is requested since final size will be unknown
        if(returnData.compact)
        {
            op.accelerationStructure.asBuffer.reset();
        }
        //otherwise update buffer if needed
        else if(!op.accelerationStructure.asBuffer || op.accelerationStructure.asBuffer->getSize() < returnData.buildSizeInfo.accelerationStructureSize)
        {
            op.accelerationStructure.asBuffer.reset();

            BufferInfo bufferInfo = {};
            bufferInfo.allocationFlags = 0;
            bufferInfo.size = returnData.buildSizeInfo.accelerationStructureSize;
            bufferInfo.usageFlags = VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
            op.accelerationStructure.asBuffer = std::make_unique<Buffer>(renderer, bufferInfo);
        }

        //set blas flags
        op.accelerationStructure.enabledFlags = op.flags;

        return returnData;
    }

    VkDeviceSize AccelerationStructureBuilder::getScratchSize(std::vector<AsBuildData>& blasDatas, std::vector<AsBuildData>& tlasDatas) const
    {
        VkDeviceSize blasSize = 0;
        VkDeviceSize tlasSize = 0;
        const uint32_t alignment = renderer.getDevice().getASproperties().minAccelerationStructureScratchOffsetAlignment;

        for(AsBuildData& data : blasDatas)
        {
            data.scratchDataOffset = blasSize;
            blasSize += Device::getAlignment(data.buildSizeInfo.buildScratchSize, alignment);
        }
        for(AsBuildData& data : tlasDatas)
        {
            data.scratchDataOffset = tlasSize;
            tlasSize += Device::getAlignment(data.buildSizeInfo.buildScratchSize, alignment);
        }
        
        //blas and tlas builds happen seperately, so the max of either will be used to save scratch memory
        return std::max((VkDeviceSize)std::max(blasSize, tlasSize), (VkDeviceSize)256); //minimum of 256 bytes
    }

    void AccelerationStructureBuilder::setBuildData()
    {
        //reset data
        buildData = {};

        //get all build data
        for(const AccelerationStructureOp& op : asQueue)
        {
            //switch statement for compaction counter
            switch(op.type)
            {
            case VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR:
                //get build data
                buildData.blasDatas.emplace_back(getAsData(op));
                if(buildData.blasDatas.rbegin()->compact) buildData.numBlasCompactions++;
                break;

            case VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR:
                //get build data
                buildData.tlasDatas.emplace_back(getAsData(op));
                if(buildData.tlasDatas.rbegin()->compact) buildData.numTlasCompactions++;
                break;

            default:
                renderer.getLogger().recordLog({
                    .type = WARNING,
                    .text = "Ambiguous acceleration structure type specified. Please specify build type of TOP or BOTTOM"
                });
                break;
            }

            asQueue.pop_front();
        }
    }

    void AccelerationStructureBuilder::destroyOldData()
    {
        //destroy old structures and buffers
        while(!destructionQueue.empty())
        {
            vkDestroyAccelerationStructureKHR(renderer.getDevice().getDevice(), destructionQueue.front().structure, nullptr);
            destructionQueue.front().buffer.reset();

            destructionQueue.pop();
        }
    }

    void AccelerationStructureBuilder::submitQueuedOps(const SynchronizationInfo& syncInfo, VkAccelerationStructureTypeKHR type)
    {
        Timer timer(renderer, "Submit Queued AS Ops", REGULAR);
        
        //set build data
        setBuildData();

        //rebuild scratch buffer if needed
        const VkDeviceSize requiredScratchSize = getScratchSize(buildData.blasDatas, buildData.tlasDatas);
        if(!scratchBuffer || scratchBuffer->getSize() < requiredScratchSize)
        {
            scratchBuffer.reset();

            const BufferInfo bufferInfo = {
                .size = requiredScratchSize,
                .usageFlags = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR,
                .allocationFlags = 0
            };
            scratchBuffer = std::make_unique<Buffer>(renderer, bufferInfo);
        }

        //switch statement for type
        uint32_t numCompactions = 0;
        switch(type)
        {
        case VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR:
            numCompactions = buildData.numBlasCompactions;
            break;

        case VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR:
            numCompactions = buildData.numTlasCompactions;
            break;

        default:
            renderer.getLogger().recordLog({
                .type = WARNING,
                .text = "Ambiguous acceleration structure type specified. Please specify build type of TOP or BOTTOM"
            });
            numCompactions = 0xFFFFFFFF; //arbitrarily large number for error handling
            break;
        }

        //query pool for compaction if needed
        VkQueryPool queryPool = VK_NULL_HANDLE;
        uint32_t queryIndex = 0;

        if(numCompactions && numCompactions != 0xFFFFFFFF)
        {
            VkQueryPoolCreateInfo queryPoolInfo = {};
            queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            queryPoolInfo.pNext = NULL;
            queryPoolInfo.flags = 0;
            queryPoolInfo.queryCount = numCompactions;
            queryPoolInfo.queryType  = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
            
            vkCreateQueryPool(renderer.getDevice().getDevice(), &queryPoolInfo, nullptr, &queryPool);
            vkResetQueryPool(renderer.getDevice().getDevice(), queryPool, 0, queryPoolInfo.queryCount);
        }

        //temporary buffers used when compaction is enabled
        struct PreCompactBuffer
        {
            std::unique_ptr<Buffer> tempBuffer;
            AS& as;
        };
        std::queue<PreCompactBuffer> preCompactBuffers;

        //start command buffer
        VkCommandBuffer cmdBuffer = renderer.getDevice().getCommands().getCommandBuffer(COMPUTE);

        VkCommandBufferBeginInfo cmdBufferInfo = {};
        cmdBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBufferInfo.pNext = NULL;
        cmdBufferInfo.pInheritanceInfo = NULL;
        cmdBufferInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(cmdBuffer, &cmdBufferInfo);

        //make sure valid build type was specified
        if(numCompactions != 0xFFFFFFFF)
        {
            //builds and updates
            for(AsBuildData& data : (type & VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR ? buildData.blasDatas : buildData.tlasDatas))
            {
                //build TLAS instances if type is used and needed 
                if(type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR)
                {
                    //only rebuild/update a TLAS if any instances were updated
                    if(((TLAS&)data.as).nextUpdateSize)
                    {
                        renderer.tlasInstanceBuildPipeline.submit(cmdBuffer, (TLAS&)data.as);

                        //TLAS instance data memory barrier
                        VkBufferMemoryBarrier2 tlasInstanceMemBarrier = {
                            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                            .pNext = NULL,
                            .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
                            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                            .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
                            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                            .buffer = ((TLAS&)data.as).instancesBuffer->getBuffer(),
                            .offset = ((TLAS&)data.as).tlInstancesOffset,
                            .size = ((TLAS&)data.as).instancesBuffer->getSize() - ((TLAS&)data.as).tlInstancesOffset
                        };

                        VkDependencyInfo tlasInstanceDependencyInfo = {};
                        tlasInstanceDependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                        tlasInstanceDependencyInfo.pNext = NULL;
                        tlasInstanceDependencyInfo.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
                        tlasInstanceDependencyInfo.bufferMemoryBarrierCount = 1;
                        tlasInstanceDependencyInfo.pBufferMemoryBarriers = &tlasInstanceMemBarrier;

                        vkCmdPipelineBarrier2(cmdBuffer, &tlasInstanceDependencyInfo);
                    }
                    else
                    {
                        continue;
                    }
                }

                //destroy old structure if being built and is valid
                if(data.as.accelerationStructure && data.buildGeoInfo.mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR)
                {
                    vkDestroyAccelerationStructureKHR(renderer.getDevice().getDevice(), data.as.accelerationStructure, nullptr);
                }

                //create a temporary buffer if needed
                VkBuffer buffer = VK_NULL_HANDLE;
                if(data.compact)
                { 
                    BufferInfo tempBufferInfo = {};
                    tempBufferInfo.allocationFlags = 0;
                    tempBufferInfo.size = data.buildSizeInfo.accelerationStructureSize;
                    tempBufferInfo.usageFlags = VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
                    
                    preCompactBuffers.emplace(std::make_unique<Buffer>(renderer, tempBufferInfo), data.as);
                    buffer = preCompactBuffers.back().tempBuffer->getBuffer();
                }
                else
                {
                    buffer = data.as.asBuffer->getBuffer();
                }

                //set scratch buffer address + offset
                data.buildGeoInfo.scratchData.deviceAddress = scratchBuffer->getBufferDeviceAddress() + data.scratchDataOffset;

                //create acceleration structure
                VkAccelerationStructureCreateInfoKHR accelStructureInfo = {};
                accelStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
                accelStructureInfo.pNext = NULL;
                accelStructureInfo.createFlags = 0;
                accelStructureInfo.buffer = buffer;
                accelStructureInfo.offset = 0;
                accelStructureInfo.size = data.buildSizeInfo.accelerationStructureSize;
                accelStructureInfo.type = type;
                
                vkCreateAccelerationStructureKHR(renderer.getDevice().getDevice(), &accelStructureInfo, nullptr, &data.as.accelerationStructure);

                //set dst structure
                data.buildGeoInfo.dstAccelerationStructure = data.as.accelerationStructure;

                //convert format of build ranges
                std::vector<VkAccelerationStructureBuildRangeInfoKHR const*> buildRangesPtrArray;
                for(const VkAccelerationStructureBuildRangeInfoKHR& buildRange : data.buildRangeInfos)
                {
                    buildRangesPtrArray.emplace_back(&buildRange);
                }

                //build command
                vkCmdBuildAccelerationStructuresKHR(cmdBuffer, 1, &data.buildGeoInfo, buildRangesPtrArray.data());

                //write compaction data if enabled
                if(data.compact)
                {
                    //memory barrier
                    VkBufferMemoryBarrier2 compactionMemBarrier = {
                        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                        .pNext = NULL,
                        .srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                        .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR,
                        .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .buffer = buffer,
                        .offset = 0,
                        .size = VK_WHOLE_SIZE
                    };

                    VkDependencyInfo compactionDependencyInfo = {};
                    compactionDependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    compactionDependencyInfo.pNext = NULL;
                    compactionDependencyInfo.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
                    compactionDependencyInfo.bufferMemoryBarrierCount = 1;
                    compactionDependencyInfo.pBufferMemoryBarriers = &compactionMemBarrier;

                    vkCmdPipelineBarrier2(cmdBuffer, &compactionDependencyInfo);     

                    //write 
                    vkCmdWriteAccelerationStructuresPropertiesKHR(
                        cmdBuffer,
                        1,
                        &data.as.accelerationStructure,
                        VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                        queryPool,
                        queryIndex
                    );
                    queryIndex++;
                }
            }
        }

        //end command buffer and submit
        vkEndCommandBuffer(cmdBuffer);

        renderer.getDevice().getCommands().unlockCommandBuffer(cmdBuffer);

        SynchronizationInfo buildSyncInfo = {};
        buildSyncInfo.queueType = COMPUTE;
        buildSyncInfo.binaryWaitPairs = syncInfo.binaryWaitPairs;
        buildSyncInfo.timelineWaitPairs = syncInfo.timelineWaitPairs;
        buildSyncInfo.fence = VK_NULL_HANDLE;

        if(!queryPool)
        {
            buildSyncInfo.binarySignalPairs = syncInfo.binarySignalPairs;
            buildSyncInfo.timelineSignalPairs = syncInfo.timelineSignalPairs;
            buildSyncInfo.fence = syncInfo.fence;
        }

        buildSyncInfo.timelineSignalPairs.push_back({ asBuildSemaphore, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR, finalSemaphoreValue + 1 });
        finalSemaphoreValue++;

        renderer.getDevice().getCommands().submitToQueue(buildSyncInfo, { cmdBuffer });

        //----------AS COMPACTION----------//

        if(queryPool)
        {
            //start new command buffer
            cmdBuffer = renderer.getDevice().getCommands().getCommandBuffer(COMPUTE);

            cmdBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            cmdBufferInfo.pNext = NULL;
            cmdBufferInfo.pInheritanceInfo = NULL;
            cmdBufferInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            vkBeginCommandBuffer(cmdBuffer, &cmdBufferInfo);

            //get query results and perform compaction
            std::vector<VkDeviceSize> compactionResults(numCompactions);
            vkGetQueryPoolResults(
                renderer.getDevice().getDevice(),
                queryPool,
                0,
                numCompactions,
                numCompactions * sizeof(VkDeviceSize),
                compactionResults.data(),
                sizeof(VkDeviceSize),
                VK_QUERY_RESULT_WAIT_BIT
            );

            //set new buffer and acceleration structure
            uint32_t index = 0;
            while(!preCompactBuffers.empty())
            {
                //store old in local variables
                PreCompactBuffer& tempBuffer = preCompactBuffers.front();

                //get compacted size
                const VkDeviceSize& newSize = compactionResults.at(index);

                //create new buffer
                BufferInfo bufferInfo = {};
                bufferInfo.allocationFlags = 0;
                bufferInfo.size = newSize;
                bufferInfo.usageFlags = VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
                std::unique_ptr<Buffer> newBuffer = std::make_unique<Buffer>(renderer, bufferInfo);

                //create new acceleration structure
                VkAccelerationStructureCreateInfoKHR accelStructureInfo = {};
                accelStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
                accelStructureInfo.pNext = NULL;
                accelStructureInfo.createFlags = 0;
                accelStructureInfo.buffer = newBuffer->getBuffer();
                accelStructureInfo.offset = 0;
                accelStructureInfo.size = newSize;
                accelStructureInfo.type = type;

                VkAccelerationStructureKHR oldStructure = tempBuffer.as.accelerationStructure;

                //overwrite blas' as handle
                vkCreateAccelerationStructureKHR(renderer.getDevice().getDevice(), &accelStructureInfo, nullptr, &tempBuffer.as.accelerationStructure);

                //copy
                VkCopyAccelerationStructureInfoKHR copyInfo = {};
                copyInfo.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR;
                copyInfo.pNext = NULL;
                copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
                copyInfo.src = oldStructure;
                copyInfo.dst = tempBuffer.as.accelerationStructure;

                vkCmdCopyAccelerationStructureKHR(cmdBuffer, &copyInfo);

                //queue destruction of old
                destructionQueue.push({oldStructure, std::move(tempBuffer.tempBuffer)});

                //set new buffer
                tempBuffer.as.asBuffer = std::move(newBuffer);

                //remove from queue
                preCompactBuffers.pop();
                index++;
            }

            //end cmd buffer
            vkEndCommandBuffer(cmdBuffer);

            renderer.getDevice().getCommands().unlockCommandBuffer(cmdBuffer);

            //submit
            SynchronizationInfo compactionSyncInfo = {};
            compactionSyncInfo.queueType = COMPUTE;
            compactionSyncInfo.timelineWaitPairs = { { asBuildSemaphore, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR, finalSemaphoreValue } };
            compactionSyncInfo.binarySignalPairs = syncInfo.binarySignalPairs;
            compactionSyncInfo.timelineSignalPairs = syncInfo.timelineSignalPairs;
            compactionSyncInfo.fence = syncInfo.fence;

            compactionSyncInfo.timelineSignalPairs.push_back({ asBuildSemaphore, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR, finalSemaphoreValue + 1 });
            finalSemaphoreValue++;

            renderer.getDevice().getCommands().submitToQueue(compactionSyncInfo, { cmdBuffer });
            vkDestroyQueryPool(renderer.getDevice().getDevice(), queryPool, nullptr);
        }

        //clear build data
        (type & VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR ? buildData.blasDatas : buildData.tlasDatas).clear();
    }
}