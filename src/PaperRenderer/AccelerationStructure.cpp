#include "AccelerationStructure.h"
#include "PaperRenderer.h"

#include <algorithm>

namespace PaperRenderer
{
    //----------TLAS INSTANCE BUILD PIPELINE DEFINITIONS----------//

    TLASInstanceBuildPipeline::TLASInstanceBuildPipeline(RenderEngine *renderer, std::string fileDir)
        :ComputeShader(renderer),
        rendererPtr(renderer)
    {
        //uniform buffer
        BufferInfo uboInfo = {};
        uboInfo.allocationFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        uboInfo.usageFlags = VK_BUFFER_USAGE_2_UNIFORM_BUFFER_BIT_KHR;
        uboInfo.size = sizeof(UBOInputData);
        uniformBuffer = std::make_unique<Buffer>(rendererPtr, uboInfo);
        
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
        uboInputData.objectCount = tlas.accelerationStructureInstances.size();

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
        bufferWrite2Info.buffer = tlas.instancesBuffer->getBuffer();
        bufferWrite2Info.offset = 0;
        bufferWrite2Info.range = tlas.accelerationStructureInstances.size() * sizeof(ModelInstance::AccelerationStructureInstance);

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
        workGroupSizes.x = ((tlas.accelerationStructureInstances.size()) / 256) + 1;
        dispatch(cmdBuffer);
    }

    //----------BLAS DEFINITIONS----------//

    BLAS::BLAS(RenderEngine* renderer, Model const* model, Buffer const* vbo)
        :rendererPtr(renderer),
        parentModelPtr(model)
    {
        if(!model) throw std::runtime_error("Model pointer cannot be null in BLAS creation");
        if(vbo) vboPtr = vbo;
    }

