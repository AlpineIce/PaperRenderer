#include "AccelerationStructure.h"
#include "PaperRenderer.h"
#include "Material.h"
#include "RayTrace.h"

#include <algorithm>

namespace PaperRenderer
{
    //----------TLAS INSTANCE BUILD PIPELINE DEFINITIONS----------//

    TLASInstanceBuildPipeline::TLASInstanceBuildPipeline(RenderEngine& renderer, std::string fileDir)
        :ComputeShader(renderer),
        renderer(renderer)
    {
        //uniform buffer
        BufferInfo uboInfo = {};
        uboInfo.allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        uboInfo.usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR;
        uboInfo.size = sizeof(UBOInputData);
        uniformBuffer = std::make_unique<Buffer>(renderer, uboInfo);
        
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

        VkDescriptorSetLayoutBinding inputASInstancesDescriptor = {};
        inputASInstancesDescriptor.binding = 2;
        inputASInstancesDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        inputASInstancesDescriptor.descriptorCount = 1;
        inputASInstancesDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        descriptorSets[0].descriptorBindings[2] = inputASInstancesDescriptor;

        VkDescriptorSetLayoutBinding outputASInstancesDescriptor = {};
        outputASInstancesDescriptor.binding = 3;
        outputASInstancesDescriptor.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        outputASInstancesDescriptor.descriptorCount = 1;
        outputASInstancesDescriptor.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        descriptorSets[0].descriptorBindings[3] = outputASInstancesDescriptor;

        buildPipeline();
    }

    TLASInstanceBuildPipeline::~TLASInstanceBuildPipeline()
    {
        uniformBuffer.reset();
    }

    void TLASInstanceBuildPipeline::submit(VkCommandBuffer cmdBuffer, const TLAS& tlas)
    {
        UBOInputData uboInputData = {};
        uboInputData.objectCount = tlas.nextUpdateSize;

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
        bufferWrite3Info.range = tlas.accelerationStructureInstances.size() * sizeof(VkAccelerationStructureInstanceKHR);

        BuffersDescriptorWrites bufferWrite3 = {};
        bufferWrite3.binding = 3;
        bufferWrite3.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bufferWrite3.infos = { bufferWrite3Info };

        //bind, write, and dispatch
        bind(cmdBuffer);

        DescriptorWrites descriptorWritesInfo = {};
        descriptorWritesInfo.bufferWrites = { bufferWrite0, bufferWrite1, bufferWrite2, bufferWrite3 };
        descriptorWrites[0] = descriptorWritesInfo;
        writeDescriptorSet(cmdBuffer, 0);

        //dispatch
        workGroupSizes.x = (tlas.nextUpdateSize / 128) + 1;
        dispatch(cmdBuffer);
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

    BLAS::BLAS(RenderEngine& renderer, Model const* model, Buffer const* vbo)
        :AS(renderer),
        parentModelPtr(model)
    {
        if(!model) throw std::runtime_error("Model pointer cannot be null in BLAS creation");
        if(vbo) vboPtr = vbo;
    }

    BLAS::~BLAS()
    {
    }

    //----------TLAS DEFINITIONS----------//
    
    TLAS::TLAS(RenderEngine& renderer)
        :AS(renderer)
    {
    }

    TLAS::~TLAS()
    {
        instancesBuffer.reset();
    }

    void TLAS::verifyInstancesBuffer()
    {
        //instances
        const VkDeviceSize newInstancesSize = Device::getAlignment(
            std::max((VkDeviceSize)(accelerationStructureInstances.size() * sizeof(ModelInstance::AccelerationStructureInstance) * instancesOverhead),
            (VkDeviceSize)(sizeof(ModelInstance::AccelerationStructureInstance) * 64)),
            renderer.getDevice().getGPUProperties().properties.limits.minStorageBufferOffsetAlignment
        );

        //instances description
        const VkDeviceSize newInstanceDescriptionsSize = Device::getAlignment(
            std::max((VkDeviceSize)(accelerationStructureInstances.size() * sizeof(InstanceDescription) * instancesOverhead),
            (VkDeviceSize)(sizeof(InstanceDescription) * 64)),
            renderer.getDevice().getGPUProperties().properties.limits.minStorageBufferOffsetAlignment
        );

        //tl instances
        const VkDeviceSize newTLInstancesSize = Device::getAlignment(
            std::max((VkDeviceSize)(accelerationStructureInstances.size() * sizeof(VkAccelerationStructureInstanceKHR) * instancesOverhead),
            (VkDeviceSize)(sizeof(VkAccelerationStructureInstanceKHR) * 64)),
            renderer.getDevice().getGPUProperties().properties.limits.minStorageBufferOffsetAlignment
        );

        const VkDeviceSize totalBufferSize = newInstancesSize + newInstanceDescriptionsSize + newTLInstancesSize;

        if(!instancesBuffer || instancesBuffer->getSize() < totalBufferSize ||
            instancesBuffer->getSize() > totalBufferSize * 2)
        {            
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

                //submit
                renderer.getDevice().getCommands().submitToQueue(syncInfo, { cmdBuffer });

                vkWaitForFences(renderer.getDevice().getDevice(), 1, &syncInfo.fence, VK_TRUE, UINT64_MAX);
                vkDestroyFence(renderer.getDevice().getDevice(), syncInfo.fence, nullptr);
            }
            
            //replace old buffers
            instancesBuffer = std::move(newInstancesBuffer);
        }
    }