    BLAS::~BLAS()
    {
        if(accelerationStructure)
        {
            vkDestroyAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), accelerationStructure, nullptr);
        }

        blasBuffer.reset();
    }

    //----------TLAS DEFINITIONS----------//
    
    TLAS::TLAS(RenderEngine* renderer)
        :rendererPtr(renderer)
    {
        rendererPtr->tlAccelerationStructures.push_back(this);
    }

    TLAS::~TLAS()
    {
        if(accelerationStructure)
        {
            vkDestroyAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), accelerationStructure, nullptr);
        }
        
        tlasBuffer.reset();
        instancesBuffer.reset();

        rendererPtr->tlAccelerationStructures.remove(this);
    }

    void TLAS::rebuildInstancesBuffer()
    {
        //instances
        VkDeviceSize newInstancesSize = Device::getAlignment(
            std::max((VkDeviceSize)(accelerationStructureInstances.size() * sizeof(ModelInstance::AccelerationStructureInstance) * instancesOverhead),
            (VkDeviceSize)(sizeof(ModelInstance::AccelerationStructureInstance) * 64)),
            rendererPtr->getDevice()->getGPUProperties().properties.limits.minStorageBufferOffsetAlignment
        );

        //instances description
        VkDeviceSize newInstanceDescriptionsSize = Device::getAlignment(
            std::max((VkDeviceSize)(accelerationStructureInstances.size() * sizeof(InstanceDescription) * instancesOverhead),
            (VkDeviceSize)(sizeof(InstanceDescription) * 64)),
            rendererPtr->getDevice()->getGPUProperties().properties.limits.minStorageBufferOffsetAlignment
        );

        //tl instances
        VkDeviceSize newTLInstancesSize = Device::getAlignment(
            std::max((VkDeviceSize)(accelerationStructureInstances.size() * sizeof(VkAccelerationStructureInstanceKHR) * instancesOverhead),
            (VkDeviceSize)(sizeof(VkAccelerationStructureInstanceKHR) * 64)),
            rendererPtr->getDevice()->getGPUProperties().properties.limits.minStorageBufferOffsetAlignment
        );
        
        //offsets
        instanceDescriptionsOffset = newInstancesSize;
        tlInstancesOffset = newInstancesSize + newInstanceDescriptionsSize;

        //buffer
        BufferInfo instancesBufferInfo = {};
        instancesBufferInfo.allocationFlags = 0;
        instancesBufferInfo.size = newInstancesSize + newInstancesSize + newInstanceDescriptionsSize;
        instancesBufferInfo.usageFlags = VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT_KHR | VK_BUFFER_USAGE_2_TRANSFER_DST_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        std::unique_ptr<Buffer> newInstancesBuffer = std::make_unique<Buffer>(rendererPtr, instancesBufferInfo);

        //copy old data into new if old existed
        if(instancesBuffer)
        {
            VkBufferCopy instancesCopyRegion = {};
            instancesCopyRegion.srcOffset = 0;
            instancesCopyRegion.dstOffset = 0;
            instancesCopyRegion.size = instancesBuffer->getSize();

            SynchronizationInfo syncInfo = {};
            syncInfo.queueType = TRANSFER;
            syncInfo.fence = Commands::getUnsignaledFence(rendererPtr);

            //start command buffer
            VkCommandBuffer cmdBuffer = Commands::getCommandBuffer(rendererPtr, syncInfo.queueType);

            VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.pNext = NULL;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

            vkBeginCommandBuffer(cmdBuffer, &beginInfo);
            vkCmdCopyBuffer(cmdBuffer, instancesBuffer->getBuffer(), newInstancesBuffer->getBuffer(), 1, &instancesCopyRegion);
            vkEndCommandBuffer(cmdBuffer);

            //submit
            Commands::submitToQueue(syncInfo, { cmdBuffer });

            rendererPtr->recycleCommandBuffer({ cmdBuffer, syncInfo.queueType });

            vkWaitForFences(rendererPtr->getDevice()->getDevice(), 1, &syncInfo.fence, VK_TRUE, UINT64_MAX);
            vkDestroyFence(rendererPtr->getDevice()->getDevice(), syncInfo.fence, nullptr);
        }
        
        //replace old buffers
        instancesBuffer = std::move(newInstancesBuffer);
    }

    void TLAS::queueInstanceTransfers()
    {
        //check buffer sizes
        if(!instancesBuffer || instancesBuffer->getSize() / sizeof(ModelInstance::AccelerationStructureInstance) < accelerationStructureInstances.size() ||
            instancesBuffer->getSize() / sizeof(ModelInstance::AccelerationStructureInstance) > accelerationStructureInstances.size() * 2)
        {
            rebuildInstancesBuffer();
        }

        //sort instances; remove duplicates
        std::sort(toUpdateInstances.begin(), toUpdateInstances.end());
        auto sortedInstances = std::unique(toUpdateInstances.begin(), toUpdateInstances.end());
        toUpdateInstances.erase(sortedInstances, toUpdateInstances.end());

        //queue instance data
        for(ModelInstance* instance : toUpdateInstances)
        {
            //skip if instance is NULL
            if(!instance) continue;

            //write instance data
            ModelInstance::AccelerationStructureInstance instanceShaderData = {};
            instanceShaderData.blasReference = instance->accelerationStructureSelfReferences.at(this).blasAddress;
            instanceShaderData.modelInstanceIndex = instance->rendererSelfIndex;

            std::vector<char> instanceData(sizeof(ModelInstance::AccelerationStructureInstance));
            memcpy(instanceData.data(), &instanceShaderData, instanceData.size());

            //write description data
            InstanceDescription descriptionShaderData = {};
            descriptionShaderData.modelDataOffset = instance->getParentModelPtr()->getShaderDataLocation(); //TODO MODEL DATA COMPACTION ERROR

            std::vector<char> descriptionData(sizeof(InstanceDescription));
            memcpy(descriptionData.data(), &descriptionShaderData, descriptionData.size());
            
            //queue data transfers
            rendererPtr->getEngineStagingBuffer()->queueDataTransfers(*instancesBuffer, sizeof(ModelInstance::AccelerationStructureInstance) * instance->rendererSelfIndex, instanceData);
            rendererPtr->getEngineStagingBuffer()->queueDataTransfers(*instancesBuffer, instanceDescriptionsOffset + sizeof(InstanceDescription) * instance->rendererSelfIndex, descriptionData);
        }

        //clear deques
        toUpdateInstances.clear();
    }

    void TLAS::addInstance(ModelInstance *instance)
    {
        //add reference
        instance->accelerationStructureSelfReferences[this] = accelerationStructureInstances.size();
        accelerationStructureInstances.push_back(instance);

        //queue data transfer
        toUpdateInstances.push_front(instance);
    }
    
    void TLAS::removeInstance(ModelInstance *instance)
    {
        //remove reference
        if(accelerationStructureInstances.size() > 1)
        {
            uint32_t& selfReference = instance->accelerationStructureSelfReferences.at(this);
            accelerationStructureInstances.at(selfReference) = accelerationStructureInstances.back();
            accelerationStructureInstances.at(selfReference)->accelerationStructureSelfReferences.at(this) = selfReference;

            //queue data transfer
            toUpdateInstances.push_front(accelerationStructureInstances.at(instance->accelerationStructureSelfReferences.at(this)));
            
            accelerationStructureInstances.pop_back();
        }
        else
        {
            accelerationStructureInstances.clear();
        }

        //null out any instances that may be queued
        for(ModelInstance*& thisInstance : toUpdateInstances)
        {
            if(thisInstance == instance)
            {
                thisInstance = NULL;
            }
        }

        instance->accelerationStructureSelfReferences.erase(this);
    }

    //----------AS BUILDER DEFINITIONS----------//

    AccelerationStructureBuilder::AccelerationStructureBuilder(RenderEngine *renderer)
        :rendererPtr(renderer)
    {
        asBuildSemaphore = Commands::getSemaphore(rendererPtr);
    }

    AccelerationStructureBuilder::~AccelerationStructureBuilder()
    {
        vkDestroySemaphore(rendererPtr->getDevice()->getDevice(), asBuildSemaphore, nullptr);

        scratchBuffer.reset();
    }

    AccelerationStructureBuilder::BlasBuildData AccelerationStructureBuilder::getBlasData(const BlasBuildOp& blasOp, VkBuildAccelerationStructureModeKHR mode) const
    {
        BlasBuildData returnData = {
            .blas = *blasOp.blas
        };
        std::vector<uint32_t> primitiveCounts;

        //get per material group geometry data
        for(const LODMeshGroup& meshGroup : blasOp.blas->getParentModelPtr()->getLODs().at(0).meshMaterialData) //use LOD 0 for BLAS
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
            trianglesGeometry.vertexData = VkDeviceOrHostAddressConstKHR{.deviceAddress = blasOp.blas->getVBOAddress()};
            trianglesGeometry.maxVertex = vertexCount;
            trianglesGeometry.vertexStride = blasOp.blas->getParentModelPtr()->getVertexDescription().stride;
            trianglesGeometry.indexType = VK_INDEX_TYPE_UINT32;
            trianglesGeometry.indexData = VkDeviceOrHostAddressConstKHR{.deviceAddress = blasOp.blas->getParentModelPtr()->getIBOAddress()};
            //trianglesGeometry.transformData = transformMatrixBufferAddress;

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

        //build information
        returnData.buildGeoInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        returnData.buildGeoInfo.pNext = NULL;
        returnData.buildGeoInfo.flags = blasOp.flags;
        returnData.buildGeoInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        returnData.buildGeoInfo.mode = mode;
        returnData.buildGeoInfo.srcAccelerationStructure = mode == VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR ? blasOp.blas->accelerationStructure : VK_NULL_HANDLE;
        returnData.buildGeoInfo.dstAccelerationStructure = blasOp.blas->accelerationStructure; //probably overwritten by code after
        returnData.buildGeoInfo.geometryCount = returnData.geometries.size();
        returnData.buildGeoInfo.pGeometries = returnData.geometries.data();
        returnData.buildGeoInfo.ppGeometries = NULL;

        vkGetAccelerationStructureBuildSizesKHR(
            rendererPtr->getDevice()->getDevice(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &returnData.buildGeoInfo,
            primitiveCounts.data(),
            &returnData.buildSizeInfo);

        //destroy buffer if compaction is requested since final size will be unknown
        if(blasOp.flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)
        {
            blasOp.blas->blasBuffer.reset();
        }
        //otherwise update buffer if needed
        else if(!blasOp.blas->blasBuffer || blasOp.blas->blasBuffer->getSize() < returnData.buildSizeInfo.accelerationStructureSize)
        {
            blasOp.blas->blasBuffer.reset();

            BufferInfo bufferInfo = {};
            bufferInfo.allocationFlags = 0;
            bufferInfo.size = returnData.buildSizeInfo.accelerationStructureSize;
            bufferInfo.usageFlags = VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
            blasOp.blas->blasBuffer = std::make_unique<Buffer>(rendererPtr, bufferInfo);
        }

        //set blas flags
        blasOp.blas->enabledFlags = blasOp.flags;

        return returnData;
    }

    AccelerationStructureBuilder::TlasBuildData AccelerationStructureBuilder::getTlasData(TLAS &tlas) const
    {
        TlasBuildData returnData = {
            .tlas = tlas
        };

        //geometries
        VkAccelerationStructureGeometryInstancesDataKHR geoInstances = {};
        geoInstances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
        geoInstances.pNext = NULL;
        geoInstances.arrayOfPointers = VK_FALSE;
        geoInstances.data = VkDeviceOrHostAddressConstKHR{ .deviceAddress = tlas.instancesBuffer->getBufferDeviceAddress() + tlas.tlInstancesOffset };

        VkAccelerationStructureGeometryDataKHR geometry = {};
        geometry.instances = geoInstances;

        returnData.geometry = std::make_unique<VkAccelerationStructureGeometryKHR>();
        returnData.geometry->sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        returnData.geometry->pNext = NULL;
        returnData.geometry->flags = VK_GEOMETRY_OPAQUE_BIT_KHR; //TODO TRANSPARENCY
        returnData.geometry->geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
        returnData.geometry->geometry = std::move(geometry);

        //size requirements
        VkAccelerationStructureBuildGeometryInfoKHR buildGeoInfo = {};
        buildGeoInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        buildGeoInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        buildGeoInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR; //according to NVIDIA, this is most optimal for TLAS almost always
        buildGeoInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        buildGeoInfo.srcAccelerationStructure = VK_NULL_HANDLE;
        buildGeoInfo.geometryCount = 1;
        buildGeoInfo.pGeometries = returnData.geometry.get();

        const uint32_t primitiveCount = tlas.accelerationStructureInstances.size();

        returnData.buildSizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
        vkGetAccelerationStructureBuildSizesKHR(
            rendererPtr->getDevice()->getDevice(),
            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
            &buildGeoInfo,
            &primitiveCount,
            &returnData.buildSizeInfo);

        //update buffer if needed
        if(!tlas.tlasBuffer || tlas.tlasBuffer->getSize() < returnData.buildSizeInfo.accelerationStructureSize)
        {
            tlas.tlasBuffer.reset();

            BufferInfo bufferInfo = {};
            bufferInfo.allocationFlags = 0;
            bufferInfo.size = returnData.buildSizeInfo.accelerationStructureSize;
            bufferInfo.usageFlags = VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
            tlas.tlasBuffer = std::make_unique<Buffer>(rendererPtr, bufferInfo);
        }

        returnData.buildGeoInfo = std::move(buildGeoInfo);

        return returnData;
    }

    VkDeviceSize AccelerationStructureBuilder::getScratchSize(
        std::vector<BlasBuildData>& updateDatas,
        std::vector<BlasBuildData>& buildDatas,
        std::vector<TlasBuildData>& tlasDatas
    ) const
    {
        VkDeviceSize size = 0;
        const uint32_t alignment = rendererPtr->getDevice()->getASproperties().minAccelerationStructureScratchOffsetAlignment;

        for(BlasBuildData& data : updateDatas)
        {
            data.scratchDataOffset = size;
            size += Device::getAlignment(data.buildSizeInfo.buildScratchSize, alignment);
        }
        for(BlasBuildData& data : buildDatas)
        {
            data.scratchDataOffset = size;
            size += Device::getAlignment(data.buildSizeInfo.buildScratchSize, alignment);
        }
        for(TlasBuildData& data : tlasDatas)
        {
            data.scratchDataOffset = size;
            size += Device::getAlignment(data.buildSizeInfo.buildScratchSize, alignment);
        }

        return size;
    }

    void AccelerationStructureBuilder::setBuildData()
    {
        //reset data
        buildData = {};

        //get all build data
        buildData.blasUpdateDatas.reserve(blasUpdateQueue.size());
        for(BLAS* blas : blasUpdateQueue)
        {
            buildData.blasUpdateDatas.emplace_back(getBlasData({ blas, VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR }, VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR));
            if(buildData.blasUpdateDatas.rbegin()->compact) buildData.numCompactions++;
        }
        buildData.blasBuildDatas.reserve(blasBuildQueue.size());
        for(const BlasBuildOp& blasBuildOp : blasBuildQueue)
        {
            buildData.blasBuildDatas.emplace_back(getBlasData(blasBuildOp, VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR));
            if(buildData.blasUpdateDatas.rbegin()->compact) buildData.numCompactions++;
        }
        buildData.tlasBuildDatas.reserve(tlasBuildQueue.size());
        for(TLAS* tlas : tlasBuildQueue)
        {
            buildData.tlasBuildDatas.emplace_back(getTlasData(*tlas));
        }
    }

    void AccelerationStructureBuilder::destroyOldData()
    {
        //destroy old structures and buffers
        while(!destructionQueue.empty())
        {
            vkDestroyAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), destructionQueue.back().structure, nullptr);
            destructionQueue.back().buffer.reset();

            destructionQueue.pop();
        }
    }

    void AccelerationStructureBuilder::submitQueuedStructureOps(const SynchronizationInfo &syncInfo)
    {
        //too lazy to refactor lmao
        std::vector<BlasBuildData>& blasUpdateDatas = buildData.blasUpdateDatas;
        std::vector<BlasBuildData>& blasBuildDatas = buildData.blasBuildDatas;
        std::vector<TlasBuildData>& tlasBuildDatas = buildData.tlasBuildDatas;

        //rebuild scratch buffer if needed
        const VkDeviceSize requiredScratchSize = getScratchSize(blasUpdateDatas, blasBuildDatas, tlasBuildDatas);
        if(!(scratchBuffer && scratchBuffer->getSize() < requiredScratchSize))
        {
            scratchBuffer.reset();

            BufferInfo bufferInfo = {};
            bufferInfo.allocationFlags = 0;
            bufferInfo.size = requiredScratchSize;
            bufferInfo.usageFlags = VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
            scratchBuffer = std::make_unique<Buffer>(rendererPtr, bufferInfo);
        }

        //query pool for compaction if needed
        VkQueryPool queryPool = VK_NULL_HANDLE;
        uint32_t queryIndex = 0;

        if(buildData.numCompactions)
        {
            VkQueryPoolCreateInfo queryPoolInfo = {};
            queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
            queryPoolInfo.pNext = NULL;
            queryPoolInfo.flags = 0;
            queryPoolInfo.queryCount = buildData.numCompactions;
            queryPoolInfo.queryType  = VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR;
            
            vkCreateQueryPool(rendererPtr->getDevice()->getDevice(), &queryPoolInfo, nullptr, &queryPool);
            vkResetQueryPool(rendererPtr->getDevice()->getDevice(), queryPool, 0, queryPoolInfo.queryCount);
        }

        //temporary buffers used when compaction is enabled
        struct PreCompactBuffer
        {
            std::unique_ptr<Buffer> tempBuffer;
            BLAS& blas;
        };
        std::queue<PreCompactBuffer> preCompactBuffers;

        //start command buffer
        VkCommandBuffer cmdBuffer = Commands::getCommandBuffer(rendererPtr, COMPUTE);

        VkCommandBufferBeginInfo cmdBufferInfo = {};
        cmdBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBufferInfo.pNext = NULL;
        cmdBufferInfo.pInheritanceInfo = NULL;
        cmdBufferInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(cmdBuffer, &cmdBufferInfo);

        //blas updates  TODO THIS MIGHT LEAK GPU MEMORY!!!
        for(BlasBuildData& data : blasUpdateDatas)
        {
            //set scratch buffer address + offset
            data.buildGeoInfo.scratchData.deviceAddress = scratchBuffer->getBufferDeviceAddress() + data.scratchDataOffset;

            //create acceleration structure
            VkAccelerationStructureCreateInfoKHR accelStructureInfo = {};
            accelStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
            accelStructureInfo.pNext = NULL;
            accelStructureInfo.createFlags = 0;
            accelStructureInfo.buffer = data.blas.blasBuffer->getBuffer();
            accelStructureInfo.offset = 0;
            accelStructureInfo.size = data.buildSizeInfo.accelerationStructureSize;
            accelStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            
            vkCreateAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), &accelStructureInfo, nullptr, &data.blas.accelerationStructure);

            //set dst structure
            data.buildGeoInfo.dstAccelerationStructure = data.blas.accelerationStructure;

            //convert format of build ranges
            std::vector<VkAccelerationStructureBuildRangeInfoKHR const*> buildRangesPtrArray;
            for(const VkAccelerationStructureBuildRangeInfoKHR& buildRange : data.buildRangeInfos)
            {
                buildRangesPtrArray.emplace_back(&buildRange);
            }

            //build command
            vkCmdBuildAccelerationStructuresKHR(cmdBuffer, 1, &data.buildGeoInfo, buildRangesPtrArray.data());
        }

        //blas builds (compact too if flag enabled)
        for(BlasBuildData& data : blasBuildDatas)
        {
            //destroy old structure since this is a build
            if(data.blas.accelerationStructure)
            {
                vkDestroyAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), data.blas.accelerationStructure, nullptr);
            }

            //create a temporary buffer if needed
            VkBuffer buffer = VK_NULL_HANDLE;
            if(data.blas.enabledFlags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)
            { 
                BufferInfo tempBufferInfo = {};
                tempBufferInfo.allocationFlags = 0;
                tempBufferInfo.size = data.buildSizeInfo.accelerationStructureSize;
                tempBufferInfo.usageFlags = VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
                
                preCompactBuffers.emplace(std::make_unique<Buffer>(rendererPtr, tempBufferInfo), data.blas);
                buffer = preCompactBuffers.back().tempBuffer->getBuffer();
            }
            else
            {
                buffer = data.blas.blasBuffer->getBuffer();
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
            accelStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            
            vkCreateAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), &accelStructureInfo, nullptr, &data.blas.accelerationStructure);

            //set dst structure
            data.buildGeoInfo.dstAccelerationStructure = data.blas.accelerationStructure;

            //convert format of build ranges
            std::vector<VkAccelerationStructureBuildRangeInfoKHR const*> buildRangesPtrArray;
            for(const VkAccelerationStructureBuildRangeInfoKHR& buildRange : data.buildRangeInfos)
            {
                buildRangesPtrArray.emplace_back(&buildRange);
            }

            //build command
            vkCmdBuildAccelerationStructuresKHR(cmdBuffer, 1, &data.buildGeoInfo, buildRangesPtrArray.data());

            //compaction if flag used
            if(data.blas.enabledFlags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)
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
                    &data.blas.accelerationStructure,
                    VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                    queryPool,
                    queryIndex
                );
                queryIndex++;
            }
        }

        //end command buffer and submit
        vkEndCommandBuffer(cmdBuffer);

        SynchronizationInfo blasSyncInfo = {};
        blasSyncInfo.queueType = COMPUTE;
        blasSyncInfo.binaryWaitPairs = syncInfo.binaryWaitPairs;
        blasSyncInfo.timelineWaitPairs = syncInfo.timelineWaitPairs;
        blasSyncInfo.binarySignalPairs = { { asBuildSemaphore, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR } };
        blasSyncInfo.fence = VK_NULL_HANDLE;

        Commands::submitToQueue(blasSyncInfo, { cmdBuffer });

        rendererPtr->recycleCommandBuffer({ cmdBuffer, COMPUTE });

        //----------BLAS COMPACTION AND TLAS BUILD----------//

        std::vector<VkBufferMemoryBarrier2> compactionMemBarriers;

        //start new command buffer
        cmdBuffer = Commands::getCommandBuffer(rendererPtr, COMPUTE);

        cmdBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBufferInfo.pNext = NULL;
        cmdBufferInfo.pInheritanceInfo = NULL;
        cmdBufferInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(cmdBuffer, &cmdBufferInfo);

        //get query results and perform compaction
        if(queryPool)
        {
            std::vector<VkDeviceSize> compactionResults(buildData.numCompactions);
            vkGetQueryPoolResults(
                rendererPtr->getDevice()->getDevice(),
                queryPool,
                0,
                buildData.numCompactions,
                buildData.numCompactions * sizeof(VkDeviceSize),
                compactionResults.data(),
                sizeof(VkDeviceSize),
                VK_QUERY_RESULT_WAIT_BIT
            );

            //set new buffer and acceleration structure
            uint32_t index = 0;
            while(!preCompactBuffers.empty())
            {
                //store old in local variables
                PreCompactBuffer& tempBuffer = preCompactBuffers.back();
                VkAccelerationStructureKHR oldStructure = tempBuffer.blas.accelerationStructure;

                //get compacted size
                const VkDeviceSize& newSize = compactionResults.at(index);

                //create new buffer
                BufferInfo bufferInfo = {};
                bufferInfo.allocationFlags = 0;
                bufferInfo.size = newSize;
                bufferInfo.usageFlags = VK_BUFFER_USAGE_2_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT_KHR;
                std::unique_ptr<Buffer> newBuffer = std::make_unique<Buffer>(rendererPtr, bufferInfo);

                //create new acceleration structure
                VkAccelerationStructureCreateInfoKHR accelStructureInfo = {};
                accelStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
                accelStructureInfo.pNext = NULL;
                accelStructureInfo.createFlags = 0;
                accelStructureInfo.buffer = newBuffer->getBuffer();
                accelStructureInfo.offset = 0;
                accelStructureInfo.size = newSize;
                accelStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

                //overwrite blas' as handle
                vkCreateAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), &accelStructureInfo, nullptr, &tempBuffer.blas.accelerationStructure);

                //copy
                VkCopyAccelerationStructureInfoKHR copyInfo = {};
                copyInfo.sType = VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR;
                copyInfo.pNext = NULL;
                copyInfo.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
                copyInfo.src = oldStructure;
                copyInfo.dst = tempBuffer.blas.accelerationStructure;

                vkCmdCopyAccelerationStructureKHR(cmdBuffer, &copyInfo);

                //queue destruction of old
                destructionQueue.push({oldStructure, std::move(tempBuffer.blas.blasBuffer)});

                //set new buffer
                tempBuffer.blas.blasBuffer = std::move(newBuffer);

                //remove from queue
                preCompactBuffers.pop();
                index++;

                //memory barrier
                compactionMemBarriers.push_back({
                    .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                    .pNext = NULL,
                    .srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR,
                    .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
                    .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR,
                    .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .buffer = tempBuffer.blas.blasBuffer->getBuffer(),
                    .offset = 0,
                    .size = VK_WHOLE_SIZE
                });
            }
        }

        //tlas builds
        for(TlasBuildData& data : tlasBuildDatas)
        {
            //build TLAS instance data
            rendererPtr->tlasInstanceBuildPipeline.submit(cmdBuffer, data.tlas);

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
                .buffer = data.tlas.instancesBuffer->getBuffer(),
                .offset = 0,
                .size = VK_WHOLE_SIZE
            };

            VkDependencyInfo tlasInstanceDependencyInfo = {};
            tlasInstanceDependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            tlasInstanceDependencyInfo.pNext = NULL;
            tlasInstanceDependencyInfo.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
            tlasInstanceDependencyInfo.bufferMemoryBarrierCount = 1;
            tlasInstanceDependencyInfo.pBufferMemoryBarriers = &tlasInstanceMemBarrier;

            vkCmdPipelineBarrier2(cmdBuffer, &tlasInstanceDependencyInfo);

            //destroy old (if exists)
            if(data.tlas.accelerationStructure)
            {
                vkDestroyAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), data.tlas.accelerationStructure, nullptr);
            }

            //set scratch offset
            data.buildGeoInfo.scratchData.deviceAddress = scratchBuffer->getBufferDeviceAddress() + data.scratchDataOffset;

            //create acceleration structure
            VkAccelerationStructureCreateInfoKHR accelStructureInfo = {};
            accelStructureInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
            accelStructureInfo.pNext = NULL;
            accelStructureInfo.createFlags = 0;
            accelStructureInfo.buffer = data.tlas.tlasBuffer->getBuffer();
            accelStructureInfo.offset = 0;
            accelStructureInfo.size = data.buildSizeInfo.accelerationStructureSize;
            accelStructureInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

            vkCreateAccelerationStructureKHR(rendererPtr->getDevice()->getDevice(), &accelStructureInfo, nullptr, &data.tlas.accelerationStructure);
            
            //set dst structure
            data.buildGeoInfo.dstAccelerationStructure = data.tlas.accelerationStructure;

            //build command
            VkAccelerationStructureBuildRangeInfoKHR buildRange;
            buildRange.primitiveCount = data.tlas.accelerationStructureInstances.size();
            buildRange.primitiveOffset = 0;
            buildRange.firstVertex = 0;
            buildRange.transformOffset = 0;
            std::vector<VkAccelerationStructureBuildRangeInfoKHR const*> buildRangesPtrArray = { &buildRange };

            vkCmdBuildAccelerationStructuresKHR(cmdBuffer, 1, &data.buildGeoInfo, buildRangesPtrArray.data());
        }

        //end command buffer and submit
        vkEndCommandBuffer(cmdBuffer);

        SynchronizationInfo tlasSyncInfo = {};
        tlasSyncInfo.queueType = COMPUTE;
        tlasSyncInfo.binaryWaitPairs = { { asBuildSemaphore, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_COPY_BIT_KHR } };
        tlasSyncInfo.binarySignalPairs = syncInfo.binarySignalPairs;
        tlasSyncInfo.timelineSignalPairs = syncInfo.timelineSignalPairs;
        tlasSyncInfo.fence = VK_NULL_HANDLE;

        Commands::submitToQueue(tlasSyncInfo, { cmdBuffer });

        rendererPtr->recycleCommandBuffer({ cmdBuffer, COMPUTE });

        vkDestroyQueryPool(rendererPtr->getDevice()->getDevice(), queryPool, nullptr);
    }
}