    void TLAS::queueInstanceTransfers(RayTraceRender const* rtRender)
    {
        //set next update size to 0
        nextUpdateSize = 0;

        //check buffer sizes
        verifyInstancesBuffer();

        //queue instance data
        uint32_t instanceIndex = 0;
        for(ModelInstance* instance : accelerationStructureInstances)
        {
            //skip if instance is NULL or has invalid BLAS
            if(!instance || !instance->getBLAS()->getAccelerationStructureAddress())
            {
                continue;
            }
            else
            {
                //increment update size if not NULL and has valid BLAS
                nextUpdateSize++;
            }

            //get sbt offset
            uint32_t sbtOffset = 
                rtRender->getPipeline().getShaderBindingTableData().shaderBindingTableOffsets.
                materialShaderGroupOffsets.at(instance->rtRenderSelfReferences.at(rtRender));

            //write instance data
            ModelInstance::AccelerationStructureInstance instanceShaderData = {};
            instanceShaderData.blasReference = instance->getBLAS()->getAccelerationStructureAddress();
            instanceShaderData.selfIndex = instance->rendererSelfIndex;
            instanceShaderData.modelInstanceIndex = instance->rendererSelfIndex;
            instanceShaderData.recordOffset = sbtOffset; //TODO

            std::vector<char> instanceData(sizeof(ModelInstance::AccelerationStructureInstance));
            memcpy(instanceData.data(), &instanceShaderData, instanceData.size());

            //write description data
            InstanceDescription descriptionShaderData = {};
            descriptionShaderData.modelDataOffset = instance->getParentModelPtr()->getShaderDataLocation(); //TODO MODEL DATA COMPACTION ERROR

            std::vector<char> descriptionData(sizeof(InstanceDescription));
            memcpy(descriptionData.data(), &descriptionShaderData, descriptionData.size());
            
            //queue data transfers
            renderer.getEngineStagingBuffer().queueDataTransfers(*instancesBuffer, sizeof(ModelInstance::AccelerationStructureInstance) * instanceIndex, instanceData);
            renderer.getEngineStagingBuffer().queueDataTransfers(*instancesBuffer, instanceDescriptionsOffset + sizeof(InstanceDescription) * instance->rendererSelfIndex, descriptionData);

            instanceIndex++;
        }

        //record transfers
        SynchronizationInfo syncInfo = {};
        syncInfo.queueType = TRANSFER;
        renderer.getEngineStagingBuffer().submitQueuedTransfers(syncInfo);
    }

    void TLAS::addInstance(ModelInstance *instance)
    {
        //add reference
        instance->accelerationStructureSelfReferences[this] = accelerationStructureInstances.size();
        accelerationStructureInstances.push_back(instance);
    }
    
    void TLAS::removeInstance(ModelInstance *instance)
    {
        //remove reference
        if(accelerationStructureInstances.size() > 1)
        {
            uint32_t& selfReference = instance->accelerationStructureSelfReferences.at(this);
            accelerationStructureInstances.at(selfReference) = accelerationStructureInstances.back();
            accelerationStructureInstances.at(selfReference)->accelerationStructureSelfReferences.at(this) = selfReference;
            
            accelerationStructureInstances.pop_back();
        }
        else
        {
            accelerationStructureInstances.clear();
        }

        //remove reference
        instance->accelerationStructureSelfReferences.erase(this);
    }

    //----------AS BUILDER DEFINITIONS----------//

    AccelerationStructureBuilder::AccelerationStructureBuilder(RenderEngine& renderer)
        :renderer(renderer)
    {
        asBuildSemaphore = renderer.getDevice().getCommands().getTimelineSemaphore(finalSemaphoreValue);
    }

    AccelerationStructureBuilder::~AccelerationStructureBuilder()
    {
        vkDestroySemaphore(renderer.getDevice().getDevice(), asBuildSemaphore, nullptr);

        scratchBuffer.reset();
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
            BLAS* blas = (BLAS*)(op.accelerationStructure);

            //get per material group geometry data
            for(const LODMeshGroup& meshGroup : blas->getParentModelPtr()->getLODs().at(0).meshMaterialData) //use LOD 0 for BLAS
            {
                VkDeviceSize vertexCount = 0;
                VkDeviceSize indexCount = 0;
                VkDeviceAddress vertexOffset = meshGroup.meshes.at(0).vboOffset;
                VkDeviceAddress indexOffset = meshGroup.meshes.at(0).iboOffset;

                for(const LODMesh& mesh : meshGroup.meshes) //per mesh in mesh group data
                {
                    vertexCount += mesh.vertexCount;
                    indexCount += mesh.indexCount;
                }

                //buffer information
                VkAccelerationStructureGeometryTrianglesDataKHR trianglesGeometry = {};
                trianglesGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                trianglesGeometry.pNext = NULL;
                trianglesGeometry.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
                trianglesGeometry.vertexData = VkDeviceOrHostAddressConstKHR{.deviceAddress = blas->getVBOAddress()};
                trianglesGeometry.maxVertex = vertexCount;
                trianglesGeometry.vertexStride = blas->getParentModelPtr()->getVertexDescription().stride;
                trianglesGeometry.indexType = VK_INDEX_TYPE_UINT32;
                trianglesGeometry.indexData = VkDeviceOrHostAddressConstKHR{.deviceAddress = blas->getParentModelPtr()->getIBOAddress()};

                //geometries
                VkAccelerationStructureGeometryKHR structureGeometry = {};
                structureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
                structureGeometry.pNext = NULL;
                structureGeometry.flags = meshGroup.invokeAnyHit ? VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR : VK_GEOMETRY_OPAQUE_BIT_KHR; 
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
            TLAS* tlas = (TLAS*)(op.accelerationStructure);

            //geometries
            VkAccelerationStructureGeometryInstancesDataKHR geoInstances = {};
            geoInstances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
            geoInstances.pNext = NULL;
            geoInstances.arrayOfPointers = VK_FALSE;
            geoInstances.data = VkDeviceOrHostAddressConstKHR{ .deviceAddress = tlas->instancesBuffer->getBufferDeviceAddress() + tlas->tlInstancesOffset };

            VkAccelerationStructureGeometryKHR structureGeometry = {};
            structureGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
            structureGeometry.pNext = NULL;
            structureGeometry.flags = VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR;
            structureGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
            structureGeometry.geometry.instances = geoInstances;

            VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo;
            buildRangeInfo.primitiveCount = tlas->accelerationStructureInstances.size();
            buildRangeInfo.primitiveOffset = 0;
            buildRangeInfo.firstVertex = 0;
            buildRangeInfo.transformOffset = 0;

            returnData.geometries.emplace_back(structureGeometry);
            returnData.buildRangeInfos.emplace_back(buildRangeInfo);
            primitiveCounts.emplace_back(tlas->accelerationStructureInstances.size());
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
        returnData.buildGeoInfo.srcAccelerationStructure = returnData.compact ? op.accelerationStructure->accelerationStructure : VK_NULL_HANDLE;
        returnData.buildGeoInfo.dstAccelerationStructure = op.accelerationStructure->accelerationStructure; //probably overwritten by code after
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
            op.accelerationStructure->asBuffer.reset();
        }
        //otherwise update buffer if needed
        else if(!op.accelerationStructure->asBuffer || op.accelerationStructure->asBuffer->getSize() < returnData.buildSizeInfo.accelerationStructureSize)
        {
            op.accelerationStructure->asBuffer.reset();

            BufferInfo bufferInfo = {};
            bufferInfo.allocationFlags = 0;
            bufferInfo.size = returnData.buildSizeInfo.accelerationStructureSize;
            bufferInfo.usageFlags = VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
            op.accelerationStructure->asBuffer = std::make_unique<Buffer>(renderer, bufferInfo);
        }

        //set blas flags
        op.accelerationStructure->enabledFlags = op.flags;

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
        return std::max(blasSize, tlasSize);
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
                throw std::runtime_error("Ambiguous acceleration structure type tried to be built. Please specify build type");
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
        //rebuild scratch buffer if needed
        const VkDeviceSize requiredScratchSize = getScratchSize(buildData.blasDatas, buildData.tlasDatas);
        if(!scratchBuffer || scratchBuffer->getSize() < requiredScratchSize)
        {
            scratchBuffer.reset();

            BufferInfo bufferInfo = {};
            bufferInfo.allocationFlags = 0;
            bufferInfo.size = requiredScratchSize;
            bufferInfo.usageFlags = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
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
            throw std::runtime_error("Ambiguous acceleration structure type tried to be built. Please specify build type");
            break;
        }

        //query pool for compaction if needed
        VkQueryPool queryPool = VK_NULL_HANDLE;
        uint32_t queryIndex = 0;

        if(numCompactions)
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
            AS* as;
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

        //builds and updates
        std::vector<AsBuildData>& buildDataRef = (type & VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR ? buildData.blasDatas : buildData.tlasDatas);
        for(AsBuildData& data : buildDataRef)
        {
            //build TLAS instances if type is used and needed 
            if(type == VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR)
            {
                //only rebuild/update a TLAS if any instances were updated
                if(((TLAS*)data.as)->nextUpdateSize)
                {
                    renderer.tlasInstanceBuildPipeline.submit(cmdBuffer, *((TLAS*)data.as));

                    //TLAS instance data memory barrier
                    VkBufferMemoryBarrier2 tlasInstanceMemBarrier = {
                        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                        .pNext = NULL,
                        .srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                        .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
                        .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                        .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .buffer = ((TLAS*)data.as)->instancesBuffer->getBuffer(),
                        .offset = ((TLAS*)data.as)->tlInstancesOffset,
                        .size = ((TLAS*)data.as)->instancesBuffer->getSize() - ((TLAS*)data.as)->tlInstancesOffset
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
            if(data.as->accelerationStructure && data.buildGeoInfo.mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR)
            {
                vkDestroyAccelerationStructureKHR(renderer.getDevice().getDevice(), data.as->accelerationStructure, nullptr);
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
                buffer = data.as->asBuffer->getBuffer();
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
            
            vkCreateAccelerationStructureKHR(renderer.getDevice().getDevice(), &accelStructureInfo, nullptr, &data.as->accelerationStructure);

            //set dst structure
            data.buildGeoInfo.dstAccelerationStructure = data.as->accelerationStructure;

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
                    &data.as->accelerationStructure,
                    VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                    queryPool,
                    queryIndex
                );
                queryIndex++;
            }
        }

        //end command buffer and submit
        vkEndCommandBuffer(cmdBuffer);

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

                VkAccelerationStructureKHR oldStructure = tempBuffer.as->accelerationStructure;

                //overwrite blas' as handle
                vkCreateAccelerationStructureKHR(renderer.getDevice().getDevice(), &accelStructureInfo, nullptr, &tempBuffer.as->accelerationStructure);

                //copy
                VkCopyAccelerationStructureInfoKHR copyInfo = {};
                copyInfo.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR;
                copyInfo.pNext = NULL;
                copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
                copyInfo.src = oldStructure;
                copyInfo.dst = tempBuffer.as->accelerationStructure;

                vkCmdCopyAccelerationStructureKHR(cmdBuffer, &copyInfo);

                //queue destruction of old
                destructionQueue.push({oldStructure, std::move(tempBuffer.tempBuffer)});

                //set new buffer
                tempBuffer.as->asBuffer = std::move(newBuffer);

                //remove from queue
                preCompactBuffers.pop();
                index++;
            }

            //end cmd buffer
            vkEndCommandBuffer(cmdBuffer);

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

        buildDataRef.clear();
    }
}